#ifndef JPG_TEST_H
#define JPG_TEST_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int jpg_test(const char *path_to_jpgs, const char *path_to_rgba_files, bool verbose);

#ifdef __cplusplus
}
#endif

#endif
