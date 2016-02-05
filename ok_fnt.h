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

#ifndef _OK_FNT_H_
#define _OK_FNT_H_

/**
 * @file
 * Functions to read AngelCode BMFont files.
 * Binary format, version 3, from AngelCode Bitmap Font Generator v1.10 or newer.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t ch;
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    int16_t offset_x;
    int16_t offset_y;
    int16_t advance_x;
    uint8_t page;
    uint8_t channel;
} ok_fnt_glyph;

typedef struct {
    uint32_t first_char;
    uint32_t second_char;
    int16_t amount;
} ok_fnt_kerning;

/**
 * The data returned from #ok_fnt_read(). For details, see
 * http://www.angelcode.com/products/bmfont/doc/file_format.html
 */
typedef struct {
    char *name;
    int size;
    int line_height;
    int base;
    int num_pages;
    char **page_names;
    int num_glyphs;
    ok_fnt_glyph *glyphs;
    int num_kerning_pairs;
    ok_fnt_kerning *kerning_pairs;
    char error_message[80];
} ok_fnt;

/**
 * Input function provided to the #ok_fnt_read() function.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_fnt_read() function.
 * @param buffer The data buffer to copy bytes to. If `NULL`, this function should perform a
 * relative seek.
 * @param count The number of bytes to read. If negative, this function should perform a
 * relative seek.
 * @return The number of bytes read or skipped. Should return 0 on error.
 */
typedef int (*ok_fnt_input_func)(void *user_data, unsigned char *buffer, int count);

/**
 * Reads a FNT file.
 * On failure, #ok_fnt.num_glyphs is 0 and #ok_fnt.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a FNT file from.
 * @return a new #ok_fnt object. Never returns `NULL`. The object should be freed with
 * #ok_fnt_free().
 */
ok_fnt *ok_fnt_read(void *user_data, ok_fnt_input_func input_func);

/**
 * Frees the font. This function should always be called when done with the font, even if reading
 * failed.
 */
void ok_fnt_free(ok_fnt *fnt);

#ifdef __cplusplus
}
#endif

#endif
