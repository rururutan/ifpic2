/**
 * @file ifpic2.c
 * @brief Susie I/F adapter for multi-platform PIC2 images.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "spibase.h"
#include "pic2decode.h"

#define IFPIC2_VERSION "0.10"

const int NumInfo = 4;
const LPCSTR PluginInfo[] = {
    "00IN", "PIC2 to DIB filter ver." IFPIC2_VERSION " (C) Ru^3",
    "*.p2", "PIC2 image"
};

/* The published PIC2 loader uses process-global arithmetic state. */
static SRWLOCK decode_lock = SRWLOCK_INIT;

static int decode_picture(const uint8_t *data, size_t size, Pic2Image *image)
{
    int result;
    AcquireSRWLockExclusive(&decode_lock);
    result = Pic2Decode(data, size, image);
    ReleaseSRWLockExclusive(&decode_lock);
    return result;
}

/** Read a Susie input stream into an automatically growing byte buffer. */
static int read_entire_file(SPI_FILE *file, uint8_t **data, size_t *size)
{
    uint8_t *buffer = NULL;
    size_t used = 0, capacity = 0;
    for (;;) {
        DWORD read_size;
        if (used == capacity) {
            size_t new_capacity = capacity == 0 ? 65536u : capacity * 2u;
            uint8_t *new_buffer;
            if (new_capacity <= capacity || new_capacity > 0x7fffffffU) {
                free(buffer);
                return SPI_ERROR_ALLOCATE_MEMORY;
            }
            new_buffer = (uint8_t *)realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return SPI_ERROR_ALLOCATE_MEMORY;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }
        read_size = SpiRead(buffer + used, (DWORD)(capacity - used), file);
        used += read_size;
        if (read_size == 0 || used < capacity)
            break;
    }
    if (used == 0) {
        free(buffer);
        return SPI_ERROR_FILE_READ;
    }
    *data = buffer;
    *size = used;
    return 0;
}

/** Identify PIC2 by its mandatory four-byte signature. */
int IsSupportedFormat(LPBYTE data, DWORD size, LPCSTR filename)
{
    (void)filename;
    return (size >= 4 && memcmp(data, "P2DT", 4) == 0) ||
           (size >= 132 && memcmp(data + 128, "P2DT", 4) == 0);
}

/** Copy the PIC2 Shift-JIS comment to a Susie-owned local-memory block. */
static int set_picture_comment(const uint8_t *data, size_t size,
                               PictureInfo *info, int utf8)
{
    const uint8_t *comment;
    size_t comment_length;
    LPBYTE output;
    size_t output_length;

    if (Pic2GetComment(data, size, &comment, &comment_length) != 0)
        return SPI_ERROR_BROKEN_DATA;
    if (comment_length == 0)
        return SPI_ERROR_SUCCESS;
    if (!utf8) {
        if (comment_length >= UINT_MAX)
            return SPI_ERROR_ALLOCATE_MEMORY;
        if (SpiAllocBuffer(&info->hInfo, &output,
                           (UINT)comment_length + 1u) != SPI_ERROR_SUCCESS)
            return SPI_ERROR_ALLOCATE_MEMORY;
        memcpy(output, comment, comment_length);
        output[comment_length] = 0;
        SpiUnlockBuffer(&info->hInfo);
        return SPI_ERROR_SUCCESS;
    }
    {
        WCHAR *wide;
        int wide_length;
        int utf8_length;
        if (comment_length > INT_MAX)
            return SPI_ERROR_ALLOCATE_MEMORY;
        wide_length = MultiByteToWideChar(932, 0, (LPCCH)comment,
                                          (int)comment_length, NULL, 0);
        if (wide_length <= 0)
            return SPI_ERROR_BROKEN_DATA;
        wide = (WCHAR *)malloc((size_t)wide_length * sizeof(*wide));
        if (wide == NULL)
            return SPI_ERROR_ALLOCATE_MEMORY;
        if (MultiByteToWideChar(932, 0, (LPCCH)comment, (int)comment_length,
                                wide, wide_length) != wide_length) {
            free(wide);
            return SPI_ERROR_BROKEN_DATA;
        }
        utf8_length = WideCharToMultiByte(CP_UTF8, 0, wide, wide_length,
                                          NULL, 0, NULL, NULL);
        output_length = 3u + (size_t)utf8_length + 1u;
        if (utf8_length <= 0 || output_length > UINT_MAX ||
            SpiAllocBuffer(&info->hInfo, &output,
                           (UINT)output_length) != SPI_ERROR_SUCCESS) {
            free(wide);
            return SPI_ERROR_ALLOCATE_MEMORY;
        }
        output[0] = 0xef;
        output[1] = 0xbb;
        output[2] = 0xbf;
        if (WideCharToMultiByte(CP_UTF8, 0, wide, wide_length,
                                (LPSTR)output + 3, utf8_length,
                                NULL, NULL) != utf8_length) {
            free(wide);
            SpiUnlockBuffer(&info->hInfo);
            SpiFreeBuffer(&info->hInfo);
            return SPI_ERROR_BROKEN_DATA;
        }
        output[3 + utf8_length] = 0;
        free(wide);
        SpiUnlockBuffer(&info->hInfo);
    }
    return SPI_ERROR_SUCCESS;
}

/** Decode PIC2 metadata and return its comment in the requested encoding. */
static int get_image_info(SPI_FILE *file, PictureInfo *info, int utf8)
{
    uint8_t *data;
    size_t size;
    Pic2Image image;
    int result = read_entire_file(file, &data, &size);
    if (result != 0) return result;
    result = decode_picture(data, size, &image);
    if (result != 0) {
        free(data);
        return result == 3 ? SPI_ERROR_ALLOCATE_MEMORY : SPI_ERROR_BROKEN_DATA;
    }
    SpiSetPictureInfo(info, image.width, image.height, 24,
                      image.x_density, image.y_density, 0, 0, NULL);
    result = set_picture_comment(data, size, info, utf8);
    free(data);
    Pic2Free(&image);
    return result;
}

/** Decode PIC2 metadata and return the original Shift-JIS comment. */
int GetImageInfo(SPI_FILE *file, PictureInfo *info)
{
    return get_image_info(file, info, 0);
}

/** Decode PIC2 metadata and return a UTF-8 comment prefixed by a BOM. */
int GetImageInfoW(SPI_FILE *file, PictureInfo *info)
{
    return get_image_info(file, info, 1);
}

/** Decode PIC pixels and construct a bottom-up 24bpp DIB. */
int GetImage(SPI_FILE *file, HANDLE *bitmap_info_handle, HANDLE *bitmap_handle,
             SPIPROC progress_callback, LONG_PTR callback_data)
{
    uint8_t *data;
    size_t size;
    Pic2Image image;
    LPBITMAPINFO bitmap_info;
    LPBYTE bitmap_bits;
    DWORD bitmap_stride;
    uint32_t y;
    int result = read_entire_file(file, &data, &size);
    if (result != 0) return result;
    result = decode_picture(data, size, &image);
    free(data);
    if (result != 0) return result == 3 ? SPI_ERROR_ALLOCATE_MEMORY : SPI_ERROR_BROKEN_DATA;
    result = SpiInitBitmap((HLOCAL *)bitmap_info_handle, &bitmap_info,
                           (HLOCAL *)bitmap_handle, &bitmap_bits, &bitmap_stride,
                           image.width, image.height, 24, 0,
                           image.x_density, image.y_density);
    if (result != 0) {
        Pic2Free(&image);
        return result;
    }
    for (y = 0; y < image.height; ++y) {
        size_t row_size = (size_t)image.width * 3u;
        memcpy(bitmap_bits + (size_t)(image.height - 1u - y) * bitmap_stride,
               image.pixels + (size_t)y * row_size, row_size);
    }
    Pic2Free(&image);
    SpiUnlockBuffer(bitmap_info_handle);
    SpiUnlockBuffer(bitmap_handle);
    if (progress_callback != NULL)
        progress_callback(100, 100, callback_data);
    return 0;
}
