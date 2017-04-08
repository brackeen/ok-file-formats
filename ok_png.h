/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen
 
 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
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
 *         ok_png *image = ok_png_read(file, OK_PNG_COLOR_FORMAT_RGBA);
 *         fclose(file);
 *         if (image->data) {
 *             printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
 *         }
 *         ok_png_free(image);
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

/**
 * The data returned from #ok_png_read() or #ok_png_read_info().
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    bool has_alpha;
    uint8_t *data;
    char error_message[80];
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
    OK_PNG_FLIP_Y = (1 << 2)
} ok_png_decode_flags;

#ifndef OK_NO_STDIO

/**
 * Gets a PNG image's dimensions and color format without reading the image data.
 * On failure, #ok_png.width and #ok_png.height are both zero and #ok_png.error_message is set.
 *
 * @param file The file to read.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with
 * #ok_png_free().
 */
ok_png *ok_png_read_info(FILE *file);

/**
 * Reads a PNG image. On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_message is set.
 *
 * @param file The file to read.

 * @param decode_flags The PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 * first row of data is the last row in the image.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with
 * #ok_png_free().
 */
ok_png *ok_png_read(FILE *file, ok_png_decode_flags decode_flags);

#endif

/**
 * Frees the image. This function should always be called when done with the image, even if reading
 * failed.
 */
void ok_png_free(ok_png *png);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_png_read_from_callbacks() or #ok_png_read_info_from_callbacks()
 * functions.
 *
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_png_read_from_callbacks() or
 * ok_png_read_info_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_png_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Seek function provided to the #ok_png_read_from_callbacks() or #ok_png_read_info_from_callbacks()
 * functions.
 *
 * This function must skip bytes from its source (typically `user_data`).
 *
 * @param user_data The parameter that was passed to the #ok_png_read_from_callbacks() or
 * ok_png_read_info_from_callbacks() function.
 * @param count The number of bytes to skip.
 * @return `true` if success.
 */
typedef bool (*ok_png_seek_func)(void *user_data, long count);

/**
 * Gets a PNG image's dimensions and color format without reading the image data.
 * On failure, #ok_png.width and #ok_png.height are both zero and #ok_png.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with
 * #ok_png_free().
 */
ok_png *ok_png_read_info_from_callbacks(void *user_data, ok_png_read_func read_func,
                                        ok_png_seek_func seek_func);

/**
 * Reads a PNG image. On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @param decode_flags The PNG decode flags. Use `OK_PNG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with
 * #ok_png_free().
 */
ok_png *ok_png_read_from_callbacks(void *user_data, ok_png_read_func read_func,
                                   ok_png_seek_func seek_func, ok_png_decode_flags decode_flags);

// MARK: Inflater

typedef struct ok_inflater ok_inflater;

/**
 * Creates an inflater. Returns `NULL` on error. The inflater should be freed with 
 * #ok_inflater_free().
 *
 * @param nowrap If `true`, the inflater assumes there is no zlib wrapper around the data.
 */
ok_inflater *ok_inflater_init(bool nowrap);

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
 * Gets the error message, if any. Returns a zero-length string if no error.
 * The string is owned by the inflater and should not be freed.
 */
const char *ok_inflater_error_message(const ok_inflater *inflater);

/**
 * Frees the inflater.
 */
void ok_inflater_free(ok_inflater *inflater);

#ifdef __cplusplus
}
#endif

#endif
