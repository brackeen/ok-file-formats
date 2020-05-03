/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2020 David Brackeen

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef OK_JPG_H
#define OK_JPG_H

/**
 * @file
 * Functions to read JPEG files.
 *
 * This JPEG decoder:
 * - Reads most JPEG files (baseline and progressive).
 * - Interprets EXIF orientation tags.
 * - Option to get the image dimensions without decoding.
 * - Option to flip the image vertically.
 * - Returns data in RGBA or BGRA format.
 *
 * Caveats:
 * - No CMYK or YCCK support.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_jpg.h"
 *
 *     int main() {
 *         FILE *file = fopen("my_image.jpg", "rb");
 *         ok_jpg image = ok_jpg_read(file, OK_JPG_COLOR_FORMAT_RGBA);
 *         fclose(file);
 *         if (image.data) {
 *             printf("Got image! Size: %li x %li\n", (long)image.width, (long)image.height);
 *             free(image.data);
 *         }
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>
#ifndef OK_NO_STDIO
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OK_JPG_SUCCESS = 0,
    OK_JPG_ERROR_API, // Invalid argument sent to public API function
    OK_JPG_ERROR_INVALID, // Not a valid JPG file
    OK_JPG_ERROR_UNSUPPORTED, // Unsupported JPG file (CMYK)
    OK_JPG_ERROR_ALLOCATION, // Couldn't allocate memory
    OK_JPG_ERROR_IO, // Couldn't read or seek the file
} ok_jpg_error;

/**
 * The data returned from #ok_jpg_read() .
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t bpp; // Always 4
    ok_jpg_error error_code:24;
    uint8_t *data;
} ok_jpg;

/**
 * JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 */
typedef enum {
    /// Set for RGBA color format. The alpha is always 255.
    OK_JPG_COLOR_FORMAT_RGBA = (0 << 0),
    /// Set for BGRA color format. The alpha is always 255.
    OK_JPG_COLOR_FORMAT_BGRA = (1 << 0),
    /// Set to flip the image data along the horizontal axis, so that the first row of data is
    /// the last row in the image.
    OK_JPG_FLIP_Y = (1 << 2),
    /// Set to read an image's dimensions and color format without reading the image data.
    OK_JPG_INFO_ONLY = (1 << 3)

} ok_jpg_decode_flags;

// MARK: Reading from a FILE

#if !defined(OK_NO_STDIO) && !defined(OK_NO_DEFAULT_ALLOCATOR)

/**
 * Reads a JPEG image using the default "stdlib" allocator.
 * On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller (using stdlib's `free()`).
 *
 * @param file The file to read.
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a #ok_jpg object.
 */
ok_jpg ok_jpg_read(FILE *file, ok_jpg_decode_flags decode_flags);

#endif

// MARK: Reading from a FILE, using a custom allocator

typedef struct {
    /**
     * Allocates uninitilized memory.
     *
     * @param user_data The pointer passed to #ok_jpg_read_with_allocator.
     * @param size The size of the memory to allocate.
     * @return the pointer to the newly allocated memory, or `NULL` if the memory could not be allocated.
     */
    void *(*alloc)(void *user_data, size_t size);
    
    /**
     * Frees memory previously allocated with `alloc`.
     *
     * @param user_data The pointer passed to #ok_jpg_read_with_allocator.
     * @param memory The memory to free.
     */
    void (*free)(void *user_data, void *memory);
    
    /**
     * Allocates memory for the decoded image.
     * This function may be `NULL`, in which case `alloc` is used instead.
     *
     * @param user_data The pointer passed to #ok_jpg_read_with_allocator.
     * @param width The image's width, in pixels.
     * @param height The image's height, in pixels.
     * @param bpp The image's number of bytes per pixel.
     * @param out_buffer The buffer to output data. The buffer must have a minimum size of
     * (`out_stride * height`) bytes. Set to `NULL` if the memory could not be allocated.
     * @param out_stride The stride of the buffer, in bytes.
     * By default, `out_stride` is `width * bpp`, but can be changed if needed.
     * The value must be greater than or equal to  `width * bpp` or an error will occur.
     */
    void (*image_alloc)(void *user_data, uint32_t width, uint32_t height, uint8_t bpp,
                        uint8_t **out_buffer, uint32_t *out_stride);
} ok_jpg_allocator;

#if !defined(OK_NO_DEFAULT_ALLOCATOR)

/// The default allocator using stdlib's `malloc` and `free`.
extern const ok_jpg_allocator OK_JPG_DEFAULT_ALLOCATOR;

#endif

#if !defined(OK_NO_STDIO)

/**
 * Reads a JPG image using the default "stdlib" allocator.
 * On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param file The file to read.
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_JPG_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_jpg object.
*/
ok_jpg ok_jpg_read_with_allocator(FILE *file, ok_jpg_decode_flags decode_flags,
                                  ok_jpg_allocator allocator, void *allocator_user_data);

#endif

// MARK: Reading from custom input

typedef struct {
    /**
     * Reads bytes from its source (typically `user_data`), copying the data to `buffer`.
     *
     * @param user_data The parameter that was passed to the #ok_jpg_read_from_input()
     * @param buffer The data buffer to copy bytes to.
     * @param count The number of bytes to read.
     * @return The number of bytes read.
     */
    size_t (*read)(void *user_data, uint8_t *buffer, size_t count);

    /**
     * Skips bytes from its source (typically `user_data`).
     *
     * @param user_data The parameter that was passed to the #ok_jpg_read_from_input().
     * @param count The number of bytes to skip.
     * @return `true` if success.
     */
    bool (*seek)(void *user_data, long count);
} ok_jpg_input;

/**
 * Reads a JPG image. On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @param input_callbacks The custom input functions.
 * @param input_callbacks_user_data The parameter to be passed to the input's `read` and `seek` functions.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_JPG_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_jpg object.
 */
ok_jpg ok_jpg_read_from_input(ok_jpg_decode_flags decode_flags,
                              ok_jpg_input input_callbacks, void *input_callbacks_user_data,
                              ok_jpg_allocator allocator, void *allocator_user_data);

#ifdef __cplusplus
}
#endif

#endif
