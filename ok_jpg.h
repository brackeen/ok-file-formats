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
#ifndef _OK_JPG_H_
#define _OK_JPG_H_

/*
 JPEG decoder (baseline only - no exteded, progressive, lossless, or CMYK)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#ifndef _OK_IMAGE_STRUCT_
#define _OK_IMAGE_STRUCT_
    
    typedef enum {
        OK_COLOR_FORMAT_RGBA = 0,
        OK_COLOR_FORMAT_RGBA_PRE,
        OK_COLOR_FORMAT_BGRA,
        OK_COLOR_FORMAT_BGRA_PRE,
    } ok_color_format;
    
    typedef struct {
        uint32_t width;
        uint32_t height;
        bool has_alpha;
        uint8_t *data;
        char error_message[80];
    } ok_image;
    
#endif
    
#ifndef _OK_READ_FUNC_
#define _OK_READ_FUNC_
    /// Reads 'count' bytes into buffer. Returns number of bytes read.
    typedef size_t (*ok_read_func)(void *user_data, uint8_t *buffer, const size_t count);
    
    /// Seek function. Should return 0 on success.
    typedef int (*ok_seek_func)(void *user_data, const int count);
#endif
    
    ok_image *ok_jpg_read_info(const char *file_name);
    ok_image *ok_jpg_read_info_from_file(FILE *file);
    ok_image *ok_jpg_read_info_from_memory(const void *buffer, const size_t buffer_length);
    ok_image *ok_jpg_read_info_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func);
    
    ok_image *ok_jpg_read(const char *file_name, const ok_color_format color_format, const bool flip_y);
    ok_image *ok_jpg_read_from_file(FILE *file, const ok_color_format color_format, const bool flip_y);
    ok_image *ok_jpg_read_from_memory(const void *buffer, const size_t buffer_length,
                                      const ok_color_format color_format, const bool flip_y);
    ok_image *ok_jpg_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                                         const ok_color_format color_format, const bool flip_y);
    
    /**
     Frees the image. This function should always be called when done with the image, even if reading failed.
     */
    void ok_image_free(ok_image *image);
        
#ifdef __cplusplus
}
#endif

#endif
