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

#ifndef _OK_CSV_H_
#define _OK_CSV_H_

/**
 * @file
 * Functions to read CSV (Comma-Separated Values) files.
 * - Reads CSV files as defined by RFC 4180.
 * - Properly handles escaped fields.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The data returned from #ok_csv_read().
 */
typedef struct {
    /// Number of records (rows)
    int num_records;
    /// Number of fields (columns) for each record.
    int *num_fields;
    /// Fields. The value fields[record][field] is a NULL-terminated string.
    char ***fields;
    /// Error message (if num_records is 0)
    char error_message[80];
} ok_csv;

/**
 * Input function provided to the #ok_csv_read() function.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_csv_read() function.
 * @param buffer The data buffer to copy bytes to. If `NULL`, this function should perform a
 * relative seek.
 * @param count The number of bytes to read. If negative, this function should perform a
 * relative seek.
 * @return The number of bytes read or skipped. Should return 0 on error.
 */
typedef int (*ok_csv_input_func)(void *user_data, uint8_t *buffer, int count);

/**
 * Reads a CSV file.
 * On failure, #ok_csv.num_records is zero and #ok_csv.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a CSV file from.
 * @return a new #ok_csv object. Never returns `NULL`. The object should be freed with
 * #ok_csv_free().
 */
ok_csv *ok_csv_read(void *user_data, ok_csv_input_func input_func);

/**
 * Frees the CSV data. This function should always be called when done with the CSV data, even if
 * reading failed.
 */
void ok_csv_free(ok_csv *csv);

#ifdef __cplusplus
}
#endif

#endif
