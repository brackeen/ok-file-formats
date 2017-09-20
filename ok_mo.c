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

#include "ok_mo.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// See https://www.gnu.org/software/gettext/manual/html_node/MO-Files.html

// MARK: MO helper functions

struct ok_mo_string {
    char *key;
    char *value;
    int num_plural_variants;
};

typedef struct {
    ok_mo *mo;

    uint8_t *key_offset_buffer;
    uint8_t *value_offset_buffer;

    // Input
    void *input_data;
    ok_mo_read_func input_read_func;
    ok_mo_seek_func input_seek_func;

} ok_mo_decoder;

static void ok_mo_decode2(ok_mo_decoder *decoder);

static void ok_mo_cleanup(ok_mo *mo) {
    if (mo) {
        if (mo->strings) {
            for (uint32_t i = 0; i < mo->num_strings; i++) {
                free(mo->strings[i].key);
                free(mo->strings[i].value);
            }
            free(mo->strings);
            mo->strings = NULL;
        }
        mo->num_strings = 0;
    }
}

#ifdef NDEBUG
#define ok_mo_error(mo, message) ok_mo_set_error((mo), "ok_mo_error")
#else
#define ok_mo_error(mo, message) ok_mo_set_error((mo), (message))
#endif

static void ok_mo_set_error(ok_mo *mo, const char *message) {
    if (mo) {
        ok_mo_cleanup(mo);
        mo->error_message = message;
    }
}

static void ok_mo_decode(ok_mo *mo, void *input_data, ok_mo_read_func input_read_func,
                         ok_mo_seek_func input_seek_func) {
    if (mo) {
        ok_mo_decoder *decoder = calloc(1, sizeof(ok_mo_decoder));
        if (!decoder) {
            ok_mo_error(mo, "Couldn't allocate decoder.");
            return;
        }
        decoder->mo = mo;
        decoder->input_data = input_data;
        decoder->input_read_func = input_read_func;
        decoder->input_seek_func = input_seek_func;

        ok_mo_decode2(decoder);

        if (decoder->key_offset_buffer) {
            free(decoder->key_offset_buffer);
        }
        if (decoder->value_offset_buffer) {
            free(decoder->value_offset_buffer);
        }
        free(decoder);
    }
}

static bool ok_read(ok_mo_decoder *decoder, uint8_t *buffer, size_t length) {
    if (decoder->input_read_func(decoder->input_data, buffer, length) == length) {
        return true;
    } else {
        ok_mo_error(decoder->mo, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(ok_mo_decoder *decoder, long length) {
    if (decoder->input_seek_func(decoder->input_data, length)) {
        return true;
    } else {
        ok_mo_error(decoder->mo, "Seek error: error calling input function.");
        return false;
    }
}

#ifndef OK_NO_STDIO

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, size_t length) {
    return fread(buffer, 1, length, (FILE *)user_data);
}

static bool ok_file_seek_func(void *user_data, long count) {
    return fseek((FILE *)user_data, count, SEEK_CUR) == 0;
}

#endif

// MARK: Public API

#ifndef OK_NO_STDIO

ok_mo *ok_mo_read(FILE *file) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (file) {
        ok_mo_decode(mo, file, ok_file_read_func, ok_file_seek_func);
    } else {
        ok_mo_error(mo, "File not found");
    }
    return mo;
}

#endif

ok_mo *ok_mo_read_from_callbacks(void *user_data, ok_mo_read_func read_func,
                                 ok_mo_seek_func seek_func) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (read_func && seek_func) {
        ok_mo_decode(mo, user_data, read_func, seek_func);
    } else {
        ok_mo_error(mo, "Invalid argument: read_func and seek_func must not be NULL");
    }
    return mo;
}

void ok_mo_free(ok_mo *mo) {
    if (mo) {
        ok_mo_cleanup(mo);
        free(mo);
    }
}

// MARK: Decoding

static inline uint16_t read16(const uint8_t *data, bool little_endian) {
    if (little_endian) {
        return (uint16_t)((data[1] << 8) | data[0]);
    } else {
        return (uint16_t)((data[0] << 8) | data[1]);
    }
}

static inline uint32_t read32(const uint8_t *data, bool little_endian) {
    if (little_endian) {
        return (((uint32_t)data[3] << 24) |
                ((uint32_t)data[2] << 16) |
                ((uint32_t)data[1] << 8) |
                ((uint32_t)data[0] << 0));
    } else {
        return (((uint32_t)data[0] << 24) |
                ((uint32_t)data[1] << 16) |
                ((uint32_t)data[2] << 8) |
                ((uint32_t)data[3] << 0));
    }
}

static void ok_mo_decode2(ok_mo_decoder *decoder) {
    ok_mo *mo = decoder->mo;
    uint8_t header[20];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }

    // Magic number
    uint32_t magic = read32(header, true);
    bool little_endian;
    if (magic == 0x950412de) {
        little_endian = true;
    } else if (magic == 0xde120495) {
        little_endian = false;
    } else {
        ok_mo_error(mo, "Not a gettext MO file");
        return;
    }

    // Header
    const uint16_t major_version = read16(header + 4, little_endian);
    //const uint16_t minor_version = read16(header + 6, little_endian); // ignore minor_version
    mo->num_strings = read32(header + 8, little_endian);
    const uint32_t key_offset = read32(header + 12, little_endian);
    const uint32_t value_offset = read32(header + 16, little_endian);

    if (!(major_version == 0 || major_version == 1)) {
        ok_mo_error(mo, "Unsupported gettext MO file. Only version 0 or 1 supported");
        return;
    }

    if (mo->num_strings == 0) {
        ok_mo_error(mo, "No strings found");
        return;
    }

    mo->strings = calloc(mo->num_strings, sizeof(struct ok_mo_string));
    decoder->key_offset_buffer = malloc(8 * mo->num_strings);
    decoder->value_offset_buffer = malloc(8 * mo->num_strings);
    if (!mo->strings || !decoder->key_offset_buffer || !decoder->value_offset_buffer) {
        ok_mo_error(mo, "Couldn't allocate arrays");
        return;
    }

    // Read offsets and lengths
    // Using "tell" because the seek functions only support relative seeking.
    size_t tell = sizeof(header);
    if (!ok_seek(decoder, (long)(key_offset - tell))) {
        return;
    }
    if (!ok_read(decoder, decoder->key_offset_buffer, 8 * mo->num_strings)) {
        ok_mo_error(mo, "Couldn't get key offsets");
        return;
    }
    tell = key_offset + 8 * mo->num_strings;
    if (!ok_seek(decoder, (long)(value_offset - tell))) {
        return;
    }
    if (!ok_read(decoder, decoder->value_offset_buffer, 8 * mo->num_strings)) {
        ok_mo_error(mo, "Couldn't get value offsets");
        return;
    }
    tell = value_offset + 8 * mo->num_strings;

    // Read keys
    // Assumes keys are sorted, per the spec.
    for (uint32_t i = 0; i < mo->num_strings; i++) {
        uint32_t length = read32(decoder->key_offset_buffer + 8 * i, little_endian);
        uint32_t offset = read32(decoder->key_offset_buffer + 8 * i + 4, little_endian);

        mo->strings[i].key = malloc(length + 1);
        if (!mo->strings[i].key) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, (long)(offset - tell))) {
            return;
        }
        if (!ok_read(decoder, (uint8_t *)mo->strings[i].key, length + 1)) {
            return;
        }
        tell = offset + length + 1;
    }

    // Read values
    for (uint32_t i = 0; i < mo->num_strings; i++) {
        uint32_t length = read32(decoder->value_offset_buffer + 8 * i, little_endian);
        uint32_t offset = read32(decoder->value_offset_buffer + 8 * i + 4, little_endian);

        mo->strings[i].value = malloc(length + 1);
        if (!mo->strings[i].value) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, (long)(offset - tell))) {
            return;
        }
        if (!ok_read(decoder, (uint8_t *)mo->strings[i].value, length + 1)) {
            return;
        }
        // Count the zeros. It is the number of plural variants.
        mo->strings[i].num_plural_variants = 0;
        const char *ch = mo->strings[i].value;
        const char *end = mo->strings[i].value + length;
        while (ch < end) {
            if (*ch++ == 0) {
                mo->strings[i].num_plural_variants++;
            }
        }
        tell = offset + length + 1;
    }
}

// MARK: Getters

static int ok_mo_bsearch_strcmp(const void *s1, const void *s2) {
    const char *key = s1;
    const struct ok_mo_string *elem = s2;
    return strcmp(key, (elem)->key);
}

static struct ok_mo_string *ok_mo_find_value(ok_mo *mo, const char *context, const char *key) {
    if (!mo || !key) {
        return NULL;
    } else if (!context) {
        return bsearch(key, mo->strings, mo->num_strings, sizeof(mo->strings[0]),
                       ok_mo_bsearch_strcmp);
    } else {
        // Complete key is (context + EOT + key)
        const size_t context_length = strlen(context);
        const size_t complete_key_length = context_length + 1 + strlen(key) + 1;
        char *complete_key = malloc(complete_key_length);
        if (!complete_key) {
            return NULL;
        }
        strcpy(complete_key, context);
        complete_key[context_length] = 4; // EOT
        strcpy(complete_key + context_length + 1, key);
        complete_key[complete_key_length - 1] = 0;
        struct ok_mo_string *r = bsearch(complete_key, mo->strings, mo->num_strings,
                                         sizeof(mo->strings[0]), ok_mo_bsearch_strcmp);
        free(complete_key);
        return r;
    }
}

const char *ok_mo_value(ok_mo *mo, const char *key) {
    return ok_mo_value_in_context(mo, NULL, key);
}

const char *ok_mo_plural_value(ok_mo *mo, const char *key, const char *plural_key, int n) {
    return ok_mo_plural_value_in_context(mo, NULL, key, plural_key, n);
}

const char *ok_mo_value_in_context(ok_mo *mo, const char *context, const char *key) {
    struct ok_mo_string *s = ok_mo_find_value(mo, context, key);
    return s ? s->value : key;
}

static int ok_mo_get_plural_index(const int num_variants, const int n) {
    // This is probably too simple for some languages
    return n <= 0 ? num_variants : min(n - 1, num_variants);
}

const char *ok_mo_plural_value_in_context(ok_mo *mo, const char *context, const char *key,
                                          const char *plural_key, int n) {
    struct ok_mo_string *s = ok_mo_find_value(mo, context, key);
    if (s) {
        // This is probably too simple for some languages
        const int plural_index = ok_mo_get_plural_index(s->num_plural_variants, n);
        const char *v = s->value;
        for (int i = 0; i < plural_index; i++) {
            while (*v++ != 0) {
                // Skip
            }
        }
        return v;
    } else {
        if (ok_mo_get_plural_index(1, n) == 0) {
            return key;
        } else {
            return plural_key;
        }
    }
}

// MARK: Unicode

unsigned int ok_utf8_strlen(const char *utf8) {
    // Might consider faster version of this if needed.
    // See http://www.daemonology.net/blog/2008-06-05-faster-utf8-strlen.html
    unsigned int len = 0;
    if (utf8) {
        const unsigned char *in = (const unsigned char *)utf8;
        while (*in != 0) {
            int skip;
            if (*in < 0xc0) {
                skip = 0;
            } else if (*in < 0xe0) {
                skip = 1;
            } else if (*in < 0xf0) {
                skip = 2;
            } else {
                skip = 3;
            }
            // Sanity check: check for malformed string
            for (int i = 0; i < skip; i++) {
                in++;
                if (*in < 128) {
                    break;
                }
            }
            len++;
            in++;
        }
    }
    return len;
}

unsigned int ok_utf8_to_unicode(const char *utf8, uint32_t *dest, unsigned int n) {
    if (!utf8 || !dest || n == 0) {
        return 0;
    }

    const unsigned char *in = (const unsigned char *)utf8;
    unsigned int len = 0;
    while (len < n && *in != 0) {
        if (*in < 0xc0) {
            dest[len] = in[0];
            in++;
        } else if (*in < 0xe0) {
            dest[len] = ((in[0] & 0x1fu) << 6) | (in[1] & 0x3fu);
            in += 2;
        } else if (*in < 0xf0) {
            dest[len] = ((in[0] & 0x0fu) << 12) | ((in[1] & 0x3fu) << 6) | (in[2] & 0x3fu);
            in += 3;
        } else {
            dest[len] = ((in[0] & 0x07u) << 18) | ((in[1] & 0x3fu) << 6) | ((in[2] & 0x3fu) << 6) |
                (in[3] & 0x3f);
            in += 4;
        }
        len++;
    }
    dest[len] = 0;
    return len;
}
