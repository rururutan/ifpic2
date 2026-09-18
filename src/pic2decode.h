/**
 * @file pic2decode.h
 * @brief Public, Susie-independent PIC2 decoder interface.
 */
#ifndef PIC2DECODE_H
#define PIC2DECODE_H

#include <stddef.h>
#include <stdint.h>

/** A decoded top-down 24-bit BGR image. */
typedef struct Pic2Image {
    uint8_t *pixels;
    uint32_t width;
    uint32_t height;
    uint16_t x_density;
    uint16_t y_density;
} Pic2Image;

/**
 * Decode a complete PIC2 stream.
 *
 * @param data Complete file contents.
 * @param size Size of @p data in bytes.
 * @param image Receives the decoded image on success.
 * @return 0 on success, 1 for malformed/unsupported data, or 3 on allocation failure.
 */
int Pic2Decode(const uint8_t *data, size_t size, Pic2Image *image);

/**
 * Return the free-form Shift-JIS comment stored after the PIC2 header.
 *
 * @param data Complete file contents.
 * @param size Size of @p data in bytes.
 * @param comment Receives a pointer into @p data.
 * @param length Receives the byte length of the comment.
 * @return 0 on success or 1 for an invalid header.
 */
int Pic2GetComment(const uint8_t *data, size_t size,
                   const uint8_t **comment, size_t *length);

/** Release memory owned by a decoded image. */
void Pic2Free(Pic2Image *image);

#endif
