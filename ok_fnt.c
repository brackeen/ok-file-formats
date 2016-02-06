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

#include "ok_fnt.h"
#include <memory.h>
#include <stdarg.h>
#include <stdio.h> // For vsnprintf
#include <stdlib.h>

typedef struct {
    ok_fnt *fnt;

    // Input
    void *input_data;
    ok_fnt_input_func input_func;

} fnt_decoder;

static void ok_fnt_error(ok_fnt *fnt, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

static void ok_fnt_error(ok_fnt *fnt, const char *format, ...) {
    if (fnt) {
        fnt->num_glyphs = 0;
        if (format) {
            va_list args;
            va_start(args, format);
            vsnprintf(fnt->error_message, sizeof(fnt->error_message), format, args);
            va_end(args);
        }
    }
}

static bool ok_read(fnt_decoder *decoder, uint8_t *data, const int length) {
    if (decoder->input_func(decoder->input_data, data, length) == length) {
        return true;
    } else {
        ok_fnt_error(decoder->fnt, "Read error: error calling input function.");
        return false;
    }
}

static void decode_fnt(ok_fnt *fnt, void *input_data, ok_fnt_input_func input_func);

// Public API

ok_fnt *ok_fnt_read(void *user_data, ok_fnt_input_func input_func) {
    ok_fnt *fnt = calloc(1, sizeof(ok_fnt));
    if (input_func) {
        decode_fnt(fnt, user_data, input_func);
    } else {
        ok_fnt_error(fnt, "Invalid argument: input_func is NULL");
    }
    return fnt;
}

void ok_fnt_free(ok_fnt *fnt) {
    if (fnt) {
        if (fnt->name) {
            free(fnt->name);
        }
        if (fnt->page_names) {
            // The memory was only allocated for the first item;
            // the remaining items are pointers within the first, so they shouldn't be freed.
            if (fnt->page_names[0]) {
                free(fnt->page_names[0]);
            }
            free(fnt->page_names);
        }
        if (fnt->glyphs) {
            free(fnt->glyphs);
        }
        if (fnt->kerning_pairs) {
            free(fnt->kerning_pairs);
        }
        free(fnt);
    }
}

// Decoding

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

typedef enum {
    BLOCK_TYPE_INFO = 1,
    BLOCK_TYPE_COMMON = 2,
    BLOCK_TYPE_PAGES = 3,
    BLOCK_TYPE_CHARS = 4,
    BLOCK_TYPE_KERNING = 5,
} block_type;

static void decode_fnt2(fnt_decoder *decoder) {
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
        ok_fnt_error(
            fnt,
            "Version %i of AngelCode binary FNT file not supported (only version 3 supported).",
            header[3]);
        return;
    }

    uint32_t block_types_found = 0;
    while (true) {
        uint8_t block_header[5];
        if (decoder->input_func(decoder->input_data, block_header,
            sizeof(block_header)) != sizeof(block_header)) {
            // Don't give an error if all required blocks have been found.
            const bool all_required_blocks_found = (block_types_found & 0x1E) == 0x1E;
            if (!all_required_blocks_found) {
                ok_fnt_error(decoder->fnt, "Read error: error calling input function.");
            }
            return;
        }

        block_type block_type = block_header[0];
        uint32_t block_length = readLE32(block_header + 1);

        block_types_found |= (1 << block_type);
        switch (block_type) {
            case BLOCK_TYPE_INFO: {
                uint8_t info_header[14];
                const int name_buffer_length = block_length - sizeof(info_header);
                if (name_buffer_length <= 0) {
                    ok_fnt_error(fnt, "Invalid info block");
                    return;
                }
                if (!ok_read(decoder, info_header, sizeof(info_header))) {
                    return;
                }
                // Get the fnt size, ignore the rest
                fnt->size = readLE16(info_header);

                // Get the fnt name
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

            case BLOCK_TYPE_COMMON: {
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

            case BLOCK_TYPE_PAGES: {
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
                    int next_index = 1;
                    while (pos + 1 < end_pos && next_index < fnt->num_pages) {
                        if (*pos == 0) {
                            fnt->page_names[next_index] = pos + 1;
                            next_index++;
                        }
                        pos++;
                    }
                    // Sanity check - make sure the remaining page names, if any, point somewhere
                    for (int i = next_index; i < fnt->num_pages; i++) {
                        fnt->page_names[i] = end_pos - 1;
                    }
                }
                break;
            }

            case BLOCK_TYPE_CHARS: {
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
                for (int i = 0; i < fnt->num_glyphs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_fnt_glyph *glyph = &fnt->glyphs[i];
                    glyph->ch = readLE32(data);
                    glyph->x = readLE16(data + 4);
                    glyph->y = readLE16(data + 6);
                    glyph->width = readLE16(data + 8);
                    glyph->height = readLE16(data + 10);
                    glyph->offset_x = readLE16(data + 12);
                    glyph->offset_y = readLE16(data + 14);
                    glyph->advance_x = readLE16(data + 16);
                    glyph->page = data[18];
                    glyph->channel = data[19];
                }
                break;
            }

            case BLOCK_TYPE_KERNING: {
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
                for (int i = 0; i < fnt->num_kerning_pairs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_fnt_kerning *kerning = &fnt->kerning_pairs[i];
                    kerning->first_char = readLE32(data);
                    kerning->second_char = readLE32(data + 4);
                    kerning->amount = readLE16(data + 8);
                }
                break;
            }

            default:
                ok_fnt_error(fnt, "Unknown block type: %i", block_type);
                return;
        }
    }
}

static void decode_fnt(ok_fnt *fnt, void *input_data, ok_fnt_input_func input_func) {
    if (fnt) {
        fnt_decoder *decoder = calloc(1, sizeof(fnt_decoder));
        if (!decoder) {
            ok_fnt_error(fnt, "Couldn't allocate decoder.");
            return;
        }
        decoder->fnt = fnt;
        decoder->input_data = input_data;
        decoder->input_func = input_func;

        decode_fnt2(decoder);

        free(decoder);
    }
}
