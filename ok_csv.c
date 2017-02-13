/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen

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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// MARK: Circular buffer

typedef struct {
    uint8_t *data;
    int capacity;
    int start;
    int length;
} circular_buffer;

static bool circular_buffer_init(circular_buffer *buffer, int capacity) {
    buffer->start = 0;
    buffer->length = 0;
    buffer->data = malloc(capacity);
    if (buffer->data) {
        buffer->capacity = capacity;
        return true;
    } else {
        buffer->capacity = 0;
        return false;
    }
}

// Number of writable elements until edge of buffer
static int circular_buffer_writable(circular_buffer *buffer) {
    int total_writable = buffer->capacity - buffer->length;
    return min(total_writable,
               buffer->capacity - ((buffer->start + buffer->length) % buffer->capacity));
}

// Number of readable elements until edge of buffer
static int circular_buffer_readable(circular_buffer *buffer) {
    return min(buffer->length, buffer->capacity - buffer->start);
}

// Doubles the size of the buffer
static bool circular_buffer_expand(circular_buffer *buffer) {
    int new_capacity = buffer->capacity * 2;
    uint8_t *new_data = malloc(new_capacity);
    if (!new_data) {
        return false;
    } else {
        const int readable1 = circular_buffer_readable(buffer);
        const int readable2 = buffer->length - readable1;
        memcpy(new_data, buffer->data + buffer->start, readable1);
        memcpy(new_data + readable1, buffer->data, readable2);
        free(buffer->data);
        buffer->data = new_data;
        buffer->capacity = new_capacity;
        buffer->start = 0;
        return true;
    }
}

static bool circular_buffer_read(circular_buffer *buffer, uint8_t *dst, int length) {
    if (length > buffer->length) {
        return false;
    } else {
        const int readable1 = circular_buffer_readable(buffer);
        if (length <= readable1) {
            memcpy(dst, buffer->data + buffer->start, length);
        } else {
            const int readable2 = buffer->length - readable1;
            memcpy(dst, buffer->data + buffer->start, readable1);
            memcpy(dst + readable1, buffer->data, readable2);
        }
        buffer->start = (buffer->start + length) % buffer->capacity;
        buffer->length -= length;
        return true;
    }
}

static bool circular_buffer_skip(circular_buffer *buffer, int length) {
    if (length > buffer->length) {
        return false;
    } else {
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
    void *input_data;
    ok_csv_read_func input_read_func;

} csv_decoder;

static void decode_csv(ok_csv *csv, void *input_data, ok_csv_read_func input_read_func);
static void decode_csv2(csv_decoder *decoder);

static void ok_csv_cleanup(ok_csv *csv) {
    if (csv) {
        if (csv->fields && csv->num_fields) {
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

static void ok_csv_error(ok_csv *csv, const char *message) {
    if (csv) {
        ok_csv_cleanup(csv);
        const size_t len = sizeof(csv->error_message) - 1;
        strncpy(csv->error_message, message, len);
        csv->error_message[len] = 0;
    }
}


#ifndef OK_NO_STDIO

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, size_t length) {
    return fread(buffer, 1, length, (FILE *)user_data);
}

#endif

// MARK: Public API

#ifndef OK_NO_STDIO

ok_csv *ok_csv_read(FILE *file) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (file) {
        decode_csv(csv, file, ok_file_read_func);
    } else {
        ok_csv_error(csv, "File not found");
    }
    return csv;
}

#endif

ok_csv *ok_csv_read_from_callbacks(void *user_data, ok_csv_read_func input_read_func) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (input_read_func) {
        decode_csv(csv, user_data, input_read_func);
    } else {
        ok_csv_error(csv, "Invalid argument: input_func is NULL");
    }
    return csv;
}

void ok_csv_free(ok_csv *csv) {
    if (csv) {
        ok_csv_cleanup(csv);
        free(csv);
    }
}

// MARK: Decoding

static const int min_record_capacity = 16;
static const int min_field_capacity = 16;
static const int default_input_buffer_capacity = 4096;

typedef enum {
    // About to add a new record. Ignore if next char is cr, lf, or eof
    RECORD_START = 0,

    // About to add a new field (could be a blank field if next char is a comma, cr, or lf)
    FIELD_START,

    // Parsing an escaped field (stop when a lone double quote is found)
    ESCAPED_FIELD,

    // Parsing a non-escaped field (stop when a comma, cr, lf, or eof is found)
    NONESCAPED_FIELD,
} csv_decoder_state;

static void decode_csv(ok_csv *csv, void *input_data, ok_csv_read_func input_read_func) {
    if (!csv) {
        return;
    }
    csv_decoder *decoder = calloc(1, sizeof(csv_decoder));
    if (!decoder) {
        ok_csv_error(csv, "Couldn't allocate decoder.");
        return;
    }
    if (!circular_buffer_init(&decoder->input_buffer, default_input_buffer_capacity)) {
        free(decoder);
        ok_csv_error(csv, "Couldn't allocate input buffer.");
        return;
    }
    decoder->csv = csv;
    decoder->input_data = input_data;
    decoder->input_read_func = input_read_func;

    decode_csv2(decoder);

    free(decoder->input_buffer.data);
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

    if (!csv->num_fields || curr_capacity < new_capacity) {
        csv->num_fields = realloc(csv->num_fields, sizeof(int) * new_capacity);
        csv->fields = realloc(csv->fields, sizeof(char **) * new_capacity);
        if (!csv->num_fields || !csv->fields) {
            ok_csv_error(csv, "Couldn't allocate fields array");
            return false;
        }
    }
    return true;
}

// Ensure capacity for at least (csv->num_fields[record] + 1) fields
static bool csv_ensure_field_capcity(ok_csv *csv, int record) {
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

    if (!csv->fields[record] || curr_capacity < new_capacity) {
        csv->fields[record] = realloc(csv->fields[record], sizeof(char *) * new_capacity);
        if (!csv->fields[record]) {
            ok_csv_error(csv, "Couldn't allocate fields array");
            return false;
        }
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
            int writeable = circular_buffer_writable(&decoder->input_buffer);
            if (writeable == 0) {
                circular_buffer_expand(&decoder->input_buffer);
                writeable = circular_buffer_writable(&decoder->input_buffer);
            }
            uint8_t *end = decoder->input_buffer.data +
                ((decoder->input_buffer.start + decoder->input_buffer.length) %
                 decoder->input_buffer.capacity);
            int bytesRead = (int)decoder->input_read_func(decoder->input_data, end, writeable);
            decoder->input_buffer.length += bytesRead;
        }

        // Peek current char (0 if EOF)
        uint8_t curr_char = 0;
        if (decoder->input_buffer.length - peek > 0) {
            int offset = (decoder->input_buffer.start + peek) % decoder->input_buffer.capacity;
            curr_char = decoder->input_buffer.data[offset];
            peek++;
        } else {
            is_eof = true;
        }

        switch (state) {
            case RECORD_START:
            default: {
                if (is_eof) {
                    // Do nothing
                    return;
                } else if (curr_char == '\n' && prev_char == '\r') {
                    // Second char in CRLF sequence, ignore
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else if (curr_char == '\"') {
                    // Add new record
                    csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;

                    // Prep for escaped field
                    state = ESCAPED_FIELD;
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else if (curr_char == ',' || curr_char == '\r' || curr_char == '\n') {
                    // Add new record
                    int curr_record = csv->num_records;
                    csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;

                    // Add blank field
                    char *blank_field = malloc(1);
                    if (!blank_field) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    blank_field[0] = 0;
                    csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][0] = blank_field;
                    csv->num_fields[curr_record] = 1;

                    if (curr_char == ',') {
                        state = FIELD_START;
                    } else {
                        state = RECORD_START;
                    }
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else {
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
            case FIELD_START: {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    // Add blank field
                    char *blank_field = malloc(1);
                    if (!blank_field) {
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
                    } else {
                        state = RECORD_START;
                    }
                } else if (curr_char == '\"') {
                    state = ESCAPED_FIELD;
                    circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else {
                    state = NONESCAPED_FIELD;
                }
                break;
            }
            case ESCAPED_FIELD: {
                if (is_eof || (prev_char == '\"' && peek > 1 &&
                               (curr_char == ',' || curr_char == '\r' || curr_char == '\n'))) {
                    if (prev_char == '\"') {
                        peek--;
                    }
                    char *field = malloc(peek + 1);
                    if (!field) {
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
                            } else {
                                dquote_escape = true;
                            }
                        } else if (dquote_escape) {
                            // Shouldn't happen on welformed CSV files
                            *field_ptr++ = '\"';
                            dquote_escape = false;
                            *field_ptr++ = ch;
                        } else if (ch == '\n' && field_ptr > field && *(field_ptr - 1) == '\r') {
                            // Convert "\r\n" to "\n". Maybe this should be an option.
                            *(field_ptr - 1) = '\n';
                        } else {
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
                    } else if (curr_char == '\r' || curr_char == '\n') {
                        state = RECORD_START;
                    }
                }
                break;
            }
            case NONESCAPED_FIELD: {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    char *field = malloc(peek);
                    if (!field) {
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
                    } else if (curr_char == '\r' || curr_char == '\n') {
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
