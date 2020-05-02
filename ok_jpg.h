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

typedef enum {
    OK_JPG_SUCCESS = 0,
    OK_JPG_ERROR_API, // Invalid argument sent to public API function
    OK_JPG_ERROR_INVALID, // Not a valid JPG file
    OK_JPG_ERROR_UNSUPPORTED, // Unsupported JPG file (CMYK)
    OK_JPG_ERROR_ALLOCATION, // Couldn't allocate memory
    OK_JPG_ERROR_IO, // Couldn't read or seek the file
} ok_jpg_error;

/**
 * The data returned from #ok_jpg_read() or #ok_jpg_read_info().
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t *data;
    ok_jpg_error error_code;
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
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_code is nonzero.
 *
 * @param file The file to read.
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read(FILE *file, ok_jpg_decode_flags decode_flags);

/**
 * Reads a JPEG image, outputing image data to a preallocated buffer.
 *
 * On success, #ok_jpg.width and #ok_jpg.height are set.
 * On failure, #ok_jpg.error_code is nonzero.
 *
 * @param file The file to read.
 * @param dst_buffer The buffer to output data. The buffer must have a minimum size of
 * (`dst_stride * height`). If `NULL`, a newly allocated buffer is used and assigned to
 * #ok_jpg.data.
 * @param dst_stride The stride of the buffer, in bytes. If 0, the stride is assumed to be
 * (`width * 4`).
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * first row of data is the last row in the image.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read_to_buffer(FILE *file, uint8_t *dst_buffer, uint32_t dst_stride,
                              ok_jpg_decode_flags decode_flags);

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
 * ok_jpg_read_info_from_callbacks() function.
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
 * ok_jpg_read_info_from_callbacks() function.
 * @param count The number of bytes to skip.
 * @return `true` if success.
 */
typedef bool (*ok_jpg_seek_func)(void *user_data, long count);

/**
 * Reads a JPEG image. On success, #ok_jpg.data contains the packed image data, with a size of
 * (`width * height * 4`). On failure, #ok_jpg.data is `NULL` and #ok_jpg.error_code is nonzero.
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

/**
 * Reads a JPEG image, outputing image data to a preallocated buffer.
 *
 * On success, #ok_jpg.width and #ok_jpg.height are set.
 * On failure, #ok_jpg.error_code is nonzero.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @param dst_buffer The buffer to output data. The buffer must have a minimum size of
 * (`dst_stride * height`). If `NULL`, a newly allocated buffer is used and assigned to
 * #ok_jpg.data.
 * @param dst_stride The stride of the buffer, in bytes. If 0, the stride is assumed to be
 * (`width * 4`).
 * @param decode_flags The JPG decode flags. Use `OK_JPG_COLOR_FORMAT_RGBA` for the most cases.
 * first row of data is the last row in the image.
 * @return a new #ok_jpg object. Never returns `NULL`. The object should be freed with
 * #ok_jpg_free().
 */
ok_jpg *ok_jpg_read_from_callbacks_to_buffer(void *user_data, ok_jpg_read_func read_func,
                                             ok_jpg_seek_func seek_func,
                                             uint8_t *dst_buffer, uint32_t dst_stride,
                                             ok_jpg_decode_flags decode_flags);

#ifdef __cplusplus
}
#endif

#endif
