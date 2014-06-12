#include "ok_fnt.h"
#include "ok_internal.h"
#include <stdlib.h>
#include <stdarg.h>
#include <memory.h>
#include <errno.h>

typedef struct {
    ok_font *font;
    
    // Input
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;
    
} fnt_decoder;

static void ok_font_error(ok_font *font, const char *format, ... ) {
    if (font != NULL) {
        font->num_glyphs = 0;
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(font->error_message, sizeof(font->error_message), format, args);
            va_end(args);
        }
    }
}
static bool ok_read(fnt_decoder *decoder, uint8_t *data, const size_t length) {
    if (decoder->read_func(decoder->reader_data, data, length) == length) {
        return true;
    }
    else {
        ok_font_error(decoder->font, "Read error: error calling read function.");
        return false;
    }
}

static void decode_fnt(ok_font *font, void *reader_data, ok_read_func read_func, ok_seek_func seek_func);

// Public API

ok_font *ok_fnt_read(const char *file_name) {
    ok_font *font = calloc(1, sizeof(ok_font));
    if (file_name != NULL) {
        FILE *fp = fopen(file_name, "rb");
        if (fp != NULL) {
            decode_fnt(font, fp, ok_file_read_func, ok_file_seek_func);
            fclose(fp);
        }
        else {
            ok_font_error(font, "%s", strerror(errno));
        }
    }
    else {
        ok_font_error(font, "Invalid argument: file_name is NULL");
    }
    return font;
}

ok_font *ok_fnt_read_from_file(FILE *fp) {
    ok_font *font = calloc(1, sizeof(ok_font));
    if (fp != NULL) {
        decode_fnt(font, fp, ok_file_read_func, ok_file_seek_func);
    }
    else {
        ok_font_error(font, "Invalid argument: file is NULL");
    }
    return font;
}

ok_font *ok_fnt_read_from_memory(const void *buffer, const size_t buffer_length) {
    ok_font *font = calloc(1, sizeof(ok_font));
    if (buffer != NULL) {
        ok_memory_source memory;
        memory.buffer = (uint8_t *)buffer;
        memory.remaining_bytes = buffer_length;
        decode_fnt(font, &memory, ok_memory_read_func, ok_memory_seek_func);
    }
    else {
        ok_font_error(font, "Invalid argument: buffer is NULL");
    }
    return font;
}

ok_font *ok_fnt_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func) {
    ok_font *font = calloc(1, sizeof(ok_font));
    if (read_func != NULL && seek_func != NULL) {
        decode_fnt(font, user_data, read_func, seek_func);
    }
    else {
        ok_font_error(font, "Invalid argument: read_func or seek_func is NULL");
    }
    return font;
}

void ok_font_free(ok_font *font) {
    if (font != NULL) {
        if (font->name != NULL) {
            free(font->name);
        }
        if (font->page_names != NULL) {
            // The memory was only allocated for the first item;
            // the remaining items are pointers within the first, so they shouldn't be freed.
            if (font->page_names[0] != NULL) {
                free(font->page_names[0]);
            }
            free(font->page_names);
        }
        if (font->glyphs != NULL) {
            free(font->glyphs);
        }
        if (font->kerning_pairs != NULL) {
            free(font->kerning_pairs);
        }
        free(font);
    }
}

// Decoding

typedef enum {
    BLOCK_TYPE_INFO = 1,
    BLOCK_TYPE_COMMON = 2,
    BLOCK_TYPE_PAGES = 3,
    BLOCK_TYPE_CHARS = 4,
    BLOCK_TYPE_KERNING = 5,
} block_type;

static void decode_fnt2(fnt_decoder *decoder) {
    ok_font *font = decoder->font;
    
    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    if (memcmp("BMF", header, 3) != 0) {
        ok_font_error(font, "Not an AngelCode binary FNT file.");
        return;
    }
    if (header[3] != 3) {
        ok_font_error(font, "Version %i of AngelCode binary FNT file not supported (only version 3 supported).",
                      header[3]);
        return;
    }
    
    uint32_t block_types_found = 0;
    while (true) {
        
        uint8_t block_header[5];
        if (decoder->read_func(decoder->reader_data, block_header, sizeof(block_header)) != sizeof(block_header)) {
            // Don't give an error if all required blocks have been found.
            const bool all_required_blocks_found = (block_types_found & 0x1E) == 0x1E;
            if (!all_required_blocks_found) {
                ok_font_error(decoder->font, "Read error: error calling read function.");
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
                    ok_font_error(font, "Invalid info block");
                    return;
                }
                if (!ok_read(decoder, info_header, sizeof(info_header))) {
                    return;
                }
                // Get the font size, ignore the rest
                font->size = readLE16(info_header);
                
                // Get the font name
                font->name = malloc(name_buffer_length);
                if (font->name == NULL) {
                    ok_font_error(font, "Couldn't allocate font name");
                    return;
                }
                if (!ok_read(decoder, (uint8_t*)font->name, name_buffer_length)) {
                    return;
                }
                // Sanity check - make sure the string has a null-terminator
                font->name[name_buffer_length - 1] = 0;
                break;
            }
                
            case BLOCK_TYPE_COMMON: {
                uint8_t common[15];
                if (block_length != sizeof(common)) {
                    ok_font_error(font, "Invalid common block");
                    return;
                }
                if (!ok_read(decoder, common, sizeof(common))) {
                    return;
                }
                // Get the line height, base, and page count; ignore the rest
                font->line_height = readLE16(common);
                font->base = readLE16(common + 2);
                font->num_pages = readLE16(common + 8);
                break;
            }
                
            case BLOCK_TYPE_PAGES: {
                if (font->num_pages <= 0 || block_length == 0) {
                    ok_font_error(font, "Couldn't get page names");
                    return;
                }
                else {
                    font->page_names = calloc(font->num_pages, sizeof(char *));
                    if (font->page_names == NULL) {
                        font->num_pages = 0;
                        ok_font_error(font, "Couldn't allocate memory for page name array");
                        return;
                    }
                    // Load everything into the first item; setup pointers below.
                    font->page_names[0] = malloc(block_length);
                    if (font->page_names[0] == NULL) {
                        font->num_pages = 0;
                        ok_font_error(font, "Couldn't allocate memory for page names");
                        return;
                    }
                    if (!ok_read(decoder, (uint8_t*)font->page_names[0], block_length)) {
                        return;
                    }
                    char *pos = font->page_names[0];
                    char * const end_pos = pos + block_length;
                    // Sanity check - make sure there is a null terminator
                    *(end_pos - 1) = 0;
                    
                    // Set up pointers for each page name
                    int next_index = 1;
                    while (pos + 1 < end_pos && next_index < font->num_pages) {
                        if (*pos == 0) {
                            font->page_names[next_index] = pos + 1;
                            next_index++;
                        }
                        pos++;
                    }
                    // Sanity check - make sure the remaining page names, if any, point somewhere
                    for (int i = next_index; i < font->num_pages; i++) {
                        font->page_names[i] = end_pos - 1;
                    }
                }
                break;
            }
                
            case BLOCK_TYPE_CHARS: {
                uint8_t data[20];
                font->num_glyphs = block_length / sizeof(data);
                font->glyphs = malloc(font->num_glyphs * sizeof(ok_font_glyph));
                if (font->glyphs == NULL) {
                    font->num_glyphs = 0;
                    ok_font_error(font, "Couldn't allocate memory for glyphs");
                    return;
                }
                // On little-endian systems we could just load the entire block into memory, but we'll assume
                // the byte order is unknown here.
                for (int i = 0; i < font->num_glyphs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_font_glyph *glyph = &font->glyphs[i];
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
                font->num_kerning_pairs = block_length / sizeof(data);
                font->kerning_pairs = malloc(font->num_kerning_pairs * sizeof(ok_font_kerning));
                if (font->kerning_pairs == NULL) {
                    font->num_kerning_pairs = 0;
                    ok_font_error(font, "Couldn't allocate memory for kerning");
                    return;
                }
                // On little-endian systems we could just load the entire block into memory, but we'll assume
                // the byte order is unknown here.
                for (int i = 0; i < font->num_kerning_pairs; i++) {
                    if (!ok_read(decoder, data, sizeof(data))) {
                        return;
                    }
                    ok_font_kerning *kerning = &font->kerning_pairs[i];
                    kerning->first_char = readLE32(data);
                    kerning->second_char = readLE32(data + 4);
                    kerning->amount = readLE16(data + 8);
                }
                break;
            }
                
            default:
                ok_font_error(font, "Unknown block type: %i", block_type);
                return;
        }
    }
}

static void decode_fnt(ok_font *font, void *reader_data, ok_read_func read_func, ok_seek_func seek_func) {
    if (font == NULL) {
        return;
    }
    fnt_decoder *decoder = calloc(1, sizeof(fnt_decoder));
    if (decoder == NULL) {
        ok_font_error(font, "Couldn't allocate decoder.");
        return;
    }
    decoder->font = font;
    decoder->reader_data = reader_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;

    decode_fnt2(decoder);
    
    free(decoder);
}

