#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "test_common.h"
#include "jpg_test.h"

const char *filenames[] = {
    "2001-stargate",
    "baby-camel", // 2x1 upsampling
    "creek-after",
    "jpeg400jfif", // grayscale. restart markers
    "jpeg444",
    "LEVEL76",
    //"mandril_color", // 1x2 upsampling - disabled for testing because sips doesn't upsample properly
    "magritte",
    "monkey_monkey",
    //"peppers_color", // 1x2 upsampling
    "tomatoes",
    "zam",

    "jpg-gray",
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
};

static bool test_image(read_type input_read_type,
                       const char *path_to_png_suite,
                       const char *path_to_rgba_files,
                       const char *name,
                       const bool info_only) {
    const bool flip_y = false;
    const bool print_image_on_error = false;
    
    char *rgba_filename = get_full_path(path_to_rgba_files, name, "rgba");
    unsigned long rgba_data_length;
    uint8_t *rgba_data = read_file(rgba_filename, &rgba_data_length);
    free(rgba_filename);
    
    // Load via ok_jpg
    ok_image *image = read_image(path_to_png_suite, name, "jpg", input_read_type, info_only, OK_COLOR_FORMAT_RGBA, flip_y);
    bool success = compare(name, "jpg", image, rgba_data, rgba_data_length, info_only, 4, print_image_on_error);
    
    // Cleanup
    if (rgba_data != NULL) {
        free(rgba_data);
    }
    ok_image_free(image);
    
    return success;
}

void jpg_test(const char *path_to_jpgs, const char *path_to_rgba_files) {
    const int num_files = sizeof(filenames)/sizeof(filenames[0]);
    printf("Testing %i files in path \"%s\".\n", num_files, path_to_jpgs);
    
    double startTime = (double)clock()/CLOCKS_PER_SEC;
    int num_failures = 0;
    for (int i = 0; i < num_files; i++) {
        for (int j = 0; j < READ_TYPE_COUNT; j++) {
            bool success = test_image(j, path_to_jpgs, path_to_rgba_files, filenames[i], false);
            if (!success) {
                num_failures++;
                break;
            }
            success = test_image(j, path_to_jpgs, path_to_rgba_files, filenames[i], true);
            if (!success) {
                num_failures++;
                break;
            }
        }
    }
    double endTime = (double)clock()/CLOCKS_PER_SEC;
    double elapsedTime = endTime - startTime;
    printf("Success: %i of %i\n", (num_files-num_failures), num_files);
    printf("Duration: %f seconds\n", (float)elapsedTime);
}