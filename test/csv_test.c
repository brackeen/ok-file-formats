
#include "csv_test.h"
#include "test_common.h"
#include "ok_csv.h"
#include <string.h>

void csv_test(const char *path) {

    char *test1_file = get_full_path(path, "test1", "csv");

    ok_csv *csv = ok_csv_read(test1_file);
    
    if (csv == NULL) {
        printf("Failure: ok_csv is NULL\n");
        return;
    }
    if (csv->num_records == 0) {
        printf("Failure: Couldn't load CSV: %s\n", csv->error_message);
        return;
    }
    if (csv->num_records != 10) {
        printf("Failure: Didn't find 10 records\n");
        return;
    }
    
    unsigned char hello[] = { 0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd, 0 };
    
    if (memcmp(hello, csv->fields[2][2], 7)) {
        printf("Failure: Couldn't read UTF-8 field\n");
        return;
    }
    if (memcmp("Smith, Fred", csv->fields[3][2], 12)) {
        printf("Failure: Couldn't read field with comma\n");
        return;
    }
    if (memcmp("\"The Prof\"", csv->fields[4][2], 11)) {
        printf("Failure: Couldn't read field with quotes\n");
        return;
    }
    
    if (csv->num_fields[5] != 3 || csv->fields[5][0][0] != 0 || csv->fields[5][1][0] != 0 || csv->fields[5][1][0] != 0) {
        printf("Failure: Couldn't read three blank fields\n");
        return;
    }
    if (csv->num_fields[6] != 1 || csv->fields[6][0][0] != 0) {
        printf("Failure: Couldn't read one blank line\n");
        return;
    }
    if (memcmp("This is\ntwo lines", csv->fields[7][2], 18)) {
        printf("Failure: Couldn't read multiline\n");
        return;
    }
    if (memcmp("\nThis is also two lines", csv->fields[8][2], 24)) {
        printf("Failure: Couldn't read multiline2\n");
        return;
    }
    if (strlen(csv->fields[9][2]) != 4105) {
        printf("Failure: Couldn't read long line\n");
        return;
    }

    ok_csv_free(csv);
    
    printf("Success: CSV\n");
}