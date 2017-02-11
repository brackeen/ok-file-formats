#include "mo_test.h"
#include "ok_mo.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ok_mo *mo_read(const char *filename) {
    FILE *fp = fopen(filename, "rb");
    ok_mo *mo = ok_mo_read(fp, file_input_func);
    fclose(fp);
    return mo;
}

int gettext_test(const char *path, bool verbose) {
    (void)verbose;

    char *en_file = get_full_path(path, "en", "mo");
    char *es_file = get_full_path(path, "es", "mo");
    char *zh_file = get_full_path(path, "zh-Hans", "mo");

    ok_mo *mo_en = mo_read(en_file);
    if (strcmp("Hello", ok_mo_value(mo_en, "Hello")) != 0) {
        printf("Failure: Hello\n");
        return 1;
    }
    if (strcmp("N/A", ok_mo_value(mo_en, "N/A")) != 0) {
        printf("Failure: missing key\n");
        return 1;
    }
    if (strcmp("%d user likes this.", ok_mo_plural_value(mo_en, "%d user likes this.",
                                                         "%d users like this.", 1)) != 0) {
        printf("Failure: singular\n");
        return 1;
    }
    if (strcmp("%d users like this.", ok_mo_plural_value(mo_en, "%d user likes this.",
                                                         "%d users like this.", 2)) != 0) {
        printf("Failure: plural\n");
        return 1;
    }
    ok_mo_free(mo_en);

    ok_mo *mo_es = mo_read(es_file);
    if (strcmp("Archivo", ok_mo_value_in_context(mo_es, "Menu", "File")) != 0) {
        printf("Failure: context\n");
        return 1;
    }
    ok_mo_free(mo_es);

    ok_mo *mo_zh = mo_read(zh_file);
    char hello_utf8[] = {0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd, 0};
    if (strcmp(hello_utf8, ok_mo_value(mo_zh, "Hello")) != 0) {
        printf("Failure: utf8\n");
        return 1;
    }
    ok_mo_free(mo_zh);

    free(en_file);
    free(es_file);
    free(zh_file);

    printf("Success: MO (gettext)\n");
    return 0;
}
