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

#ifndef OK_PNG_H
#define OK_PNG_H

/**
 * @file
 * Functions to read any PNG file.
 *
 * This PNG decoder:
 * - Supports all PNG color types and bit depths.
 * - Supports Apple's proprietary PNG extensions for iOS.
 * - Options to premultiply alpha and flip data vertically.
 * - Option to get image dimensions without decoding.
 * - Returns data in RGBA or BGRA format.
 *
 * Caveats:
 * - No gamma conversion.
 * - No CRC or ADLER32 checks.
 * - Ignores all chunks not related to the image.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_png.h"
 *
 *     int main() {
 *         FILE *file = fopen("my_image.png", "rb");
 *         ok_png image = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA);
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
    OK_PNG_SUCCESS = 0,
    OK_PNG_ERROR_API, // Invalid argument sent to public API function
    OK_PNG_ERROR_INVALID, // Not a valid PNG file
    OK_PNG_ERROR_INFLATER, // Couldn't inflate data
    OK_PNG_ERROR_UNSUPPORTED, // Unsupported PNG file (width > 1073741824)
    OK_PNG_ERROR_ALLOCATION, // Couldn't allocate memory
    OK_PNG_ERROR_IO, // Couldn't read or seek the file
} ok_png_error;

/**
 * The data returned from #ok_png_read().
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t bpp; // Always 4
    bool has_alpha;
    ok_png_error error_code:16;
    uint8_t *data;
} ok_png;

/**
 * PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 */
typedef enum {
    /// Set for RGBA color format. This is the default format for standard PNG files.
    OK_PNG_COLOR_FORMAT_RGBA = (0 << 0),
    /// Set for BGRA color format.
    OK_PNG_COLOR_FORMAT_BGRA = (1 << 0),
    /// Set for premultiplied alpha. The default format on iOS devices is
    /// `(OK_PNG_COLOR_FORMAT_RGBA | OK_PNG_PREMULTIPLIED_ALPHA)`
    OK_PNG_PREMULTIPLIED_ALPHA = (1 << 1),
    /// Set to flip the image data along the horizontal axis, so that the first row of data is
    /// the last row in the image.
    OK_PNG_FLIP_Y = (1 << 2),
    /// Set to read an image's dimensions and color format without reading the image data.
    OK_PNG_INFO_ONLY = (1 << 3)
} ok_png_decode_flags;

// MARK: Reading from a FILE

#if !defined(OK_NO_STDIO) && !defined(OK_NO_DEFAULT_ALLOCATOR)

/**
 * Reads a PNG image using the default "stdlib" allocator.
 * On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller (using stdlib's `free()`).
 *
 * @param file The file to read.
 * @param decode_flags The PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a #ok_png object.
 */
ok_png ok_png_read(FILE *file, ok_png_decode_flags decode_flags);

#endif

// MARK: Reading from a FILE, using a custom allocator

typedef struct {
    /**
     * Allocates uninitilized memory.
     *
     * @param user_data The pointer passed to #ok_png_read_with_allocator.
     * @param size The size of the memory to allocate.
     * @return the pointer to the newly allocated memory, or `NULL` if the memory could not be allocated.
     */
    void *(*alloc)(void *user_data, size_t size);
    
    /**
     * Frees memory previously allocated with `alloc`.
     *
     * @param user_data The pointer passed to #ok_png_read_with_allocator.
     * @param memory The memory to free.
     */
    void (*free)(void *user_data, void *memory);
    
    /**
     * Allocates memory for the decoded image.
     * This function may be `NULL`, in which case `alloc` is used instead.
     *
     * @param user_data The pointer passed to #ok_png_read_with_allocator.
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
} ok_png_allocator;

#if !defined(OK_NO_DEFAULT_ALLOCATOR)

/// The default allocator using stdlib's `malloc` and `free`.
extern const ok_png_allocator OK_PNG_DEFAULT_ALLOCATOR;

#endif

#if !defined(OK_NO_STDIO)

/**
 * Reads a PNG image using a custom allocator.
 * On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param file The file to read.
 * @param decode_flags The PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_PNG_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_png object.
*/
ok_png ok_png_read_with_allocator(FILE *file, ok_png_decode_flags decode_flags,
                                  ok_png_allocator allocator, void *allocator_user_data);

#endif

// MARK: Reading from custom input

typedef struct {
    /**
     * Reads bytes from its source (typically `user_data`), copying the data to `buffer`.
     *
     * @param user_data The parameter that was passed to the #ok_png_read_from_input()
     * @param buffer The data buffer to copy bytes to.
     * @param count The number of bytes to read.
     * @return The number of bytes read.
     */
    size_t (*read)(void *user_data, uint8_t *buffer, size_t count);

    /**
     * Skips bytes from its source (typically `user_data`).
     *
     * @param user_data The parameter that was passed to the #ok_png_read_from_input().
     * @param count The number of bytes to skip.
     * @return `true` if success.
     */
    bool (*seek)(void *user_data, long count);
} ok_png_input;

/**
 * Reads a PNG image. On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_code is nonzero.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param decode_flags The PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 * @param input_callbacks The custom input functions.
 * @param input_callbacks_user_data The parameter to be passed to the input's `read` and `seek` functions.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_PNG_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_png object.
 */
ok_png ok_png_read_from_input(ok_png_decode_flags decode_flags,
                              ok_png_input input_callbacks, void *input_callbacks_user_data,
                              ok_png_allocator allocator, void *allocator_user_data);

// MARK: Inflater

typedef struct ok_inflater ok_inflater;

/**
 * Creates an inflater. Returns `NULL` on error. The inflater should be freed with 
 * #ok_inflater_free().
 *
 * @param nowrap If `true`, the inflater assumes there is no zlib wrapper around the data.
 */
ok_inflater *ok_inflater_init(bool nowrap, ok_png_allocator allocator, void *allocator_user_data);

/**
 * Resets the inflater to work with a new stream.
 */
void ok_inflater_reset(ok_inflater *inflater);

/**
 * Returns true if the inflater needs more input.
 */
bool ok_inflater_needs_input(const ok_inflater *inflater);

/**
 * Sets the input for the inflater. Only call this function if #ok_inflater_needs_input() returns
 * `true`, otherwise, an error may occur.
 *
 * @param inflater The inflater.
 * @param buffer The input buffer.
 * @param buffer_length The length of the input buffer.
 */
void ok_inflater_set_input(ok_inflater *inflater, const uint8_t *buffer, size_t buffer_length);

/**
 * Inflates at most `dst_length` bytes. Returns the number of bytes inflated, or `SIZE_MAX`
 * if an error occured.
 *
 * @param inflater The inflater.
 * @param dst The destination buffer to inflate bytes to.
 * @param dst_length The maximum number of bytes to inflate.
 */
size_t ok_inflater_inflate(ok_inflater *inflater, uint8_t *dst, size_t dst_length);

/**
 * Frees the inflater.
 */
void ok_inflater_free(ok_inflater *inflater);

#ifdef __cplusplus
}
#endif

#endif
