/**
 * @file pic2legacy.h
 * @brief Compatibility declarations for the original PIC2 load algorithms.
 */
#ifndef PIC2LEGACY_H
#define PIC2LEGACY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef uint8_t uchar;
typedef int8_t schar;
typedef uint16_t ushort;
typedef uint32_t ulong;
typedef uint8_t SHORT[2];
typedef uint8_t LONG[4];
typedef uint32_t pix;

#define mem2short(addr) ((int16_t)(((uint16_t)((uchar *)(addr))[0] << 8) | \
                                  ((uchar *)(addr))[1]))
#define mem2long(addr) ((int32_t)(((uint32_t)((uchar *)(addr))[0] << 24) | \
                                 ((uint32_t)((uchar *)(addr))[1] << 16) | \
                                 ((uint32_t)((uchar *)(addr))[2] << 8) | \
                                 ((uchar *)(addr))[3]))

enum { N_FBUF = 8192, N_COLOR_CACHE = 31, N_CONTEXT = 128,
       N_CACHE = 64, P2E_FEOF = 257, P2E_BADFORM = 258 };

#pragma pack(push, 1)
struct p2_hdr {
    uchar magic[4];
    uchar name[18];
    uchar subtitle[8];
    uchar crlf0[2];
    uchar title[30];
    uchar crlf1[2];
    uchar saver[30];
    uchar crlf2[2];
    uchar eof;
    uchar reserve0;
    SHORT flag;
    SHORT no;
    LONG time;
    LONG size;
    SHORT depth;
    SHORT x_aspect;
    SHORT y_aspect;
    SHORT x_max;
    SHORT y_max;
    LONG reserve1;
};

struct p2_blk {
    uchar id[4];
    LONG size;
    SHORT flag;
    SHORT x_wid;
    SHORT y_wid;
    SHORT x_offset;
    SHORT y_offset;
    LONG opaque;
    LONG reserve;
};
#pragma pack(pop)

typedef struct _P2 {
    struct p2_hdr *header;
    struct p2_blk *blk;
    int n_pal;
    int pal_bits;
    uchar pal[256][3];
    uchar *comment;
    int fds;
    uchar mode;
    uchar macbin;
    int error_code;
    long next_pos;
    long blk_pos;
    short x_max;
    short y_max;
    int ynow;
    uchar *buff;
    long n_buff;
    pix *vram_prev;
    pix *vram_now;
    pix *vram_next;
    schar *flag_now;
    schar *flag_next;
    schar *flag2_now;
    schar *flag2_next;
    schar *flag2_next2;
    pix (*cache)[N_COLOR_CACHE + 1];
    ushort *cache_pos;
    ushort *mulu_tab;
    uchar fbuf[N_FBUF];
    uchar *fbufp;
    long n_fbuf;
    ushort bit_buf;
    short n_bit_buf;
    long aa;
    long cc;
    long dd;
    char cache_hit_c;
    int (*nextline)(struct _P2 *, pix **);
    union { int int_t; void *ptr; } data[4];
    const uchar *memory;
    size_t memory_size;
    size_t memory_position;
} P2;

extern uchar p2buff[512];
extern int p2errno;
extern char raw_beta;

long read_file(P2 *p2, void *memory, size_t size);
long read_file2(P2 *p2, void *memory, size_t size);
long seek_file(P2 *p2, long position, int origin);
int read_short(P2 *p2);
int p2ss_ld_init(P2 *p2);
int p2sf_ld_init(P2 *p2);
int p2b_ld_init(P2 *p2);

#endif
