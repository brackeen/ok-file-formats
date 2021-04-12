#include "csv_test.h"
#include "jpg_test.h"
#include "mo_test.h"
#include "png_suite_test.h"
#include "png_write_test.h"
#include "wav_test.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#  define getcwd _getcwd
#else
#  include <unistd.h>
#endif

int main(int argc, char *argv[]) {
    bool verbose = false;
    for (int i = 0; i < argc; i++) {
        if (strcmp("--verbose", argv[i]) == 0) {
            verbose = true;
        }
    }

    int error_count = 0;

    char path[1024];
    if (getcwd(path, sizeof(path)) == NULL) {
        perror("getcwd() error");
        error_count = 1;
    } else {
        char *path_png = append_path(path, "PngSuite");
        char *path_jpg = append_path(path, "jpg");
        char *path_csv = append_path(path, "csv");
        char *path_gettext = append_path(path, "gettext");

        #ifdef _WIN32
            strcat(path, "\\build");
        #else
            strcat(path, "/build");
        #endif
        char *path_gen = append_path(path, "gen");

        error_count += png_suite_test(path_png, path_gen, verbose);
        error_count += png_write_test(verbose);
        error_count += wav_test(path_gen, verbose);
        error_count += jpg_test(path_jpg, path_gen, verbose);
        error_count += csv_test(path_csv, verbose);
        error_count += gettext_test(path_gettext, verbose);

        free(path_png);
        free(path_jpg);
        free(path_gen);
        free(path_csv);
        free(path_gettext);
    }
    return error_count;
}
