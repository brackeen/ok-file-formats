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
#ifndef _OK_CSV_H_
#define _OK_CSV_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
    
    /**
     Reads CSV (Comma-Separated Values) files.
     Properly handles escaped fields.
     Same as RFC 4180, with the addition of allowing UTF-8 strings (as exported from Apple Numbers and Google Docs).
     On success, num_records will be > 0.
     */
    
    typedef struct {
        int num_records;         /// Number of records (rows)
        int *num_fields;         /// Number of fields (columns) for each record.
        char ***fields;          /// Fields. The value fields[record][field] is a NULL-terminated string.
        char error_message[80];  /// Error message (if num_records is 0)
    } ok_csv;
    
#ifndef _OK_READ_FUNC_
#define _OK_READ_FUNC_
    /// Reads 'count' bytes into buffer. Returns number of bytes read.
    typedef size_t (*ok_read_func)(void *user_data, uint8_t *buffer, const size_t count);
    
    /// Seek function. Should return 0 on success.
    typedef int (*ok_seek_func)(void *user_data, const int count);
#endif
    
    ok_csv *ok_csv_read(const char *file_name);
    ok_csv *ok_csv_read_from_file(FILE *file);
    ok_csv *ok_csv_read_from_memory(const void *buffer, const size_t buffer_length);
    ok_csv *ok_csv_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func);

    void ok_csv_free(ok_csv *csv);
    
#ifdef __cplusplus
}
#endif

#endif
