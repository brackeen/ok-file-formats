/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2019 David Brackeen

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

#ifndef OK_FNT_H
#define OK_FNT_H

/**
 * @file
 * Functions to read AngelCode BMFont files.
 * Binary format, version 3, from AngelCode Bitmap Font Generator v1.10 or newer.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_fnt.h"
 *
 *     int main() {
 *         FILE *file = fopen("my_font.fnt", "rb");
 *         ok_fnt *fnt = ok_fnt_read(file);
 *         fclose(file);
 *         if (fnt->num_glyphs > 0) {
 *             printf("Got FNT! %i glyphs\n", fnt->num_glyphs);
 *             printf("First glyph is '%c' in page '%s'.\n", fnt->glyphs[0].ch,
 *                    fnt->page_names[fnt->glyphs[0].page]);
 *         }
 *         ok_fnt_free(fnt);
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
 * The data returned from #ok_fnt_read(). The `name` may be `NULL`. For details, see
 * http://www.angelcode.com/products/bmfont/doc/file_format.html
 */
typedef struct {
    char *name;
    int size;
    int line_height;
    int base;
    size_t num_pages;
    char **page_names;
    size_t num_glyphs;
    ok_fnt_glyph *glyphs;
    size_t num_kerning_pairs;
    ok_fnt_kerning *kerning_pairs;
    const char *error_message;
} ok_fnt;

#ifndef OK_NO_STDIO

/**
 * Reads a FNT file.
 * On failure, #ok_fnt.num_glyphs is 0 and #ok_fnt.error_message is set.
 *
 * @param file The file to read.
 * @return a new #ok_fnt object. Never returns `NULL`. The object should be freed with
 * #ok_fnt_free().
 */
ok_fnt *ok_fnt_read(FILE *file);

#endif

/**
 * Frees the font. This function should always be called when done with the font, even if reading
 * failed.
 */
void ok_fnt_free(ok_fnt *fnt);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_fnt_read_from_callbacks() function.
 *
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_fnt_read_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_fnt_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Reads a FNT file.
 * On failure, #ok_fnt.num_glyphs is 0 and #ok_fnt.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @return a new #ok_fnt object. Never returns `NULL`. The object should be freed with
 * #ok_fnt_free().
 */
ok_fnt *ok_fnt_read_from_callbacks(void *user_data, ok_fnt_read_func read_func);

#ifdef __cplusplus
}
#endif

#endif
