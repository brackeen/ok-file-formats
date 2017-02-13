#include "jpg_test.h"
#include "ok_jpg.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *filenames[] = {

    // grayscale
    "jpg-gray",
    "jpeg400jfif", // has restart markers

    // no upsampling
    "creek-after",
    "jpeg444",
    "magritte",

    // 2x1 upsampling
    "baby-camel",
    "LEVEL76",

    // 1x2 upsampling
    "mandril_color",
    "peppers_color",

    // 2x2 upsampling
    "2001-stargate",
    "monkey_monkey",
    "tomatoes",
    "zam",

    // Various sizes (2x2 upsampling)
    "jpg-size-1x1",
    "jpg-size-2x2",
    "jpg-size-3x3",
    "jpg-size-4x4",
    "jpg-size-5x5",
    "jpg-size-6x6",
    "jpg-size-7x7",
    "jpg-size-8x8",
    "jpg-size-9x9",
    "jpg-size-15x15",
    "jpg-size-16x16",
    "jpg-size-17x17",
    "jpg-size-31x31",
    "jpg-size-32x32",
    "jpg-size-33x33",

    // EXIF orientation
    "orientation_none",
    "orientation_1",
    "orientation_2",
    "orientation_3",
    "orientation_4",
    "orientation_5",
    "orientation_6",
    "orientation_7",
    "orientation_8",
};

static bool test_image(const char *path_to_jpgs,
                       const char *path_to_rgba_files,
                       const char *name,
                       bool info_only, bool verbose) {
    const bool flip_y = false;

    char *rgba_filename = get_full_path(path_to_rgba_files, name, "rgba");
    unsigned long rgba_data_length;
    uint8_t *rgba_data = read_file(rgba_filename, &rgba_data_length);
    bool success = false;

    // Load via ok_jpg
    ok_jpg *jpg = NULL;
    char *in_filename = get_full_path(path_to_jpgs, name, "jpg");
    FILE *file = fopen(in_filename, "rb");
    if (file) {
        if (info_only) {
            jpg = ok_jpg_read_info(file);
        } else {
            jpg = ok_jpg_read(file, OK_JPG_COLOR_FORMAT_RGBA, flip_y);
        }
        fclose(file);

        success = compare(name, "jpg", jpg->data, jpg->width, jpg->height, jpg->error_message,
                          rgba_data, rgba_data_length, info_only, 4, verbose);
    } else {
        printf("Warning: File not found: %s.jpg\n", name);
        success = true;
    }

    free(rgba_data);
    free(rgba_filename);
    free(in_filename);
    ok_jpg_free(jpg);

    return success;
}

int jpg_test(const char *path_to_jpgs, const char *path_to_rgba_files, bool verbose) {
    const int num_files = sizeof(filenames) / sizeof(filenames[0]);
    if (verbose) {
        printf("Testing %i files in path \"%s\".\n", num_files, path_to_jpgs);
    }

    double startTime = clock() / (double)CLOCKS_PER_SEC;
    int num_failures = 0;
    for (int i = 0; i < num_files; i++) {
        bool success = test_image(path_to_jpgs, path_to_rgba_files, filenames[i], true, verbose);
        if (!success) {
            num_failures++;
        } else {
            success = test_image(path_to_jpgs, path_to_rgba_files, filenames[i], false, verbose);
            if (!success) {
                num_failures++;
            }
        }
    }
    double endTime = clock() / (double)CLOCKS_PER_SEC;
    double elapsedTime = endTime - startTime;
    printf("Success: JPEG %i of %i\n", (num_files - num_failures), num_files);
    if (verbose) {
        printf("Duration: %f seconds\n", elapsedTime);
    }
    return num_failures;
}
