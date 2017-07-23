#include "jpg_test.h"
#include "ok_jpg.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

enum jpg_test_type {
    test_normal,
    test_info_only,
    test_to_buffer,
};

static const char *filenames[] = {

    // Grayscale
    "jpg-gray",
    "jpeg400jfif", // With restart markers
    "peace",

    // No upsampling
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

    // Strange markers
    "applesauce", // Extra 0xFF
    "park", // Extra 0x00 before 0xFF

    // Large images (max size handled by ImageMagick)
    "65500w",
    "65500h",

    // Progressive
    "pumpkins", // Color, 2x2 upsampling, no successive approximation
    "robot", // Color, 2x2 upsampling, successive approximation
    "2004", // Color, no upsampling, successive approximation
    "einstein", // Color, no upsampling, no successive approximation
    "gort", // Grayscale, no upsampling, no successive approximation
    // TODO: Find progressive, color, upsampled image with restart markers

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
                       enum jpg_test_type test_type, bool verbose) {
    char *rgba_filename = get_full_path(path_to_rgba_files, name, "rgba");
    unsigned long rgba_data_length;
    uint8_t *rgba_data = read_file(rgba_filename, &rgba_data_length);
    bool success = false;

    // Load via ok_jpg
    ok_jpg *jpg = NULL;
    uint8_t *jpg_data = NULL;
    uint32_t jpg_data_stride = 0;
    char *in_filename = get_full_path(path_to_jpgs, name, "jpg");
    FILE *file = fopen(in_filename, "rb");
    if (file) {
        switch (test_type) {
            case test_normal:
                jpg = ok_jpg_read(file, OK_JPG_COLOR_FORMAT_RGBA);
                jpg_data = jpg->data;
                jpg_data_stride = jpg->width * 4;
                break;
            case test_info_only:
                jpg = ok_jpg_read(file, OK_JPG_INFO_ONLY);
                jpg_data = jpg->data;
                jpg_data_stride = jpg->width * 4;
                break;
            case test_to_buffer:
                jpg = ok_jpg_read(file, OK_JPG_INFO_ONLY);

                if (jpg->width > 0 && jpg->height > 0) {
                    fclose(file);
                    uint32_t dst_stride = align_to(jpg->width * 4, 512);
                    uint8_t *dst_buffer = malloc(dst_stride * jpg->height);
                    ok_jpg_free(jpg);

                    file = fopen(in_filename, "rb");
                    jpg = ok_jpg_read_to_buffer(file, dst_buffer, dst_stride,
                                                OK_JPG_COLOR_FORMAT_RGBA);
                    jpg_data = dst_buffer;
                    jpg_data_stride = dst_stride;
                }
                break;
        }
        fclose(file);

        bool info_only = test_type == test_info_only;
        success = compare(name, "jpg", jpg_data, jpg_data_stride,
                          jpg->width, jpg->height,
                          jpg->error_message,
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
        bool success = test_image(path_to_jpgs, path_to_rgba_files, filenames[i], test_normal,
                                  verbose);
        if (!success) {
            num_failures++;
            continue;
        }
        success = test_image(path_to_jpgs, path_to_rgba_files, filenames[i], test_info_only,
                             verbose);
        if (!success) {
            num_failures++;
            continue;
        }

        success = test_image(path_to_jpgs, path_to_rgba_files, filenames[i], test_to_buffer,
                             verbose);
        if (!success) {
            num_failures++;
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
