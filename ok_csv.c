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
    size_t capacity;
    size_t start;
    size_t length;
} ok_csv_circular_buffer;

static bool ok_csv_circular_buffer_init(ok_csv_circular_buffer *buffer, size_t capacity) {
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
static size_t ok_csv_circular_buffer_writable(ok_csv_circular_buffer *buffer) {
    size_t total_writable = buffer->capacity - buffer->length;
    return min(total_writable,
               buffer->capacity - ((buffer->start + buffer->length) % buffer->capacity));
}

// Number of readable elements until edge of buffer
static size_t ok_csv_circular_buffer_readable(ok_csv_circular_buffer *buffer) {
    return min(buffer->length, buffer->capacity - buffer->start);
}

// Doubles the size of the buffer
static bool ok_csv_circular_buffer_expand(ok_csv_circular_buffer *buffer) {
    size_t new_capacity = buffer->capacity * 2;
    uint8_t *new_data = malloc(new_capacity);
    if (!new_data) {
        return false;
    } else {
        const size_t readable1 = ok_csv_circular_buffer_readable(buffer);
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

static bool ok_csv_circular_buffer_read(ok_csv_circular_buffer *buffer, uint8_t *dst,
                                        size_t length) {
    if (length > buffer->length) {
        return false;
    } else {
        const size_t readable1 = ok_csv_circular_buffer_readable(buffer);
        if (length <= readable1) {
            memcpy(dst, buffer->data + buffer->start, length);
        } else {
            const size_t readable2 = buffer->length - readable1;
            memcpy(dst, buffer->data + buffer->start, readable1);
            memcpy(dst + readable1, buffer->data, readable2);
        }
        buffer->start = (buffer->start + length) % buffer->capacity;
        buffer->length -= length;
        return true;
    }
}

static bool ok_csv_circular_buffer_skip(ok_csv_circular_buffer *buffer, size_t length) {
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
    ok_csv_circular_buffer input_buffer;

    // Input
    void *input_data;
    ok_csv_read_func input_read_func;

} ok_csv_decoder;

static void ok_csv_decode(ok_csv *csv, void *input_data, ok_csv_read_func input_read_func);
static void ok_csv_decode2(ok_csv_decoder *decoder);

static void ok_csv_cleanup(ok_csv *csv) {
    if (csv) {
        if (csv->fields && csv->num_fields) {
            for (size_t i = 0; i < csv->num_records; i++) {
                for (size_t j = 0; j < csv->num_fields[i]; j++) {
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

#ifdef NDEBUG
#define ok_csv_error(csv, message) ok_csv_set_error((csv), "ok_csv_error")
#else
#define ok_csv_error(csv, message) ok_csv_set_error((csv), (message))
#endif

static void ok_csv_set_error(ok_csv *csv, const char *message) {
    if (csv) {
        ok_csv_cleanup(csv);
        csv->error_message = message;
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
        ok_csv_decode(csv, file, ok_file_read_func);
    } else {
        ok_csv_error(csv, "File not found");
    }
    return csv;
}

#endif

ok_csv *ok_csv_read_from_callbacks(void *user_data, ok_csv_read_func input_read_func) {
    ok_csv *csv = calloc(1, sizeof(ok_csv));
    if (input_read_func) {
        ok_csv_decode(csv, user_data, input_read_func);
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

static const size_t OK_CSV_MIN_RECORD_CAPACITY = 16;
static const size_t OK_CSV_MIN_FIELD_CAPACITY = 16;
static const size_t OK_CSV_INPUT_BUFFER_CAPACITY = 4096;

typedef enum {
    // About to add a new record. Ignore if next char is cr, lf, or eof
    OK_CSV_RECORD_START = 0,

    // About to add a new field (could be a blank field if next char is a comma, cr, or lf)
    OK_CSV_FIELD_START,

    // Parsing an escaped field (stop when a lone double quote is found)
    OK_CSV_ESCAPED_FIELD,

    // Parsing a non-escaped field (stop when a comma, cr, lf, or eof is found)
    OK_CSV_NONESCAPED_FIELD,
} ok_csv_decoder_state;

static void ok_csv_decode(ok_csv *csv, void *input_data, ok_csv_read_func input_read_func) {
    if (!csv) {
        return;
    }
    ok_csv_decoder *decoder = calloc(1, sizeof(ok_csv_decoder));
    if (!decoder) {
        ok_csv_error(csv, "Couldn't allocate decoder.");
        return;
    }
    if (!ok_csv_circular_buffer_init(&decoder->input_buffer, OK_CSV_INPUT_BUFFER_CAPACITY)) {
        free(decoder);
        ok_csv_error(csv, "Couldn't allocate input buffer.");
        return;
    }
    decoder->csv = csv;
    decoder->input_data = input_data;
    decoder->input_read_func = input_read_func;

    ok_csv_decode2(decoder);

    free(decoder->input_buffer.data);
    free(decoder);
}

// Ensure capacity for at least (csv->num_records + 1) records
static bool ok_csv_ensure_record_capcity(ok_csv *csv) {
    // curr_capacity is >= csv->num_records
    size_t curr_capacity = OK_CSV_MIN_RECORD_CAPACITY;
    while (curr_capacity < csv->num_records) {
        curr_capacity <<= 1;
    }

    // new_capacity is >= (csv->num_records + 1)
    size_t new_capacity = curr_capacity;
    if (new_capacity < csv->num_records + 1) {
        new_capacity <<= 1;
    }

    if (!csv->num_fields || curr_capacity < new_capacity) {
        csv->num_fields = realloc(csv->num_fields, sizeof(*csv->num_fields) * new_capacity);
        csv->fields = realloc(csv->fields, sizeof(char **) * new_capacity);
        if (!csv->num_fields || !csv->fields) {
            ok_csv_error(csv, "Couldn't allocate fields array");
            return false;
        }
    }
    return true;
}

// Ensure capacity for at least (csv->num_fields[record] + 1) fields
static bool ok_csv_ensure_field_capcity(ok_csv *csv, size_t record) {
    // curr_capacity is >= csv->num_fields[record]
    size_t curr_capacity = OK_CSV_MIN_FIELD_CAPACITY;
    while (curr_capacity < csv->num_fields[record]) {
        curr_capacity <<= 1;
    }

    // new_capacity is >= (csv->num_fields[record] + 1)
    size_t new_capacity = curr_capacity;
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

static void ok_csv_decode2(ok_csv_decoder *decoder) {
    ok_csv *csv = decoder->csv;
    size_t peek = 0;
    uint8_t prev_char = 0;
    ok_csv_decoder_state state = OK_CSV_RECORD_START;
    bool is_eof = false;

    while (true) {
        // Read data if needed
        if (decoder->input_buffer.length - peek == 0) {
            size_t writeable = ok_csv_circular_buffer_writable(&decoder->input_buffer);
            if (writeable == 0) {
                ok_csv_circular_buffer_expand(&decoder->input_buffer);
                writeable = ok_csv_circular_buffer_writable(&decoder->input_buffer);
            }
            uint8_t *end = decoder->input_buffer.data +
                ((decoder->input_buffer.start + decoder->input_buffer.length) %
                 decoder->input_buffer.capacity);
            size_t bytesRead = decoder->input_read_func(decoder->input_data, end, writeable);
            decoder->input_buffer.length += bytesRead;
        }

        // Peek current char (0 if EOF)
        uint8_t curr_char = 0;
        if (decoder->input_buffer.length - peek > 0) {
            size_t offset = (decoder->input_buffer.start + peek) % decoder->input_buffer.capacity;
            curr_char = decoder->input_buffer.data[offset];
            peek++;
        } else {
            is_eof = true;
        }

        switch (state) {
            case OK_CSV_RECORD_START:
            default: {
                if (is_eof) {
                    // Do nothing
                    return;
                } else if (curr_char == '\n' && prev_char == '\r') {
                    // Second char in CRLF sequence, ignore
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else if (curr_char == '\"') {
                    // Add new record
                    ok_csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;

                    // Prep for escaped field
                    state = OK_CSV_ESCAPED_FIELD;
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else if (curr_char == ',' || curr_char == '\r' || curr_char == '\n') {
                    // Add new record
                    size_t curr_record = csv->num_records;
                    ok_csv_ensure_record_capcity(csv);
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
                    ok_csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][0] = blank_field;
                    csv->num_fields[curr_record] = 1;

                    if (curr_char == ',') {
                        state = OK_CSV_FIELD_START;
                    } else {
                        state = OK_CSV_RECORD_START;
                    }
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else {
                    // Add new record
                    ok_csv_ensure_record_capcity(csv);
                    csv->num_fields[csv->num_records] = 0;
                    csv->fields[csv->num_records] = NULL;
                    csv->num_records++;

                    // Prep for nonescaped field
                    state = OK_CSV_NONESCAPED_FIELD;
                }
                break;
            }
            case OK_CSV_FIELD_START: {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    // Add blank field
                    char *blank_field = malloc(1);
                    if (!blank_field) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    blank_field[0] = 0;
                    size_t curr_record = csv->num_records - 1;
                    ok_csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = blank_field;
                    csv->num_fields[curr_record]++;
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                    if (curr_char == ',') {
                        state = OK_CSV_FIELD_START;
                    } else {
                        state = OK_CSV_RECORD_START;
                    }
                } else if (curr_char == '\"') {
                    state = OK_CSV_ESCAPED_FIELD;
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, peek);
                    peek = 0;
                } else {
                    state = OK_CSV_NONESCAPED_FIELD;
                }
                break;
            }
            case OK_CSV_ESCAPED_FIELD: {
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
                        char ch = (char)decoder->input_buffer.data[decoder->input_buffer.start];
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
                        ok_csv_circular_buffer_skip(&decoder->input_buffer, 1);
                    }
                    *field_ptr++ = 0;

                    if (prev_char == '\"') {
                        // Skip closing dquote
                        ok_csv_circular_buffer_skip(&decoder->input_buffer, 1);
                    }

                    size_t curr_record = csv->num_records - 1;
                    ok_csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = field;
                    csv->num_fields[curr_record]++;

                    if (curr_char == ',') {
                        state = OK_CSV_FIELD_START;
                    } else if (curr_char == '\r' || curr_char == '\n') {
                        state = OK_CSV_RECORD_START;
                    }
                }
                break;
            }
            case OK_CSV_NONESCAPED_FIELD: {
                if (curr_char == ',' || curr_char == '\r' || curr_char == '\n' || is_eof) {
                    char *field = malloc(peek);
                    if (!field) {
                        ok_csv_error(csv, "Couldn't allocate field");
                        return;
                    }
                    ok_csv_circular_buffer_read(&decoder->input_buffer, (unsigned char *)field,
                                                peek - 1);
                    ok_csv_circular_buffer_skip(&decoder->input_buffer, 1);
                    field[peek - 1] = 0;
                    peek = 0;

                    size_t curr_record = csv->num_records - 1;
                    ok_csv_ensure_field_capcity(csv, curr_record);
                    csv->fields[curr_record][csv->num_fields[curr_record]] = field;
                    csv->num_fields[curr_record]++;

                    if (curr_char == ',') {
                        state = OK_CSV_FIELD_START;
                    } else if (curr_char == '\r' || curr_char == '\n') {
                        state = OK_CSV_RECORD_START;
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
