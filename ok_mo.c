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
#include "ok_mo.h"
#include <errno.h>
#include <memory.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

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
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;
    
} mo_decoder;

static void decode_mo2(mo_decoder *decoder);

static void ok_mo_cleanup(ok_mo *mo) {
    if (mo != NULL) {
        if (mo->strings != NULL) {
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

__attribute__((__format__ (__printf__, 2, 3)))
static void ok_mo_error(ok_mo *mo, const char *format, ... ) {
    if (mo != NULL) {
        ok_mo_cleanup(mo);
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(mo->error_message, sizeof(mo->error_message), format, args);
            va_end(args);
        }
    }
}

static void decode_mo(ok_mo *mo, void *reader_data, ok_read_func read_func, ok_seek_func seek_func) {
    if (mo == NULL) {
        return;
    }
    mo_decoder *decoder = calloc(1, sizeof(mo_decoder));
    if (decoder == NULL) {
        ok_mo_error(mo, "Couldn't allocate decoder.");
        return;
    }
    decoder->mo = mo;
    decoder->reader_data = reader_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;
    
    decode_mo2(decoder);
    
    if (decoder->key_offset_buffer != NULL) {
        free(decoder->key_offset_buffer);
    }
    if (decoder->value_offset_buffer != NULL) {
        free(decoder->value_offset_buffer);
    }
    free(decoder);
}

// MARK: Input helper functions

typedef struct {
    uint8_t *buffer;
    size_t remaining_bytes;
} ok_memory_source;

static size_t ok_memory_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    const size_t len = min(count, memory->remaining_bytes);
    if (len > 0) {
        memcpy(buffer, memory->buffer, len);
        memory->buffer += len;
        memory->remaining_bytes -= len;
        return len;
    }
    else {
        return 0;
    }
}

static int ok_memory_seek_func(void *user_data, const int count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    if ((size_t)count <= memory->remaining_bytes) {
        memory->buffer += count;
        memory->remaining_bytes -= count;
        return 0;
    }
    else {
        return -1;
    }
}

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    if (count > 0) {
        FILE *fp = (FILE *)user_data;
        return fread(buffer, 1, count, fp);
    }
    else {
        return 0;
    }
}

static int ok_file_seek_func(void *user_data, const int count) {
    if (count != 0) {
        FILE *fp = (FILE *)user_data;
        return fseek(fp, count, SEEK_CUR);
    }
    else {
        return 0;
    }
}

static bool ok_read(mo_decoder *decoder, uint8_t *data, const size_t length) {
    if (decoder->read_func(decoder->reader_data, data, length) == length) {
        return true;
    }
    else {
        ok_mo_error(decoder->mo, "Read error: error calling read function.");
        return false;
    }
}

static bool ok_seek(mo_decoder *decoder, const int length) {
    if (decoder->seek_func(decoder->reader_data, length) == 0) {
        return true;
    }
    else {
        ok_mo_error(decoder->mo, "Read error: error calling seek function.");
        return false;
    }
}

// MARK: Public API

ok_mo *ok_mo_read(const char *file_name) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (file_name != NULL) {
        FILE *fp = fopen(file_name, "rb");
        if (fp != NULL) {
            decode_mo(mo, fp, ok_file_read_func, ok_file_seek_func);
            fclose(fp);
        }
        else {
            ok_mo_error(mo, "%s", strerror(errno));
        }
    }
    else {
        ok_mo_error(mo, "Invalid argument: file_name is NULL");
    }
    return mo;
}

ok_mo *ok_mo_read_from_file(FILE *fp) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (fp != NULL) {
        decode_mo(mo, fp, ok_file_read_func, ok_file_seek_func);
    }
    else {
        ok_mo_error(mo, "Invalid argument: file is NULL");
    }
    return mo;
}

ok_mo *ok_mo_read_from_memory(const void *buffer, const size_t buffer_length) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (buffer != NULL) {
        ok_memory_source memory;
        memory.buffer = (uint8_t *)buffer;
        memory.remaining_bytes = buffer_length;
        decode_mo(mo, &memory, ok_memory_read_func, ok_memory_seek_func);
    }
    else {
        ok_mo_error(mo, "Invalid argument: buffer is NULL");
    }
    return mo;
}

ok_mo *ok_mo_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func) {
    ok_mo *mo = calloc(1, sizeof(ok_mo));
    if (read_func != NULL && seek_func != NULL) {
        decode_mo(mo, user_data, read_func, seek_func);
    }
    else {
        ok_mo_error(mo, "Invalid argument: read_func or seek_func is NULL");
    }
    return mo;
}

void ok_mo_free(ok_mo *mo) {
    if (mo != NULL) {
        ok_mo_cleanup(mo);
        free(mo);
    }
}

// MARK: Decoding

static inline uint16_t read16(const uint8_t *data, const bool little_endian) {
    if (little_endian) {
        return (uint16_t)((data[1] << 8) | data[0]);
    }
    else {
        return (uint16_t)((data[0] << 8) | data[1]);
    }
}

static inline uint32_t read32(const uint8_t *data, const bool little_endian) {
    if (little_endian) {
        return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
    }
    else {
        return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    }
}

static void decode_mo2(mo_decoder *decoder) {
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
    }
    else if (magic == 0xde120495) {
        little_endian = false;
    }
    else {
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
        ok_mo_error(mo, "Not a gettext MO file (version %d)", major_version);
        return;
    }
    
    if (mo->num_strings == 0) {
        ok_mo_error(mo, "No strings found");
        return;
    }
    
    mo->strings = calloc(mo->num_strings, sizeof(struct ok_mo_string));
    decoder->key_offset_buffer = malloc(8 * mo->num_strings);
    decoder->value_offset_buffer = malloc(8 * mo->num_strings);
    if (mo->strings == NULL || decoder->key_offset_buffer == NULL || decoder->value_offset_buffer == NULL) {
        ok_mo_error(mo, "Couldn't allocate arrays");
        return;
    }
    
    // Read offsets and lengths
    // Using "tell" because the seek functions only support relative seeking.
    int tell = sizeof(header);
    if (!ok_seek(decoder, key_offset - tell)) {
        return;
    }
    if (!ok_read(decoder, decoder->key_offset_buffer, 8 * mo->num_strings)) {
        ok_mo_error(mo, "Couldn't get key offsets");
        return;
    }
    tell = key_offset + 8 * mo->num_strings;
    if (!ok_seek(decoder, value_offset - tell)) {
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
        if (mo->strings[i].key == NULL) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, offset - tell)) {
            return;
        }
        if (!ok_read(decoder, (uint8_t*)mo->strings[i].key, length + 1)) {
            return;
        }
        tell = offset + length + 1;
    }
    
    // Read values
    for (uint32_t i = 0; i < mo->num_strings; i++) {
        uint32_t length = read32(decoder->value_offset_buffer + 8 * i, little_endian);
        uint32_t offset = read32(decoder->value_offset_buffer + 8 * i + 4, little_endian);
        
        mo->strings[i].value = malloc(length + 1);
        if (mo->strings[i].value == NULL) {
            ok_mo_error(mo, "Couldn't allocate strings");
            return;
        }
        if (!ok_seek(decoder, offset - tell)) {
            return;
        }
        if (!ok_read(decoder, (uint8_t*)mo->strings[i].value, length + 1)) {
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

static int bsearch_strcmp(const void *s1, const void *s2) {
    const char *key = s1;
    const struct ok_mo_string * elem = s2;
    return strcmp(key, (elem)->key);
}

static struct ok_mo_string *find_value(ok_mo *mo, const char *context, const char *key) {
    if (mo == NULL || key == NULL) {
        return NULL;
    }
    else if (context == NULL) {
        return bsearch(key, mo->strings, mo->num_strings, sizeof(mo->strings[0]), bsearch_strcmp);
    }
    else {
        // Complete key is (context + EOT + key)
        const size_t context_length = strlen(context);
        const size_t complete_key_length = context_length + 1 + strlen(key) + 1;
        char * complete_key = malloc(complete_key_length);
        if (complete_key == NULL) {
            return NULL;
        }
        strcpy(complete_key, context);
        complete_key[context_length] = 4; // EOT
        strcpy(complete_key + context_length + 1, key);
        complete_key[complete_key_length] = 0;
        struct ok_mo_string *r = bsearch(complete_key, mo->strings, mo->num_strings,
                                         sizeof(mo->strings[0]), bsearch_strcmp);
        free(complete_key);
        return r;
    }
}

const char *ok_mo_value(ok_mo *mo, const char *key) {
    return ok_mo_value_in_context(mo, NULL, key);
}

const char *ok_mo_plural_value(ok_mo *mo, const char *key, const char *plural_key, const int n) {
    return ok_mo_plural_value_in_context(mo, NULL, key, plural_key, n);
}

const char *ok_mo_value_in_context(ok_mo *mo, const char *context, const char *key) {
    struct ok_mo_string *s = find_value(mo, context, key);
    if (s == NULL) {
        return key;
    }
    else {
        return s->value;
    }
}

static int get_plural_index(const int num_variants, const int n) {
    // This is probably too simple for some languages
    return n <= 0 ? num_variants : min(n-1, num_variants);
}

const char *ok_mo_plural_value_in_context(ok_mo *mo, const char *context, const char *key, const char *plural_key,
                                          const int n) {
    struct ok_mo_string *s = find_value(mo, context, key);
    if (s == NULL) {
        if (get_plural_index(1, n) == 0) {
            return key;
        }
        else {
            return plural_key;
        }
    }
    else {
        // This is probably too simple for some languages
        const int plural_index = get_plural_index(s->num_plural_variants, n);
        const char *v = s->value;
        for (int i = 0; i < plural_index; i++) {
            while (*v++ != 0) { }
        }
        return v;
    }
}

// MARK: Unicode

size_t ok_utf8_strlen(const char *utf8) {
    // Might consider faster version of this if needed.
    // See http://www.daemonology.net/blog/2008-06-05-faster-utf8-strlen.html
    size_t len = 0;
    if (utf8 != NULL) {
        const unsigned char *in = (const unsigned char *)utf8;
        while (*in != 0) {
            int skip;
            if (*in < 0xc0) {
                skip = 0;
            }
            else if (*in < 0xe0) {
                skip = 1;
            }
            else if (*in < 0xf0) {
                skip = 2;
            }
            else {
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

size_t ok_utf8_to_unicode(const char *utf8, uint32_t *dest, const size_t n) {
    if (utf8 == NULL || dest == NULL || n == 0) {
        return 0;
    }
    
    const unsigned char *in = (const unsigned char *)utf8;
    size_t len = 0;
    while (len < n && *in != 0) {
        if (*in < 0xc0) {
            dest[len] = in[0];
            in++;
        }
        else if (*in < 0xe0) {
            dest[len] = ((in[0] & 0x1f) << 6) | (in[1] & 0x3f);
            in += 2;
        }
        else if (*in < 0xf0) {
            dest[len] = ((in[0] & 0x0f) << 12) | ((in[1] & 0x3f) << 6) | (in[2] & 0x3f);
            in += 3;
        }
        else {
            dest[len] = ((in[0] & 0x07) << 18) | ((in[1] & 0x3f) << 6) | ((in[2] & 0x3f) << 6) | (in[3] & 0x3f);
            in += 4;
        }
        len++;
    }
    dest[len] = 0;
    return len;
}
