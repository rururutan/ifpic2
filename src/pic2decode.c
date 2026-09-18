/**
 * @file pic2decode.c
 * @brief Memory-backed PIC2 parser and image compositor.
 *
 * The entropy and prediction routines are maintained in pic2arith.c and
 * pic2fast.c, derived from the format author's published reference loader.
 */
#include "pic2decode.h"
#include "pic2legacy.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum { PIC2_INVALID = 1, PIC2_NOMEM = 3 };

uchar p2buff[512];
int p2errno;
char raw_beta;

static int locate_data_fork(const uint8_t **data, size_t *size)
{
    if (*size >= 4 && memcmp(*data, "P2DT", 4) == 0) return 1;
    if (*size >= 132 && memcmp(*data + 128, "P2DT", 4) == 0) {
        *data += 128;
        *size -= 128;
        return 1;
    }
    return 0;
}

long read_file2(P2 *p2, void *memory, size_t size)
{
    size_t available;
    if (p2 == NULL || memory == NULL || p2->memory_position > p2->memory_size)
        return 0;
    available = p2->memory_size - p2->memory_position;
    if (size > available) size = available;
    memcpy(memory, p2->memory + p2->memory_position, size);
    p2->memory_position += size;
    return (long)size;
}

long read_file(P2 *p2, void *memory, size_t size)
{
    long result = read_file2(p2, memory, size);
    if ((size_t)result != size) p2->error_code = p2errno = P2E_FEOF;
    return result;
}

long seek_file(P2 *p2, long position, int origin)
{
    int64_t target;
    if (origin == SEEK_SET) target = position;
    else if (origin == SEEK_CUR) target = (int64_t)p2->memory_position + position;
    else if (origin == SEEK_END) target = (int64_t)p2->memory_size + position;
    else return -1;
    if (target < 0 || (uint64_t)target > p2->memory_size) {
        p2->error_code = p2errno = P2E_FEOF;
        return -1;
    }
    p2->memory_position = (size_t)target;
    return (long)target;
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int read_short(P2 *p2)
{
    uint8_t bytes[2];
    if (read_file(p2, bytes, sizeof(bytes)) != sizeof(bytes)) return -1;
    return (int)be16(bytes);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void free_context(P2 *p2)
{
    free(p2->vram_prev);
    free(p2->vram_now);
    free(p2->vram_next);
    free(p2->flag_now);
    free(p2->flag_next);
    free(p2->flag2_now);
    free(p2->flag2_next);
    free(p2->flag2_next2);
    free(p2->cache);
    free(p2->cache_pos);
    free(p2->mulu_tab);
}

static int allocate_context(P2 *p2, uint32_t width)
{
    size_t padded = (size_t)width + 8u;
    p2->vram_prev = (pix *)calloc(padded, sizeof(pix));
    p2->vram_now = (pix *)calloc(padded, sizeof(pix));
    p2->vram_next = (pix *)calloc(padded, sizeof(pix));
    p2->flag_now = (schar *)calloc(padded, 1);
    p2->flag_next = (schar *)calloc(padded, 1);
    p2->flag2_now = (schar *)calloc(padded, 1);
    p2->flag2_next = (schar *)calloc(padded, 1);
    p2->flag2_next2 = (schar *)calloc(padded, 1);
    p2->cache = (pix (*)[N_COLOR_CACHE + 1])calloc(512u * 64u, sizeof(pix));
    p2->cache_pos = (ushort *)calloc(512u, sizeof(ushort));
    p2->mulu_tab = (ushort *)calloc(16384u, sizeof(ushort));
    if (p2->vram_prev == NULL || p2->vram_now == NULL ||
        p2->vram_next == NULL || p2->flag_now == NULL ||
        p2->flag_next == NULL || p2->flag2_now == NULL ||
        p2->flag2_next == NULL || p2->flag2_next2 == NULL ||
        p2->cache == NULL || p2->cache_pos == NULL || p2->mulu_tab == NULL) {
        free_context(p2);
        return 0;
    }
    return 1;
}

static void put_pixel(uint8_t *destination, uint32_t depth, pix color)
{
    if (depth == 15) {
        destination[0] = (uint8_t)((color & 0x003eu) << 2);
        destination[1] = (uint8_t)((color & 0xf800u) >> 8);
        destination[2] = (uint8_t)((color & 0x07c0u) >> 3);
    } else {
        destination[0] = (uint8_t)color;
        destination[1] = (uint8_t)(color >> 16);
        destination[2] = (uint8_t)(color >> 8);
    }
}

static int decode_block(const uint8_t *data, size_t size,
                        struct p2_hdr *header, size_t block_position,
                        uint8_t *pixels, uint32_t canvas_width,
                        uint32_t canvas_height)
{
    P2 p2;
    struct p2_blk block;
    uint32_t depth = be16(header->depth);
    uint32_t width, height;
    int32_t x_offset, y_offset;
    uint32_t block_size, y;
    uint32_t flags, opaque;
    int result;

    if (size - block_position < sizeof(block)) return PIC2_INVALID;
    memcpy(&block, data + block_position, sizeof(block));
    block_size = be32(block.size);
    width = be16(block.x_wid);
    height = be16(block.y_wid);
    x_offset = (int16_t)be16(block.x_offset);
    y_offset = (int16_t)be16(block.y_offset);
    flags = be16(block.flag);
    opaque = be32(block.opaque);
    if (block_size < sizeof(block) || block_size > size - block_position ||
        width == 0 || height == 0) return PIC2_INVALID;

    memset(&p2, 0, sizeof(p2));
    p2.header = header;
    p2.blk = &block;
    p2.blk_pos = (long)block_position;
    p2.memory = data;
    p2.memory_size = block_position + block_size;
    if (!allocate_context(&p2, width)) return PIC2_NOMEM;

    raw_beta = 0;
    if (memcmp(block.id, "P2SS", 4) == 0) result = p2ss_ld_init(&p2);
    else if (memcmp(block.id, "P2SF", 4) == 0) result = p2sf_ld_init(&p2);
    else if (memcmp(block.id, "P2BM", 4) == 0) result = p2b_ld_init(&p2);
    else result = -1;
    if (result != 0) {
        free_context(&p2);
        return PIC2_INVALID;
    }

    for (y = 0; y < height; ++y) {
        pix *line = NULL;
        uint32_t x;
        if (p2.nextline(&p2, &line) < 0 || line == NULL) {
            free_context(&p2);
            return PIC2_INVALID;
        }
        for (x = 0; x < width; ++x) {
            int64_t dx = (int64_t)x_offset + x;
            int64_t dy = (int64_t)y_offset + y;
            if ((flags & 1u) != 0 && line[x] == opaque) continue;
            if (dx >= 0 && dy >= 0 && dx < canvas_width && dy < canvas_height)
                put_pixel(pixels + ((size_t)dy * canvas_width + (size_t)dx) * 3u,
                          depth, line[x]);
        }
    }
    free_context(&p2);
    return 0;
}

int Pic2GetComment(const uint8_t *data, size_t size,
                   const uint8_t **comment, size_t *length)
{
    size_t position = sizeof(struct p2_hdr);
    uint32_t header_size;
    if (data == NULL || comment == NULL || length == NULL ||
        !locate_data_fork(&data, &size) || size < sizeof(struct p2_hdr))
        return PIC2_INVALID;
    header_size = be32(data + 106);
    if (header_size < position || header_size > size) return PIC2_INVALID;
    if ((be16(data + 98) & 1u) != 0) {
        uint32_t colors;
        if (header_size - position < 3) return PIC2_INVALID;
        colors = be16(data + position + 1);
        if (colors > 256 || (size_t)colors * 3u > header_size - position - 3u)
            return PIC2_INVALID;
        position += 3u + (size_t)colors * 3u;
    }
    *comment = data + position;
    *length = header_size - position;
    while (*length != 0 && (*comment)[*length - 1u] == 0) --*length;
    return 0;
}

int Pic2Decode(const uint8_t *data, size_t size, Pic2Image *image)
{
    struct p2_hdr header;
    uint32_t header_size, width, height, depth;
    size_t position, pixel_count;
    int decoded = 0;
    if (data == NULL || image == NULL || !locate_data_fork(&data, &size) ||
        size < sizeof(header)) return PIC2_INVALID;
    memset(image, 0, sizeof(*image));
    memcpy(&header, data, sizeof(header));
    header_size = be32(header.size);
    depth = be16(header.depth);
    width = be16(header.x_max);
    height = be16(header.y_max);
    if (header_size < sizeof(header) || header_size > size ||
        (depth != 15 && depth != 24) || width == 0 || height == 0 ||
        width > SIZE_MAX / height ||
        (pixel_count = (size_t)width * height) > SIZE_MAX / 3u)
        return PIC2_INVALID;
    image->pixels = (uint8_t *)calloc(pixel_count, 3u);
    if (image->pixels == NULL) return PIC2_NOMEM;

    position = header_size;
    while (size - position >= 8u && data[position] != 0) {
        uint32_t block_size = be32(data + position + 4u);
        int result;
        if (block_size < sizeof(struct p2_blk) || block_size > size - position)
            goto broken;
        if (memcmp(data + position, "P2SS", 4) == 0 ||
            memcmp(data + position, "P2SF", 4) == 0 ||
            memcmp(data + position, "P2BM", 4) == 0) {
            result = decode_block(data, size, &header, position, image->pixels,
                                  width, height);
            if (result != 0) {
                if (result == PIC2_NOMEM) {
                    free(image->pixels);
                    image->pixels = NULL;
                    return result;
                }
                goto broken;
            }
            decoded = 1;
            /* Susie exposes the first picture block as the file image. */
            break;
        }
        position += block_size;
    }
    if (!decoded) goto broken;
    image->width = width;
    image->height = height;
    {
        uint32_t x_aspect = be16(header.x_aspect);
        uint32_t y_aspect = be16(header.y_aspect);
        uint32_t maximum = x_aspect > y_aspect ? x_aspect : y_aspect;
        if (x_aspect != 0 && y_aspect != 0) {
            image->x_density = (uint16_t)(128u * y_aspect / maximum);
            image->y_density = (uint16_t)(128u * x_aspect / maximum);
        }
    }
    return 0;

broken:
    free(image->pixels);
    image->pixels = NULL;
    return PIC2_INVALID;
}

void Pic2Free(Pic2Image *image)
{
    if (image != NULL) {
        free(image->pixels);
        image->pixels = NULL;
    }
}
