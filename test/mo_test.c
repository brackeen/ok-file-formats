
#include "mo_test.h"
#include "test_common.h"
#include "ok_mo.h"
#include <string.h>

void gettext_test(const char *path) {
    
    const char *en_file = get_full_path(path, "en", "mo");
    const char *es_file = get_full_path(path, "es", "mo");
    const char *zh_file = get_full_path(path, "zh-Hans", "mo");
    
    ok_mo *mo_en = ok_mo_read(en_file);
    if (strcmp("Hello", ok_mo_value(mo_en, "Hello")) != 0) {
        printf("Failure: Hello\n");
        return;
    }
    if (strcmp("N/A", ok_mo_value(mo_en, "N/A")) != 0) {
        printf("Failure: missing key\n");
        return;
    }
    if (strcmp("%d user likes this.", ok_mo_plural_value(mo_en, "%d user likes this.", "%d users like this.", 1)) != 0) {
        printf("Failure: singular\n");
        return;
    }
    if (strcmp("%d users like this.", ok_mo_plural_value(mo_en, "%d user likes this.", "%d users like this.", 2)) != 0) {
        printf("Failure: plural\n");
        return;
    }
    ok_mo_free(mo_en);
    
    ok_mo *mo_es = ok_mo_read(es_file);
    if (strcmp("Archivo", ok_mo_value_in_context(mo_es, "Menu", "File")) != 0) {
        printf("Failure: context\n");
        return;
    }
    ok_mo_free(mo_es);
    
    ok_mo *mo_zh = ok_mo_read(zh_file);
    char hello_utf8[] = { 0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd, 0 };
    if (strcmp(hello_utf8, ok_mo_value(mo_zh, "Hello")) != 0) {
        printf("Failure: utf8\n");
        return;
    }
    ok_mo_free(mo_zh);
    
    printf("Success: MO (gettext)\n");
}