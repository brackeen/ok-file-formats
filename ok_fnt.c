/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen

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

#include "ok_fnt.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    ok_fnt *fnt;

    // Input
    void *input_data;
    ok_fnt_read_func input_read_func;

} ok_fnt_decoder;

#ifdef NDEBUG
#define ok_fnt_error(fnt, message) ok_fnt_set_error((fnt), "ok_fnt_error")
#else
#define ok_fnt_error(fnt, message) ok_fnt_set_error((fnt), (message))
#endif

static void ok_fnt_set_error(ok_fnt *fnt, const char *message) {
    if (fnt) {
        fnt->num_glyphs = 0;
        fnt->error_message = message;
    }
}

static bool ok_read(ok_fnt_decoder *decoder, uint8_t *data, size_t length) {
    if (decoder->input_read_func(decoder->input_data, data, length) == length) {
        return true;
    } else {
        ok_fnt_error(decoder->fnt, "Read error: error calling input function.");
        return false;
    }
}

static void ok_fnt_decode(ok_fnt *fnt, void *input_data, ok_fnt_read_func input_read_func);

#ifndef OK_NO_STDIO

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, size_t length) {
    return fread(buffer, 1, length, (FILE *)user_data);
}

#endif

// MARK: Public API

#ifndef OK_NO_STDIO

ok_fnt *ok_fnt_read(FILE *file) {
    ok_fnt *fnt = calloc(1, sizeof(ok_fnt));
    if (file) {
        ok_fnt_decode(fnt, file, ok_file_read_func);
    } else {
        ok_fnt_error(fnt, "File not found");
    }
    return fnt;
}

#endif

ok_fnt *ok_fnt_read_from_callbacks(void *user_data, ok_fnt_read_func input_read_func) {
    ok_fnt *fnt = calloc(1, sizeof(ok_fnt));
    if (input_read_func) {
        ok_fnt_decode(fnt, user_data, input_read_func);
    } else {
        ok_fnt_error(fnt, "Invalid argument: read_func is NULL");
    }
    return fnt;
}

void ok_fnt_free(ok_fnt *fnt) {
    if (fnt) {
        free(fnt->name);
        if (fnt->page_names) {
            // The memory was only allocated for the first item;
            // the remaining items are pointers within the first, so they shouldn't be freed.
            free(fnt->page_names[0]);
            free(fnt->page_names);
        }
        free(fnt->glyphs);
        free(fnt->kerning_pairs);
        free(fnt);
    }
}

// Decoding

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (((uint32_t)data[3] << 24) |
            ((uint32_t)data[2] << 16) |
            ((uint32_t)data[1] << 8) |
            (uint32_t)data[0]);
}

typedef enum {
    OK_FNT_BLOCK_TYPE_INFO = 1,
    OK_FNT_BLOCK_TYPE_COMMON = 2,
    OK_FNT_BLOCK_TYPE_PAGES = 3,
    OK_FNT_BLOCK_TYPE_CHARS = 4,
    OK_FNT_BLOCK_TYPE_KERNING = 5,
} ok_fnt_block_type;

static void ok_fnt_decode2(ok_fnt_decoder *decoder) {
    ok_fnt *fnt = decoder->fnt;

    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    if (memcmp("BMF", header, 3) != 0) {
        ok_fnt_error(fnt, "Not an AngelCode binary FNT file.");
        return;
    }
    if (header[3] != 3) {
        ok_fnt_error(fnt, "Unsupported version of AngelCode binary FNT file "
                     "(only version 3 supported).");
        return;
    }

    uint32_t block_types_found = 0;
    while (true) {
        uint8_t block_header[5];
        if (decoder->input_read_func(decoder->input_data, block_header,
            sizeof(block_header)) != sizeof(block_header)) {
            // Don't give an error if all required blocks have been found.
            const bool all_required_blocks_found = (block_types_found & 0x1E) == 0x1E;
            if (!all_required_blocks_found) {
                ok_fnt_error(decoder->fnt, "Read error: error calling input function.");
            }
            return;
        }

        ok_fnt_block_type block_type = block_header[0];
        uint32_t block_length = readLE32(block_header + 1);

        block_types_found |= (1 << block_type);
        switch (block_type) {
            case OK_FNT_BLOCK_TYPE_INFO: {
                uint8_t info_header[14];
                if (block_length <= sizeof(info_header)) {
                    ok_fnt_error(fnt, "Invalid info block");
                    return;
                }
                if (!ok_read(decoder, info_header, sizeof(info_header))) {
                    return;
                }
                // Get the fnt size, ignore the rest
                fnt->size = readLE16(info_header);

                // Get the fnt name
                const size_t name_buffer_length = block_length - sizeof(info_header);
                fnt->name = malloc(name_buffer_length);
                if (!fnt->name) {
                    ok_fnt_error(fnt, "Couldn't allocate font name");
                    return;
                }
                if (!ok_read(decoder, (uint8_t *)fnt->name, name_buffer_length)) {
                    return;
                }
                // Sanity check - make sure the string has a null-terminator
                fnt->name[name_buffer_length - 1] = 0;
                break;
            }

            case OK_FNT_BLOCK_TYPE_COMMON: {
                uint8_t common[15];
                if (block_length != sizeof(common)) {
                    ok_fnt_error(fnt, "Invalid common block");
                    return;
                }
                if (!ok_read(decoder, common, sizeof(common))) {
                    return;
                }
                // Get the line height, base, and page count; ignore the rest
                fnt->line_height = readLE16(common);
                fnt->base = readLE16(common + 2);
                fnt->num_pages = readLE16(common + 8);
                break;
            }

            case OK_FNT_BLOCK_TYPE_PAGES: {
                if (fnt->num_pages <= 0 || block_length == 0) {
                    ok_fnt_error(fnt, "Couldn't get page names");
                    return;
                } else {
                    fnt->page_names = calloc(fnt->num_pages, sizeof(char *));
                    if (!fnt->page_names) {
                        fnt->num_pages = 0;
                        ok_fnt_error(fnt, "Couldn't allocate memory for page name array");
                        return;
                    }
                    // Load everything into the first item; setup pointers below.
                    fnt->page_names[0] = malloc(block_length);
                    if (!fnt->page_names[0]) {
                        fnt->num_pages = 0;
                        ok_fnt_error(fnt, "Couldn't allocate memory for page names");
                        return;
                    }
                    if (!ok_read(decoder, (uint8_t *)fnt->page_names[0], block_length)) {
                        return;
                    }
                    char *pos = fnt->page_names[0];
                    char *const end_pos = pos + block_length;
                    // Sanity check - make sure there is a null terminator
                    *(end_pos - 1) = 0;

                    // Set up pointers for each page name
                    size_t next_index = 1;
                    while (pos + 1 < end_pos && next_index < fnt->num_pages) {
                        if (*pos == 0) {
                            fnt->page_names[next_index] = pos + 1;
                            next_index++;
                        }
                        pos++;
                    }
                    // Sanity check - make sure the remaining page names, if any, point somewhere
                    for (size_t i = next_index; i < fnt->num_pages; i++) {
                        fnt->page_names[i] = end_pos - 1;
                    }
                }
                break;
            }

            case OK_FNT_BLOCK_TYPE_CHARS: {
                uint8_t data[20];
                fnt->num_glyphs = block_length / sizeof(data);
                fnt->glyphs = malloc(fnt->num_glyphs * sizeof(ok_fnt_glyph));
                if (!fnt->glyphs) {
                    fnt->num_glyphs = 0;
                    ok_fnt_error(fnt, "Couldn't allocate memory for glyphs");
                    return;
                }
                // On little-endian systems we could just load the entire block into memory, but
                // we'll assume the byte order is unknown here.
                for (size_t i = 0; i < fnt->num_glyphs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_fnt_glyph *glyph = &fnt->glyphs[i];
                    glyph->ch = readLE32(data);
                    glyph->x = readLE16(data + 4);
                    glyph->y = readLE16(data + 6);
                    glyph->width = readLE16(data + 8);
                    glyph->height = readLE16(data + 10);
                    glyph->offset_x = (int16_t)readLE16(data + 12);
                    glyph->offset_y = (int16_t)readLE16(data + 14);
                    glyph->advance_x = (int16_t)readLE16(data + 16);
                    glyph->page = data[18];
                    glyph->channel = data[19];
                }
                break;
            }

            case OK_FNT_BLOCK_TYPE_KERNING: {
                uint8_t data[10];
                fnt->num_kerning_pairs = block_length / sizeof(data);
                fnt->kerning_pairs = malloc(fnt->num_kerning_pairs * sizeof(ok_fnt_kerning));
                if (!fnt->kerning_pairs) {
                    fnt->num_kerning_pairs = 0;
                    ok_fnt_error(fnt, "Couldn't allocate memory for kerning");
                    return;
                }
                // On little-endian systems we could just load the entire block into memory, but
                // we'll assume the byte order is unknown here.
                for (size_t i = 0; i < fnt->num_kerning_pairs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_fnt_kerning *kerning = &fnt->kerning_pairs[i];
                    kerning->first_char = readLE32(data);
                    kerning->second_char = readLE32(data + 4);
                    kerning->amount = (int16_t)readLE16(data + 8);
                }
                break;
            }

            default:
                ok_fnt_error(fnt, "Unknown block type");
                return;
        }
    }
}

static void ok_fnt_decode(ok_fnt *fnt, void *input_data, ok_fnt_read_func input_read_func) {
    if (fnt) {
        ok_fnt_decoder *decoder = calloc(1, sizeof(ok_fnt_decoder));
        if (!decoder) {
            ok_fnt_error(fnt, "Couldn't allocate decoder.");
            return;
        }
        decoder->fnt = fnt;
        decoder->input_data = input_data;
        decoder->input_read_func = input_read_func;

        ok_fnt_decode2(decoder);

        free(decoder);
    }
}
