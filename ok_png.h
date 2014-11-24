/*
 ok-file-formats
 Copyright (c) 2014 David Brackeen
 
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

/*
 This PNG decoder:
 - Supports all PNG color types and bit depths.
 - Supports Apple's proprietary PNG extensions for iOS.
 - Options to premultiply alpha and flip data vertically.
 - Option to get image dimensions without decoding.
 - Returns data in RGBA or BGRA format.
 Caveats:
 - No gamma conversion.
 - No CRC or ADLER32 checks.
 - Ignores all chunks not related to the image.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
  
    typedef enum {
        OK_PNG_COLOR_FORMAT_RGBA = 0,
        OK_PNG_COLOR_FORMAT_RGBA_PRE,
        OK_PNG_COLOR_FORMAT_BGRA,
        OK_PNG_COLOR_FORMAT_BGRA_PRE,
    } ok_png_color_format;
    
    typedef struct {
        uint32_t width;
        uint32_t height;
        bool has_alpha;
        uint8_t *data;
        char error_message[80];
    } ok_png;
    
    /**
     Input function provided to the ok_png_read function.
     Reads 'count' bytes into buffer. Returns number of bytes actually read.
     If buffer is NULL or 'count' is negative, this function should perform a relative seek.
     */
    typedef int (*ok_png_input_func)(void *user_data, unsigned char *buffer, const int count);

    /**
     Gets a PNG image's dimensions and color format without reading the image data. 
     On failure, width and height are both zero. 
     */
    ok_png *ok_png_read_info(void *user_data, ok_png_input_func input_func);
    
    /**
     Reads a PNG image.
     On success, the data field contains to packed image data, with a size of (width * height * 4).
     On failure, data is NULL.
     
     The color_format parameter specifies the format to return the pixel data (RGBA or BGRA, with or without 
     premultiplied alpha).
     
     If flip_y is true, the returned image data is flipped along the vertical axis, so that the first row of data
     is the last row in the image.
     */
    ok_png *ok_png_read(void *user_data, ok_png_input_func input_func,
                        const ok_png_color_format color_format, const bool flip_y);
    
    /**
     Frees the image. This function should always be called when done with the image, even if reading failed.
     */
    void ok_png_free(ok_png *png);

    //
    // Inflater - used internally by the PNG decoder, and may be useful for other applications.
    // If you only need to read PNG files, you don't need to use these functions.
    //
    
    typedef struct ok_inflater ok_inflater;
    
    /// Creates an inflater. Returns NULL on error. If nowrap is true, the inflater assumes there is no
    /// zlib wrapper around the data. The inflater should be freed with ok_inflater_free().
    ok_inflater *ok_inflater_init(const bool nowrap);
    
    // Resets the inflater to work with a new stream.
    void ok_inflater_reset(ok_inflater *inflater);

    /// Returns true if the inflater needs more input.
    bool ok_inflater_needs_input(const ok_inflater *inflater);
    
    /// Sets the input for the inflater. Only call this function if ok_inflater_needs_input() returns true, otherwise,
    /// an error may occur.
    void ok_inflater_set_input(ok_inflater *inflater, const void *buffer, const unsigned int buffer_length);
    
    /// Inflates at most dst_length bytes. Returns number of bytes inflated, or a negative value if an error occured.
    int ok_inflater_inflate(ok_inflater *inflater, uint8_t *dst, const unsigned int dst_length);
    
    /// Gets the error message, if any. Returns a zero-length string if no error.
    /// The string is owned by the inflater and should not be freed.
    const char *ok_inflater_error_message(const ok_inflater *inflater);
    
    /// Frees the inflater.
    void ok_inflater_free(ok_inflater *inflater);
    
#ifdef __cplusplus
}
#endif

#endif
