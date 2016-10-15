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
 *         FILE *fp = fopen("my_image.jpg", "rb");
 *         ok_jpg *image = ok_jpg_read(fp, file_input_func, OK_JPG_COLOR_FORMAT_RGBA, false);
 *         fclose(fp);
 *         if (image->data) {
 *             printf("Got image! Size: %li x %li\n", (long)image->width, (long)image->height);
 *         }
 *         ok_jpg_free(image);
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>

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
 * The color format that should be returned when decoding the JPEG data.
 */
typedef enum {
    /// Specifies that data should be returned in RGBA format. The alpha is always 255.
    OK_JPG_COLOR_FORMAT_RGBA = 0,
    /// Specifies that data should be returned in BGRA format. The alpha is always 255.
    OK_JPG_COLOR_FORMAT_BGRA,
} ok_jpg_color_format;

/**
 * Input function provided to the #ok_jpg_read() or #ok_jpg_read_info() functions.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_jpg_read() or #ok_jpg_read_info()
 * functions.
 * @param buffer The data buffer to copy bytes to. If `NULL`, this function should perform a
 * relative seek.
 * @param count The number of bytes to read. If negative, this function should perform a
 * relative seek.
 * @return The number of bytes read or skipped. Should return 0 on error.
 */
typedef int (*ok_jpg_input_func)(void *user_data, uint8_t *buffer, int count);

/**
 * Gets a JPEG image's dimensions without reading the image data. Its EXIF orientation is taken into
 * consideration.
 * On failure, #ok_jpg.width and #ok_jpg.height are both zero and #ok_jpg.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a JPEG file from.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read_info(void *user_data, ok_jpg_input_func input_func);

/**
 * Reads a JPEG image. On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a JPEG file from.
 * @param color_format The format to return the pixel data.
 * @param flip_y If `true`, the returned image data is flipped along the vertical axis, so that the
 * first row of data is the last row in the image.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read(void *user_data, ok_jpg_input_func input_func, ok_jpg_color_format color_format,
                    bool flip_y);

/**
 * Frees the image. This function should always be called when done with the image, even if reading
 * failed.
 */
void ok_jpg_free(ok_jpg *jpg);

#ifdef __cplusplus
}
#endif

#endif
