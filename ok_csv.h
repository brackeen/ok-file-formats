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

#ifndef OK_CSV_H
#define OK_CSV_H

/**
 * @file
 * Functions to read CSV (Comma-Separated Values) files.
 * - Reads CSV files as defined by RFC 4180.
 * - Properly handles escaped fields.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_csv.h"
 *     int main() {
 *         FILE *file = fopen("my_data.csv", "rb");
 *         ok_csv *csv = ok_csv_read(file);
 *         fclose(file);
 *         if (csv->num_records > 0) {
 *             printf("Got CSV! %i records\n", csv->num_records);
 *             if (csv->num_fields[0] > 2) {
 *                 printf("Third field in first record: '%s'\n", csv->fields[0][2]);
 *             }
 *         }
 *         ok_csv_free(csv);
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

/**
 * The data returned from #ok_csv_read().
 */
typedef struct {
    /// Number of records (rows)
    size_t num_records;
    /// Number of fields (columns) for each record.
    size_t *num_fields;
    /// Fields. The value fields[record][field] is a NULL-terminated string.
    char ***fields;
    /// Error message (if num_records is 0)
    const char *error_message;
} ok_csv;

#ifndef OK_NO_STDIO

/**
 * Reads a CSV file.
 * On failure, #ok_csv.num_records is zero and #ok_csv.error_message is set.
 *
 * @param file The file to read.
 * @return a new #ok_csv object. Never returns `NULL`. The object should be freed with
 * #ok_csv_free().
 */
ok_csv *ok_csv_read(FILE *file);

#endif

/**
 * Frees the CSV data. This function should always be called when done with the CSV data, even if
 * reading failed.
 */
void ok_csv_free(ok_csv *csv);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_csv_read_from_callbacks() function.
 *
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_csv_read_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_csv_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Reads a CSV file.
 * On failure, #ok_csv.num_records is zero and #ok_csv.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @return a new #ok_csv object. Never returns `NULL`. The object should be freed with
 * #ok_csv_free().
 */
ok_csv *ok_csv_read_from_callbacks(void *user_data, ok_csv_read_func read_func);


#ifdef __cplusplus
}
#endif

#endif
