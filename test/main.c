#include "csv_test.h"
#include "jpg_test.h"
#include "mo_test.h"
#include "png_suite_test.h"
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

int main() {
    char path[1024];
    if (getcwd(path, sizeof(path)) == NULL) {
        perror("getcwd() error");
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

        png_suite_test(path_png, path_gen);
        jpg_test(path_jpg, path_gen);
        csv_test(path_csv);
        gettext_test(path_gettext);
        wav_test(path_gen);

        free(path_png);
        free(path_jpg);
        free(path_gen);
        free(path_csv);
        free(path_gettext);
    }
    return 0;
}
