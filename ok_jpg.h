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

#ifndef OK_JPG_H
#define OK_JPG_H

/**
 * @file
 * Functions to read JPEG files.
 *
 * This JPEG decoder:
 * - Reads most JPEG files.
 * - Baseline only (no progressive JPEGs)
 * - Interprets EXIF orientation tags.
 * - Option to get the image dimensions without decoding.
 * - Option to flip the image vertically.
 * - Returns data in RGBA or BGRA format.
 *
 * Caveats:
 * - No progressive, lossless, or CMYK support.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_jpg.h"
 *
 *     int main() {
 *         FILE *file = fopen("my_image.jpg", "rb");
 *         ok_jpg *image = ok_jpg_read(file, OK_JPG_COLOR_FORMAT_RGBA);
 *         fclose(file);
 *         if (image->data) {
 *             printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
 *         }
 *         ok_jpg_free(image);
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
 * The data returned from #ok_jpg_read() or #ok_jpg_read_info().
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *data;
    char error_message[80];
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

#ifndef OK_NO_STDIO

/**
 * Reads a JPEG image. On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_message is set.
 *
 * @param file The file to read.
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read(FILE *file, ok_jpg_decode_flags decode_flags);

#endif

/**
 * Frees the image. This function should always be called when done with the image, even if reading
 * failed.
 */
void ok_jpg_free(ok_jpg *jpg);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_jpg_read_from_callbacks() or #ok_jpg_read_info_from_callbacks()
 * functions.
 *
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_jpg_read_from_callbacks() or
 * ok_png_read_info_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_jpg_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Seek function provided to the #ok_jpg_read_from_callbacks() or #ok_jpg_read_info_from_callbacks()
 * functions.
 *
 * This function must skip bytes from its source (typically `user_data`).
 *
 * @param user_data The parameter that was passed to the #ok_jpg_read_from_callbacks() or
 * ok_png_read_info_from_callbacks() function.
 * @param count The number of bytes to skip.
 * @return `true` if success.
 */
typedef bool (*ok_jpg_seek_func)(void *user_data, long count);

/**
 * Reads a JPEG image. On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read_from_callbacks(void *user_data, ok_jpg_read_func read_func,
                                   ok_jpg_seek_func seek_func, ok_jpg_decode_flags decode_flags);

#ifdef __cplusplus
}
#endif

#endif
