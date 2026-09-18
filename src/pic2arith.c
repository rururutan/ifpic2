/**
 * @file pic2arith.c
 * @brief PIC2 arithmetic-coded block decoder from the published reference loader.
 *
 * The original decoding algorithm is retained to preserve format accuracy;
 * platform I/O is supplied by pic2decode.c.
 */
/*
 *	PIC2 高圧縮/ベタフォーマットの展開 by やなぎさわ
 *
 */
static char rcsid[] = "($Id: p2load.c_ 1.10 1993/11/27 17:53:45 akira Exp akira $)";

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>

#define read_color p2arith_read_color
#define read_color15 p2arith_read_color15
#define read_color24 p2arith_read_color24
#include "pic2legacy.h"

/* 現在のP2の中の変数のコピー (^^; /下手するとかえって効率が悪い (^^; */

static ushort aa;
static ushort dd;
static char cache_hit_c;
static int bit_buf;
static int n_bit_buf;
static uchar *fbufp;
static int n_fbuf;
static P2 *p2;
static pix *vram_prev;
static pix *vram_now;
static pix *vram_next;
static schar *flag_now;
static schar *flag_next;
static schar *flag2_now;
static schar *flag2_next;
static schar *flag2_next2;
static pix (*cache)[N_COLOR_CACHE+1];
static ushort *cache_pos;
static ushort *mulu_tab;

static short ynow;

pix (*read_color)( ushort x, ushort y);

/* エラー時の脱出用 */
static jmp_buf jmp_env;

/* ファイルから uchar よみこむ */
static uchar
load_char( void)
{
	if ( --n_fbuf <= 0) {
		/* 次のバッファを読み込む */

		fbufp = p2->fbuf;
		n_fbuf = read_file2( p2, fbufp, N_FBUF);

		/* もう無しだとエラーにする
		 * 実際のブロック長でチェックされていない (^^;
		 * (続きのブロックを読み込んでいてもエラーにならない)
		 */
		if ( n_fbuf == 0) {
			p2errno = p2->error_code = P2E_FEOF;
			longjmp( jmp_env, 1);
		}
	}
	return ( *fbufp++);
}

/* 1ビット読み込みマクロ */
#define	load_bit() ((n_bit_buf /= 2) == 0			\
	? ((bit_buf = load_char()) & (n_bit_buf = 0x80)) 	\
	: (bit_buf & n_bit_buf))


/* 算術圧縮を１ビット復元 */
short
bit_decode( int c)
{
	ushort pp = mulu_tab[(aa & 0x7f00) / 2 + c];

	if ( dd >= pp) {
		dd -= pp;
		aa -= pp;

		while ( (short)aa >= 0) {
			dd *= 2;
			if ( load_bit()) dd++;
			aa *= 2;
		}
		return ( 1);
	
	} else {
		aa = pp;

		while ( (short)aa >= 0) {
			dd *= 2;
			if ( load_bit()) dd++;
			aa *= 2;
		}
		return ( 0);

	}
}

/*
 * 確立空間 cで 0..255の数値を得る
 * なお c..c+7 にてビット長さを得る
 * c+8..c+14でビット分だけ各ビットを得る
 */
short
nn_decode( int c)
{
	int n;

	if ( bit_decode( c)) {
		/* n < 1 */
		n = 0;

	} else if ( bit_decode( c + 1)) {
		/* n < 1 + 2 */
		n = 1;
		if ( bit_decode( c + 8)) n += 1;

	} else if ( bit_decode( c + 2)) {
		/* n < 1 + 2 + 4 */
		n = 1 + 2;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;

	} else if ( bit_decode( c + 3)) {
		/* n < 1 + 2 + 4 + 8 */
		n = 1 + 2 + 4;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;
		if ( bit_decode( c + 10)) n += 4;

	} else if ( bit_decode( c + 4)) {
		/* n < 1 + 2 + 4 + 8 + 16 */
		n = 1 + 2 + 4 + 8;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;
		if ( bit_decode( c + 10)) n += 4;
		if ( bit_decode( c + 11)) n += 8;

	} else if ( bit_decode( c + 5)) {
		/* n < 1 + 2 + 4 + 8 + 16 + 32 */
		n = 1 + 2 + 4 + 8 + 16;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;
		if ( bit_decode( c + 10)) n += 4;
		if ( bit_decode( c + 11)) n += 8;
		if ( bit_decode( c + 12)) n += 16;

	} else if ( bit_decode( c + 6)) {
		/* n < 1 + 2 + 4 + 8 + 16 + 32 + 64 */
		n = 1 + 2 + 4 + 8 + 16 + 32;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;
		if ( bit_decode( c + 10)) n += 4;
		if ( bit_decode( c + 11)) n += 8;
		if ( bit_decode( c + 12)) n += 16;
		if ( bit_decode( c + 13)) n += 32;

	} else if ( bit_decode( c + 7)) {
		/* n < 1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 */
		n = 1 + 2 + 4 + 8 + 16 + 32 + 64;
		if ( bit_decode( c + 8)) n += 1;
		if ( bit_decode( c + 9)) n += 2;
		if ( bit_decode( c + 10)) n += 4;
		if ( bit_decode( c + 11)) n += 8;
		if ( bit_decode( c + 12)) n += 16;
		if ( bit_decode( c + 13)) n += 32;
		if ( bit_decode( c + 14)) n += 64;

	} else {
		n = 1 + 2 + 4 + 8 + 16 + 32 + 64 + 128;
	}
	return ( n);
}

/*
 * 連鎖をよむ
 */
static void
expand_chain( int x, int y, pix cc)
{

	/* 現地点がどの連鎖上にあったかで確立空間を変える、そのテーブル */
	static const ushort c_tab[] = {
		80 + 6 * 5,	/* -5 */
		80 + 6 * 4,
		80 + 6 * 3,
		80 + 6 * 2,
		80 + 6 * 1,
		80 + 6 * 0,	/* 0  */
		80 + 6 * 0,	/* 1  */
	};
	ushort b = c_tab[ flag_now[ x] + 5];

	if ( ! bit_decode( b++)) {
		if ( bit_decode( b++))      {	/* 下 */
			vram_next[x    ] = cc;
			flag_next[x    ] = -1;
		} else if ( bit_decode( b++)) { /* 左 */

			vram_next[x - 1] = cc;
			flag_next[x - 1] = -2;

		} else if ( bit_decode( b++)) { /* 右 */
			vram_next[x + 1] = cc;
			flag_next[x + 1] = -3;

		} else if ( bit_decode( b++)) { /* 左2 */
			vram_next[x - 2] = cc;
			flag_next[x - 2] = -4;

		} else {			/* 右2 */
			 vram_next[x + 2] = cc;
			flag_next[x + 2] = -5;

		}
	}
}


/*
 * 空間'c'で'bef'に対しての変化分を読む
 * 01234567890123456789012345678901
 *             +                    前の位置が12だと
 *          64201357     7の変位で 16になる
 */
static ushort
getnum15( int c, int bef)
{
	ushort	n;

	n = nn_decode( c);
	if ( bef >= 16) {
		if ( n > (31 - bef) * 2) n = 31 - n;
		else if ( n & 1) n = n / 2 + bef + 1;
		else n = bef - n / 2;
	} else {
		if ( n > bef * 2) n = n;
		else if ( n & 1) n = n / 2 + bef + 1;
		else n = bef - n / 2;
	}
	return ( n);
}

static short
getnum24( int c, int bef)
{
	ushort	n;

	n = nn_decode( c);
	if ( bef >= 128) {
		if ( n > (255 - bef) * 2) n = 255 - n;
		else if ( n & 1) n = n / 2 + bef + 1;
		else n = bef - n / 2;
	} else {
		if ( n > bef * 2) n = n;
		else if ( n & 1) n = n / 2 + bef + 1;
		else n = bef - n / 2;
	}
	return ( n);
}

/*
 * 15bit色の読み込み
 */
pix
read_color15( int x, int y)
{
	pix c1,c2,cc;
	ushort i,j,k,m;
	short r,g,b;
	short r0,g0,b0;

	c1 = vram_prev[x];

	/*    green		     red		  blue */
	k = ((c1 / 128) & 0x1c0) + ((c1 / 32) & 0x38) + ((c1 / 8) & 7);

	if ( bit_decode( cache_hit_c)) {  /* はずれ */
		cache_hit_c = 16;

		c2 = vram_now[ x - 1];
		g = ((c1 & 0xf800) + (c2 & 0xf800)) >> 12;
		r = ((c1 & 0x07c0) + (c2 & 0x07c0)) >>  7;
		b = ((c1 & 0x003e) + (c2 & 0x003e)) >>  2;

		g0 = getnum15( 32, g);
		r = r + g0 - g;
		if ( r > 31) r = 31;
		else if ( r < 0) r = 0;

		b = b + g0 - g;
		if ( b > 31) b = 31;
		else if ( b < 0) b = 0;
		r0 = getnum15( 48, r);
		b0 = getnum15( 64, b);
		cache_pos[k] = j = (cache_pos[k] - 1) & 31;
		cache[k][j] = cc = (g0 << 11) + (r0 << 6) + (b0 << 1);
	} else {
		cache_hit_c = 15;
		j = nn_decode( 17);
		m = cache_pos[k];
		i = (m + j / 2) & 31;
		j = (m + j) & 31;

		cc = cache[k][j];
		cache[k][j] = cache[k][i];
		cache[k][i] = cache[k][m];
		cache[k][m] = cc;
	}
	return ( cc);
}

pix
read_color24( int x, int y)
{
	pix c1,c2,cc;
	ushort i,j,k,m;
	short r, g, b;
	short g0;

	c1 = vram_prev[x];

	/*    green		     red		  blue */
	
	k = ((c1 >> 15) & 0x1c0) + ((c1 >> 10) & 0x38) + ((c1 >> 5) & 7);

	if ( bit_decode( cache_hit_c)) { /* はずれ */
		cache_hit_c = 16;

		c2 = vram_now[ x - 1];
		/* Average the stored G byte (the published loader had a >> 9 typo). */
		g = ((c1 & 0xff0000) + (c2 & 0xff0000)) >> 17;
		g0 = getnum24( 32, g);

		/* Match the predictor used by the reference saver. */
		r = (((c1 & 0x00ff00) + (c2 & 0x00ff00)) >> 9) + g0 - g;
		if ( r > 255) r = 255;
		else if ( r < 0) r = 0;
		r = getnum24( 48, r);

		b = (((c1 & 0x0000ff) + (c2 & 0x0000ff)) >> 1) + g0 - g;;
		if ( b > 255) b = 255;
		else if ( b < 0) b = 0;
		b = getnum24( 64, b);

		cache_pos[k] = j = (cache_pos[k] - 1) & 31;
		cache[k][j] = cc = (g0 << 16) + (r << 8) + b;
	} else {
		cache_hit_c = 15;

		j = nn_decode( 17);
		m = cache_pos[k];
		i = (m + j / 2) & 31;
		j = (m + j) & 31;

		cc = cache[k][j];
		cache[k][j] = cache[k][i];
		cache[k][i] = cache[k][m];
		cache[ k][m] = cc;
	}
	return ( cc);
}

/* 構造体アクセスをケチするため */

static void
para_out()
{
	fbufp = p2->fbufp;
	n_fbuf = p2->n_fbuf;
	bit_buf = p2->bit_buf;
	n_bit_buf = p2->n_bit_buf;
	aa = p2->aa;
	dd = p2->dd;
	cache_hit_c = p2->cache_hit_c;
	read_color = p2->data[0].ptr;

	ynow = p2->ynow;

	vram_prev = p2->vram_prev + 4;
	vram_now = p2->vram_now + 4;
	vram_next = p2->vram_next + 4;
	flag_now = p2->flag_now + 4;
	flag_next = p2->flag_next + 4;
	flag2_now = p2->flag2_now + 4;
	flag2_next = p2->flag2_next + 4;
	flag2_next2 = p2->flag2_next2 + 4;
	cache = p2->cache;
	cache_pos = p2->cache_pos;
	mulu_tab = p2->mulu_tab;
}

static void
para_in()
{
	void *p;
	
	p2->fbufp = fbufp;
	p2->n_fbuf = n_fbuf;
	p2->bit_buf = bit_buf;
	p2->n_bit_buf = n_bit_buf;
	p2->aa = aa;
	p2->dd = dd;
	p2->cache_hit_c = cache_hit_c;
	p2->data[0].ptr = read_color;

	p = p2->vram_prev;
	p2->vram_prev = p2->vram_now;
	p2->vram_now = p2->vram_next;
	p2->vram_next = p;

	p = p2->flag_now;
	p2->flag_now = p2->flag_next;
	p2->flag_next = p;
	
	p = p2->flag2_now;
	p2->flag2_now = p2->flag2_next;
	p2->flag2_next = p2->flag2_next2;
	p2->flag2_next2 = p;

	p2->ynow = ynow;
}

/* 算術版の１ライン展開 */

static int
line_expand( P2 *pp2, pix **line)
{
	int ymax;
	int x,xw;
	pix cc;

	if ( setjmp( jmp_env) != 0) return ( -1);

	p2 = pp2;
	para_out(); /* パラメータの取り出し */
	ymax = mem2short( &p2->blk->y_wid) - 1;

	if ( ynow > ymax) return ( -2); /* おわりだよん */

	xw = mem2short( &p2->blk->x_wid);
	/* 前のラインの右端を今のラインの左端のひとつ外にセット */
	if ( ynow == 0) {
		cc = 0;
	} else cc = vram_prev[ xw - 1];
	vram_now[-1] = cc;

	/* 変化点ようフラグをクリア */
	memset( flag_next, 0, xw * sizeof( flag_next[0]));

	/* 位置の確立空間ようのフラグをクリア */
	memset( flag2_next2, 0, xw * sizeof( flag2_next2[0]));

	for ( x = 0; x < xw; x++) {
		char a = flag_now[x];
		if ( a < 0) {
			cc = vram_now[x];
			if ( ynow < ymax) expand_chain( x, ynow, cc);
		} else if ( bit_decode( flag2_now[x])) {
			/* 変化点の回りの確立空間を調整*/
			flag2_now[ x + 1]++;
			flag2_now[ x + 2]++;
			flag2_next[  x - 1]++;
			flag2_next[  x    ]++;
			flag2_next[  x + 1]++;
			flag2_next2[ x - 1]++;
			flag2_next2[ x    ]++;
			flag2_next2[ x + 1]++;

			vram_now[ x] = cc = read_color( x, ynow);
			if ( ynow < ymax) expand_chain( x, ynow, cc);
		} else {
			vram_now[x] = cc;
		}
	}
	if ( line != NULL) *line = vram_now;
	ynow++;
	para_in();	/* パラメータ戻す */

	return ( ynow - 1);
}

/* 算術版の展開部の初期化 */
int
p2ss_ld_init( P2 *pp2)
{
	ushort *p2b = (ushort *)p2buff;

	int i,xw;

	p2 = pp2;
	p2->ynow = 0; /* 0 ラインからね */

	/* 色数に応じた色読み込み関数セット */
	if ( mem2short( &p2->header->depth) == 24) {
		p2->data[0].ptr = read_color24;
	} else if ( mem2short( &p2->header->depth) == 15) {
		p2->data[0].ptr = read_color15;
	} else {
		p2errno= p2->error_code = P2E_BADFORM;
		return ( -1);
	}
	/* 次ライン展開ようの関数セット */
	p2->nextline = line_expand;

	/* ブロックの頭だし */
	seek_file( p2, p2->blk_pos + sizeof(*p2->blk), SEEK_SET);

	/* キャッシュとフラグ関係のクリア */
	xw = mem2short( &p2->blk->x_wid);
	memset( p2->cache, 0, 8 * 8 * 8 * sizeof( p2->cache[0]));
	memset( p2->cache_pos, 0, 8 * 8 * 8 * sizeof( p2->cache_pos[0]));

	memset( p2->flag_now, 0, xw * sizeof( flag_now[0]));
	memset( p2->flag2_now, 0, 8 + xw * sizeof( flag2_now[0]));
	memset( p2->flag2_next, 0, 8 + xw * sizeof( flag2_next[0]));

	/* 確立テーブルの読み込み */
	for ( i = 0; i < N_CONTEXT; i++) {
		p2b[i] = read_short( p2);
	}
	/* 掛け算テーブルの作成 */
	for ( i = 0; i < 16384; i++) {
		p2->mulu_tab[i] = (long)(i / 128 + 128) * p2b[i & 127] / 256;
		if ( p2->mulu_tab[i] == 0) p2->mulu_tab[i] = 1;
	}
	/* 各種変数の初期化 */
	n_bit_buf = 0;
	n_fbuf = 0;
	bit_buf = 0;
	p2->aa = 0xffffu;
	p2->dd = 0;
	for ( i = 0; i < 16; i++) {
		p2->dd *= 2;
		if ( load_bit()) p2->dd |= 1;
	}
	p2->fbufp = fbufp;
	p2->bit_buf = bit_buf;
	p2->n_bit_buf = n_bit_buf;
	p2->n_fbuf = n_fbuf;
	p2->cache_hit_c = 16;
	return ( 0);
}


/* ベタ圧縮の１ライン展開 */
static int
beta_line_expand( P2 *pp2, pix **line)
{
	int i,xw,ymax;
	uchar a,b,c,*p;
	pix *pc;

	p2 = pp2;
	ymax = mem2short( &p2->blk->y_wid) - 1;
	if ( p2->ynow > ymax) return ( -2); /* 終わり */

	xw = mem2short( &p2->blk->x_wid);

	p2errno = 0;
	if ( raw_beta == 0) { /* 他圧縮と同じpix並び */
		if ( mem2short( &p2->header->depth) == 24) {
			if ( read_file( p2, p2->vram_prev, xw * 3) != xw * 3) return ( -1);
			pc = p2->vram_now;
			p = (uchar *)p2->vram_prev;
			for ( i = 0; i < xw; i++) {
				a = *p++;	/* G */
				b = *p++;	/* R */
				c = *p++;	/* B */
				*pc++ = (a << 16) + (b << 8) + c;
			}
		} else if ( mem2short( &p2->header->depth) == 15) {
			if ( read_file( p2, p2->vram_prev, xw * 2) != xw * 2) return ( -1);
			pc = p2->vram_now;
			p = (uchar *)p2->vram_prev;
			if ( memcmp( p2->blk->id, "P2BM", 4) == 0) {
				for ( i = 0; i < xw; i++) {
					a = *p++;
					b = *p++;
					*pc++ = (a << 8) + b;
				}
			} else {
				for ( i = 0; i < xw; i++) {
					a = *p++;
					b = *p++;
					*pc++ = (b << 8) + a;
				}
			}
		}
		*line = p2->vram_now;
	} else { /* ベタ形式そのまま入れる */
		if ( mem2short( &p2->header->depth) == 24) {
			if ( read_file( p2, p2->vram_now, xw * 3) != xw * 3) return ( -1);
		} else if ( mem2short( &p2->header->depth) == 15) {
			if ( read_file( p2, p2->vram_now, xw * 2) != xw * 2 ) return ( -1);
		}
		*line = p2->vram_now;
	}
	pc = p2->vram_prev;
	p2->vram_prev = p2->vram_now;
	p2->vram_now = p2->vram_next;
	p2->vram_next = pc;

	p2->ynow++;
	return ( p2->ynow - 1);
}

int
p2b_ld_init( P2 *p2)
{
	p2->ynow = 0;
	p2->nextline = beta_line_expand;
	seek_file( p2, p2->blk_pos + sizeof(*p2->blk), SEEK_SET);
	return ( 0);
}


/* eof */
