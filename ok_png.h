/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2016 David Brackeen
 
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

#ifndef _OK_PNG_H_
#define _OK_PNG_H_

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
 *     static int file_input_func(void *user_data, uint8_t *buffer, const int count) {
 *         FILE *fp = (FILE *)user_data;
 *         if (buffer && count > 0) {
 *             return (int)fread(buffer, 1, (size_t)count, fp);
 *         } else if (fseek(fp, count, SEEK_CUR) == 0) {
 *             return count;
 *         } else {
 *             return 0;
 *         }
 *     }
 *
 *     int main() {
 *         FILE *fp = fopen("my_image.png", "rb");
 *         ok_png *image = ok_png_read(fp, file_input_func, OK_PNG_COLOR_FORMAT_RGBA, false);
 *         fclose(fp);
 *         if (image->data) {
 *             printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
 *         }
 *         ok_png_free(image);
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>

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
 * The color format that should be returned when decoding the PNG data.
 */
typedef enum {
    /// Specifies that data should be returned in RGBA format.
    /// This is the default format for standard PNG files.
    OK_PNG_COLOR_FORMAT_RGBA = 0,
    /// Specifies that data should be returned in RGBA format, with premultiplied alpha.
    /// The color components are multiplied by the alpha component.
    OK_PNG_COLOR_FORMAT_RGBA_PRE,
    /// Specifies that data should be returned in BGRA format.
    OK_PNG_COLOR_FORMAT_BGRA,
    /// Specifies that data should be returned in BGRA format, with premultiplied alpha.
    /// The color components are multiplied by the alpha component.
    /// This is the default format on iOS devices.
    OK_PNG_COLOR_FORMAT_BGRA_PRE,
} ok_png_color_format;

/**
 * Input function provided to the #ok_png_read() or #ok_png_read_info() functions.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_png_read() or #ok_png_read_info() 
 * functions.
 * @param buffer The data buffer to copy bytes to. If `NULL`, this function should perform a 
 * relative seek.
 * @param count The number of bytes to read. If negative, this function should perform a
 * relative seek.
 * @return The number of bytes read or skipped. Should return 0 on error.
 */
typedef int (*ok_png_input_func)(void *user_data, uint8_t *buffer, int count);

/**
 * Gets a PNG image's dimensions and color format without reading the image data.
 * On failure, #ok_png.width and #ok_png.height are both zero and #ok_png.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a PNG file from.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with
 * #ok_png_free().
 */
ok_png *ok_png_read_info(void *user_data, ok_png_input_func input_func);

/**
 * Reads a PNG image. On success, #ok_png.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_png.data is `NULL` and #ok_png.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a PNG file from.
 * @param color_format The format to return the pixel data.
 * @param flip_y If `true`, the returned image data is flipped along the vertical axis, so that the
 * first row of data is the last row in the image.
 * @return a new #ok_png object. Never returns `NULL`. The object should be freed with 
 * #ok_png_free().
 */
ok_png *ok_png_read(void *user_data, ok_png_input_func input_func, ok_png_color_format color_format,
                    bool flip_y);

/**
 * Frees the image. This function should always be called when done with the image, even if reading 
 * failed.
 */
void ok_png_free(ok_png *png);

//
// Inflater - used internally by the PNG decoder, and may be useful for other applications.
// If you only need to read PNG files, you don't need to use these functions.
//

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
void ok_inflater_set_input(ok_inflater *inflater, const void *buffer, unsigned int buffer_length);

/**
 * Inflates at most `dst_length` bytes. Returns the number of bytes inflated, or a negative value
 * if an error occured.
 *
 * @param inflater The inflater.
 * @param dst The destination buffer to inflate bytes to.
 * @param dst_length The maximum number of bytes to inflate.
 */
int ok_inflater_inflate(ok_inflater *inflater, uint8_t *dst, unsigned int dst_length);

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
