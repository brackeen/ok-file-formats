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
#include "ok_csv.h"
#include <memory.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// MARK: Circular buffer

typedef struct {
    uint8_t *data;
    size_t capacity;
    size_t start;
    size_t length;
} circular_buffer;

bool circular_buffer_init(circular_buffer *buffer, const int capacity) {
    buffer->start = 0;
    buffer->length = 0;
    buffer->data = malloc(capacity);
    if (buffer->data != NULL) {
        buffer->capacity = capacity;
        return true;
    }
    else {
        buffer->capacity = 0;
        return false;
    }
}

// Number of writable elements until edge of buffer
size_t circular_buffer_writable(circular_buffer *buffer) {
    size_t total_writable = buffer->capacity - buffer->length;
    return min(total_writable, buffer->capacity - ((buffer->start + buffer->length) % buffer->capacity));
}

// Number of readable elements until edge of buffer
size_t circular_buffer_readable(circular_buffer *buffer) {
    return min(buffer->length, buffer->capacity - buffer->start);
}

// Doubles the size of the buffer
bool circular_buffer_expand(circular_buffer *buffer) {
    size_t new_capacity = buffer->capacity * 2;
    uint8_t *new_data = malloc(new_capacity);
    if (new_data == NULL) {
        return false;
    }
    else {
        const size_t readable1 = circular_buffer_readable(buffer);
        const size_t readable2 = buffer->length - readable1;
        memcpy(new_data, buffer->data + buffer->start, readable1);
        memcpy(new_data + readable1, buffer->data, readable2);
        free(buffer->data);
        buffer->data = new_data;
        buffer->capacity = new_capacity;
        buffer->start = 0;
        return true;
    }
}

bool circular_buffer_read(circular_buffer *buffer, uint8_t *dst, const size_t length) {
    if (length > buffer->length) {
        return false;
    }
    else {
        const size_t readable1 = circular_buffer_readable(buffer);
        if (length <= readable1) {
            memcpy(dst, buffer->data + buffer->start, length);
        }
        else {
            const size_t readable2 = buffer->length - readable1;
            memcpy(dst, buffer->data + buffer->start, readable1);
            memcpy(dst + readable1, buffer->data, readable2);
        }
        buffer->start = (buffer->start + length) % buffer->capacity;
        buffer->length -= length;
        return true;
    }
}

bool circular_buffer_skip(circular_buffer *buffer, const size_t length) {
    if (length > buffer->length) {
        return false;
    }
    else {
        buffer->start = (buffer->start + length) % buffer->capacity;
        buffer->length -= length;
        return true;
    }
}

// MARK: CSV Helper functions

typedef struct {
    ok_csv *csv;

    // The circular buffer is expanded if needed (for example, a field is larger than 4K)
    circular_buffer input_buffer;
    
    // Input
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;
    
} csv_decoder;

static void decode_csv(ok_csv *csv, void *reader_data, ok_read_func read_func, ok_seek_func seek_func);
static void decode_csv2(csv_decoder *decoder);

static void ok_csv_cleanup(ok_csv *csv) {
    if (csv != NULL) {
        if (csv->fields != NULL && csv->num_fields != NULL) {
            for (int i = 0; i < csv->num_records; i++) {
                for (int j = 0; j < csv->num_fields[i]; j++) {
                    free(csv->fields[i][j]);
                }
                free(csv->fields[i]);
            }
            free(csv->fields);
            free(csv->num_fields);
            csv->fields = NULL;
            csv->num_fields = NULL;
        }
        csv->num_records = 0;
    }
}

__attribute__((__format__ (__printf__, 2, 3)))
static void ok_csv_error(ok_csv *csv, const char *format, ... ) {
    if (csv != NULL) {
        ok_csv_cleanup(csv);
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(csv->error_message, sizeof(csv->error_message), format, args);
            va_end(args);
        }
    }
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

// MARK: Public API

ok_csv *ok_csv_read(const char *file_name) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (file_name != NULL) {
        FILE *fp = fopen(file_name, "rb");
        if (fp != NULL) {
            decode_csv(csv, fp, ok_file_read_func, ok_file_seek_func);
            fclose(fp);
        }
        else {
            ok_csv_error(csv, "%s", strerror(errno));
        }
    }
    else {
        ok_csv_error(csv, "Invalid argument: file_name is NULL");
    }
    return csv;
}

ok_csv *ok_csv_read_from_file(FILE *fp) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (fp != NULL) {
        decode_csv(csv, fp, ok_file_read_func, ok_file_seek_func);
    }
    else {
        ok_csv_error(csv, "Invalid argument: file is NULL");
    }
    return csv;
}

ok_csv *ok_csv_read_from_memory(const void *buffer, const size_t buffer_length) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (buffer != NULL) {
        ok_memory_source memory;
        memory.buffer = (uint8_t *)buffer;
        memory.remaining_bytes = buffer_length;
        decode_csv(csv, &memory, ok_memory_read_func, ok_memory_seek_func);
    }
    else {
        ok_csv_error(csv, "Invalid argument: buffer is NULL");
    }
    return csv;
}

ok_csv *ok_csv_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (read_func != NULL && seek_func != NULL) {
        decode_csv(csv, user_data, read_func, seek_func);
    }
    else {
        ok_csv_error(csv, "Invalid argument: read_func or seek_func is NULL");
    }
    return csv;
}

void ok_csv_free(ok_csv *csv) {
    if (csv != NULL) {
        ok_csv_cleanup(csv);
        free(csv);
    }
}

// MARK: Decoding

static const int min_record_capacity = 16;
static const int min_field_capacity = 16;
static const int default_input_buffer_capacity = 4096;

typedef enum {
    RECORD_START = 0, // About to add a new record. Ignore if next char is cr, lf, or eof
    FIELD_START,      // About to add a new field (could be a blank field if next char is a comma, cr, or lf)
    ESCAPED_FIELD,    // Parsing an escaped field (stop when a lone double quote is found)
    NONESCAPED_FIELD, // Parsing a non-escaped field (stop when a comma, cr, lf, or eof is found)
} csv_decoder_state;

static void decode_csv(ok_csv *csv, void *reader_data, ok_read_func read_func, ok_seek_func seek_func) {
    if (csv == NULL) {
        return;
    }
    csv_decoder *decoder = calloc(1, sizeof(csv_decoder));
    if (decoder == NULL) {
        ok_csv_error(csv, "Couldn't allocate decoder.");
        return;
    }
    if (!circular_buffer_init(&decoder->input_buffer, default_input_buffer_capacity)) {
        free(decoder);
        ok_csv_error(csv, "Couldn't allocate input buffer.");
        return;
    }
    decoder->csv = csv;
    decoder->reader_data = reader_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;
    
    decode_csv2(decoder);
    
    free(decoder);
}

// Ensure capacity for at least (csv->num_records + 1) records
static bool csv_ensure_record_capcity(ok_csv *csv) {
    
    // curr_capacity is >= csv->num_records
    int curr_capacity = min_record_capacity;
    while (curr_capacity < csv->num_records) {
        curr_capacity <<= 1;
    }
    
    // new_capacity is >= (csv->num_records + 1)
    int new_capacity = curr_capacity;
    if (new_capacity < csv->num_records + 1) {
        new_capacity <<= 1;
    }
    
    if (csv->num_fields == NULL || curr_capacity < new_capacity) {
        int *new_num_fields = malloc(sizeof(int) * new_capacity);
        char ***new_fields = malloc(sizeof(char**) * new_capacity);
        if (new_num_fields == NULL || new_fields == NULL) {
            ok_csv_error(csv, "Couldn't allocate fields array");
            return false;
        }
        if (csv->num_fields != NULL) {
            memcpy(new_num_fields, csv->num_fields, sizeof(int) * csv->num_records);
            free(csv->num_fields);
        }
        csv->num_fields = new_num_fields;
        if (csv->fields != NULL) {
            memcpy(new_fields, csv->fields, sizeof(char**) * csv->num_records);
            free(csv->fields);
        }
        csv->fields = new_fields;
    }
    return true;
}

// Ensure capacity for at least (csv->num_fields[record] + 1) fields
static bool csv_ensure_field_capcity(ok_csv *csv, const int record) {
    // curr_capacity is >= csv->num_fields[record]
    int curr_capacity = min_field_capacity;
    while (curr_capacity < csv->num_fields[record]) {
        curr_capacity <<= 1;
    }
    
    // new_capacity is >= (csv->num_fields[record] + 1)
    int new_capacity = curr_capacity;
    if (new_capacity < csv->num_fields[record] + 1) {
        new_capacity <<= 1;
    }
    
    if (csv->fields[record] == NULL || curr_capacity < new_capacity) {
        char **new_fields = malloc(sizeof(char*) * new_capacity);
        if (new_fields == NULL) {
            ok_csv_error(csv, "Couldn't allocate fields array");
            return false;
        }
        if (csv->fields[record] != NULL) {
            memcpy(new_fields, csv->fields[record], sizeof(char*) * csv->num_fields[record]);
            free(csv->fields[record]);
        }
        csv->fields[record] = new_fields;
    }
    return true;
}

static void decode_csv2(csv_decoder *decoder) {
    ok_csv *csv = decoder->csv;
    int peek = 0;
    uint8_t prev_char = 0;
    csv_decoder_state state = RECORD_START;
    bool is_eof = false;
    
    while (true) {
        // Read data if needed
        if (decoder->input_buffer.length - peek == 0) {
            size_t writeable = circular_buffer_writable(&decoder->input_buffer);
            if (writeable == 0) {
                circular_buffer_expand(&decoder->input_buffer);
                writeable = circular_buffer_writable(&decoder->input_buffer);
            }
            uint8_t *end = decoder->input_buffer.data +
            ((decoder->input_buffer.start + decoder->input_buffer.length) % decoder->input_buffer.capacity);
            size_t bytesRead = decoder->read_func(decoder->reader_data, end, writeable);
            decoder->input_buffer.length += bytesRead;
        }
        
        // Peek current char (0 if EOF)
        uint8_t curr_char = 0;
        if (decoder->input_buffer.length - peek > 0) {
            size_t offset = (decoder->input_buffer.start + peek) % decoder->input_buffer.capacity;
            curr_char = decoder->input_buffer.data[offset];
            peek++;
        }
        else {
            is_eof = true;
        }
        
        switch (state) {
            case RECORD_START: default:
            {
                if (is_eof) {
                    // Do nothing
                    return;
                }
                else if (curr_char == '\n' && prev_char == '\r') {
                    // Second char in CRLF sequence, ignore
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                }
                else if (curr_char == '\"') {
                    // Add new record
                    csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;
                    
                    // Prep for escaped field
                    state = ESCAPED_FIELD;
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                }
                else if (curr_char == ',' || curr_char == '\r' || curr_char == '\n') {
                    // Add new record
                    int curr_record = csv->num_records;
                    csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;
                    
                    // Add blank field
                    char *blank_field = malloc(1);
                    if (blank_field == NULL) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    blank_field[0] = 0;
                    csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][0] = blank_field;
                    csv->num_fields[curr_record] = 1;
                    
                    if (curr_char == ',') {
                        state = FIELD_START;
                    }
                    else {
                        state = RECORD_START;
                    }
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                }
                else {
                    // Add new record
                    csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;
                    
                    // Prep for nonescaped field
                    state = NONESCAPED_FIELD;
                }
                break;
            }
            case FIELD_START:
            {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    // Add blank field
                    char *blank_field = malloc(1);
                    if (blank_field == NULL) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    blank_field[0] = 0;
                    int curr_record = csv->num_records - 1;
                    csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = blank_field;
                    csv->num_fields[curr_record]++;
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                    if (curr_char == ',') {
                        state = FIELD_START;
                    }
                    else {
                        state = RECORD_START;
                    }
                }
                else if (curr_char == '\"') {
                    state = ESCAPED_FIELD;
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                }
                else {
                    state = NONESCAPED_FIELD;
                }
                break;
            }
            case ESCAPED_FIELD:
            {
                if (is_eof || (prev_char == '\"' && peek > 1 && (curr_char == ',' || curr_char == '\r' || curr_char == '\n'))) {
                    if (prev_char == '\"') {
                        peek--;
                    }
                    char *field = malloc(peek + 1);
                    if (field == NULL) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    char *field_ptr = field;
                    
                    bool dquote_escape = false;
                    while (peek > 0) {
                        char ch = decoder->input_buffer.data[decoder->input_buffer.start];
                        if (ch == '\"') {
                            if (dquote_escape) {
                                *field_ptr++ = '\"';
                                dquote_escape = false;
                            }
                            else {
                                dquote_escape = true;
                            }
                        }
                        else if (dquote_escape) {
                            // Shouldn't happen on welformed CSV files
                            *field_ptr++ = '\"';
                            dquote_escape = false;
                            *field_ptr++ = ch;
                        }
                        else {
                            *field_ptr++ = ch;
                        }
                        peek--;
                        circular_buffer_skip(&decoder->input_buffer, 1);
                    }
                    *field_ptr++ = 0;
                            
                    if (prev_char == '\"') {
                        // Skip closing dquote
                        circular_buffer_skip(&decoder->input_buffer, 1);
                    }
                    
                    int curr_record = csv->num_records - 1;
                    csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = field;
                    csv->num_fields[curr_record]++;
                    
                    if (curr_char == ',') {
                        state = FIELD_START;
                    }
                    else if (curr_char == '\r' || curr_char == '\n') {
                        state = RECORD_START;
                    }
                }
                break;
            }
            case NONESCAPED_FIELD:
            {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    char *field = malloc(peek);
                    if (field == NULL) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    circular_buffer_read(&decoder->input_buffer, (unsigned char *)field, peek - 1);
                    circular_buffer_skip(&decoder->input_buffer, 1);
                    field[peek - 1] = 0;
                    peek = 0;
                    
                    int curr_record = csv->num_records - 1;
                    csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = field;
                    csv->num_fields[curr_record]++;
                    
                    if (curr_char == ',') {
                        state = FIELD_START;
                    }
                    else if (curr_char == '\r' || curr_char == '\n') {
                        state = RECORD_START;
                    }
                }
                break;
            }
        }
        prev_char = curr_char;
        if (is_eof) {
            // All done
            return;
        }
    }
}
