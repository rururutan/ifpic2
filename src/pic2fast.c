/**
 * @file pic2fast.c
 * @brief PIC2 fast-compression block decoder from the published reference loader.
 *
 * The original decoding algorithm is retained to preserve format accuracy;
 * platform I/O is supplied by pic2decode.c.
 */
/*
 *	PIC2 高速フォーマットの展開 by やなぎさわ
 */
static char rcsid[] = "($Id: p2loadhi.c_ 1.8 1993/11/27 17:53:45 akira Exp akira $)";

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>

#define read_color p2fast_read_color
#define read_color15 p2fast_read_color15
#define read_color24 p2fast_read_color24
#include "pic2legacy.h"

static uchar *fbufp;
static int n_fbuf;
static int bit_buf;
static int n_bit_buf;
static long len;
static long len2;
static P2 *p2;
static pix *vram_prev;
static pix *vram_now;
static pix *vram_next;
static pix (*cache)[64];
static ushort *cache_pos;
static short ynow;

pix (*read_color)( pix bc);

/* エラー時の脱出用 */
static jmp_buf jmp_env;

/*
 * 'size'bit ファイルから読み込む
 */
static ulong
bit_load(int size)
{
	ulong	a = 0;
	
	bit_buf &= 0xff;
	while ( size > n_bit_buf) {
		a = a << n_bit_buf;
		bit_buf = bit_buf << n_bit_buf;
		a = a + ( bit_buf >> 8);
		size -= n_bit_buf;

		if ( n_fbuf == 0) {
			fbufp = p2->fbuf;
			n_fbuf = read_file2( p2, fbufp, N_FBUF);
			if ( n_fbuf == 0) {
				p2errno = p2->error_code = P2E_FEOF;
				longjmp( jmp_env, 1);
			}
		}
		--n_fbuf;
		bit_buf = *fbufp++;
		n_bit_buf = 8;
	}
	a = a << size;
	bit_buf = bit_buf << size;
	a = a + ( bit_buf >> 8);
	n_bit_buf -= size;
	return ( a);
}

/*
 * 長さを読み込む
 */
static long
read_len( void)
{
	int a;
	
	a = 0;
	while (bit_load( 1)) {
		a++;
	}
	if ( a == 0) return ( 0);
	return ( bit_load( a) + (1 << a) - 1);
}

/*
 * 連鎖を展開
 */
static void
expand_chain( int x, pix cc)
{
	if ( bit_load(1) != 0) {
		if ( bit_load(1) != 0) { /* 下 */
			vram_next[ x] = cc | 0x80000000u;

		} else if ( bit_load(1) != 0) {
			if ( bit_load(1) == 0) { /* 左2下 */
				vram_next[ x - 2] = cc | 0x80000000u;

			} else {	/* 左1下 */
				vram_next[ x - 1] = cc | 0x80000000u;

			}
		} else {
			if ( bit_load(1) == 0) { /* 右2下 */
				vram_next[ x + 2] = cc | 0x80000000u;

			} else {		/* 左1下 */
				vram_next[ x + 1] = cc | 0x80000000u;

			}
		}
	}
}

/*
 * 24bit色のよみこみ
 */
static pix
read_color24( pix bc)
{
	pix	cc;
	int	j,k,m;

	k = bc >> 16;

	if ( bit_load( 1) == 0) {
		cache_pos[k] = m = (cache_pos[k] - 1) & (N_CACHE - 1);
		cc = cache[k][m] = bit_load( 24);
	} else {
		j = bit_load( 6);	/* 6= log2(n_cache) */
		m = cache_pos[k];
		cc = cache[k][(m + j) & (N_CACHE - 1)];
	}
	return ( cc);
}

/*
 * 15bit色のよみこみ
 */
static pix
read_color15( pix bc)
{
	pix	cc;
	int	j,k,m;

	k = bc >> 8;
	if ( bit_load( 1) == 0) {
		cache_pos[k] = m = (cache_pos[k] - 1) & (N_CACHE - 1);
		cc = cache[k][m] = bit_load( 15) * 2;
	} else {
		j = bit_load( 6);
		m = cache_pos[k];
		cc = cache[k][(m + j) & (N_CACHE -1 )];
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
	len = p2->aa;
	len2 = p2->dd;
	read_color = p2->data[0].ptr;
	ynow = p2->ynow;
	vram_prev = p2->vram_prev + 4;
	vram_now = p2->vram_now + 4;
	vram_next = p2->vram_next + 4;
	cache = (pix (*)[64])p2->cache;
	cache_pos = p2->cache_pos;
}

static void
para_in()
{
	void *p;
	
	p2->fbufp = fbufp;
	p2->n_fbuf = n_fbuf;
	p2->bit_buf = bit_buf;
	p2->n_bit_buf = n_bit_buf;
	p2->aa = len;
	p2->dd = len2;
	p2->data[0].ptr = read_color;

	p = p2->vram_prev;
	p2->vram_prev = p2->vram_now;
	p2->vram_now = p2->vram_next;
	p2->vram_next = p;

	p2->ynow = ynow;
}

/*
 * 行単位の展開
 */
static int
line_expand( P2 *pp2, pix **line)
{
	int ymax;
	int x,xw;
	pix cc;

	if ( setjmp( jmp_env) != 0) return ( -1);

	p2 = pp2;
	para_out();
	ymax = mem2short( &p2->blk->y_wid) - 1;

	if ( ynow > ymax) return ( -2);

	xw = mem2short( &p2->blk->x_wid);

	if ( ynow == 0) {
		len2 = 0;
		len = read_len();
		if ( len == 1023) len2 = 1023;
		else if ( len > 1023) {
			len--;
		}
		cc = 0;
	} else cc = vram_prev[ xw - 1];

	for ( x = 0; x < xw; x++) {
		char a = (char)(vram_now[x] / 0x1000000u);

		if ( len2 > 0) {
			if ( a != 0) { /* on chain ? */
				cc = vram_now[x] & 0xffffffu;
				expand_chain( x, cc);
				if ( --len2 == 0) {
					len = read_len();
					if ( len == 1023) len2 = 1023;
					else if ( len > 1023) {
						len--;
					}
				}
			}
		} else {
			if ( a != 0) { /* on chain ? */
				cc = vram_now[x] & 0xffffffu;
				expand_chain( x, cc);
			} else if ( --len < 0) {
				cc = vram_now[x] = read_color( cc);
				expand_chain( x, cc);
				len = read_len();
				if ( len == 1023) len2 = 1023;
				else if ( len > 1023) {
					len--;
				}
			}	
		}
		vram_now[x] = cc;
	}
	if ( line != NULL) *line = vram_now;
	ynow++;
	para_in();

	return ( ynow - 1);
}

/*
 * 高速フォーマット展開の初期化
 */
int
p2sf_ld_init( P2 *p2)
{
	int xw;

	p2->ynow = 0;
	if ( mem2short( &p2->header->depth) == 24) {
		p2->data[0].ptr = read_color24;
	} else if ( mem2short( &p2->header->depth) == 15) {
		p2->data[0].ptr = read_color15;
	} else {
		p2errno= p2->error_code = P2E_BADFORM;
		return ( -1);
	}
	p2->nextline = line_expand;
	seek_file( p2, p2->blk_pos + sizeof(*p2->blk), SEEK_SET);
	xw = mem2short( &p2->blk->x_wid);

	memset( p2->cache, 0, sizeof( cache[0]) * 256);
	memset( p2->cache_pos, 0, sizeof( cache_pos[0]) * 256);
	memset( p2->vram_now, 0, (xw + 8) * sizeof( pix));
	memset( p2->vram_next, 0, (xw + 8) * sizeof( pix));
	memset( p2->vram_prev, 0, (xw + 8) * sizeof( pix));

	p2->n_bit_buf = 0;
	p2->n_fbuf = 0;
	p2->bit_buf = 0;

	return ( 0);
}

/* eof */
