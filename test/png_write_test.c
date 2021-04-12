#include "png_write_test.h"
#include "test_common.h"
#include "ok_png.h"
#include "ok_png_write.h"

#include <stdlib.h>
#include <stdio.h>
#if defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
#    include "file_compat.h"
#    define USE_FC_DATADIR
#  endif
#endif
#if !defined(PATH_MAX)
#  define PATH_MAX 4096
#endif

static int test_image_size(uint32_t width, uint32_t height, bool verbose) {
    ok_png png = { 0 };
    uint8_t *original_rgba_data = NULL;
    bool success = false;
    char test_name[PATH_MAX];
    snprintf(test_name, sizeof(test_name), "png_write_%ux%u", width, height);
    
    // Get output dir
    char dir_path[PATH_MAX];
#if defined(USE_FC_DATADIR)
    if (fc_datadir("ok_file_formats", dir_path, PATH_MAX)) {
        printf("Error: Couldn't get datadir\n");
        goto cleanup;
    }
#else
    dir_path[0] = '\0';
#endif
    
    // Fill an image with data
    const size_t original_rgba_data_len = (size_t)width * 4 * height;
    original_rgba_data = malloc(original_rgba_data_len);
    if (!original_rgba_data) {
        printf("Error: Couldn't allocate memory\n");
        goto cleanup;
    }
    for (size_t i = 0; i < original_rgba_data_len; i++) {
        original_rgba_data[i] = i & 0xff;
    }
    
    // Write image to PNG file
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s.png", dir_path, test_name);
    FILE *out_file = fopen(path, "wb");
    if (!out_file) {
        printf("Error: Couldn't open file for writing\n");
        goto cleanup;
    }
    bool write_success = ok_png_write_to_file(out_file, (ok_png_write_params) {
        .width = width,
        .height = height,
        .data = original_rgba_data,
        .color_type = OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA,
    });
    fclose(out_file);
    if (!write_success) {
        printf("Error writing PNG data\n");
        goto cleanup;
    }
    
    // Read image from PNG file
    FILE *in_file = fopen(path, "rb");
    if (!in_file) {
        printf("Error: Couldn't open file for reading\n");
        goto cleanup;
    }
    png = ok_png_read(in_file, OK_PNG_COLOR_FORMAT_RGBA);
    fclose(in_file);
    if (png.error_code != OK_PNG_SUCCESS) {
        printf("Error reading PNG data (error %i)\n", png.error_code);
        goto cleanup;
    }
    
    // Compare original vs. read
    success = compare(test_name, "png", png.data, png.stride, png.width, png.height, original_rgba_data, original_rgba_data_len, false, 0, verbose);

    // Cleanup
cleanup:
    remove(path);
    free(original_rgba_data);
    free(png.data);
    return success;
}

// This tests writing various size images; doesn't test CRC or Adler checksums (ok_png reader ignores them)
int png_write_test(bool verbose) {
#ifndef OK_PNG_WRITE_IDAT_MAX_LENGTH
#pragma message("Define OK_PNG_WRITE_IDAT_MAX_LENGTH=0x7ffff to test writing multiple IDAT chunks")
#endif
    // Sizes for 8-bit RGBA format images
    const uint32_t test_sizes[] = {
        // One block
        16383, 1,
        384, 42,
        
        // N rows per block
        384, 43, // 1 small block per row
        384, 84, // no small block per row
        384, 85, // 1 small block per row
        
        // N blocks per row
        16384, 4, // 1 small block at end
        49151, 4, // no small block at end
        
        // Adler in separate chunk
#if defined(OK_PNG_WRITE_IDAT_MAX_LENGTH) && OK_PNG_WRITE_IDAT_MAX_LENGTH == 0x7ffff
        1, 196598, // N rows per block, Adler in separate chunk
        131061, 1 // N blocks per row, Adler in separate chunk
#endif
        
    };
    const int num_tests = sizeof(test_sizes) / (sizeof(*test_sizes) * 2);
    int num_failures = 0;
    
    for (int i = 0; i < num_tests; i++) {
        uint32_t width = test_sizes[i * 2 + 0];
        uint32_t height = test_sizes[i * 2 + 1];
        if (!test_image_size(width, height, verbose)) {
            num_failures++;
        }
    }
    
    printf("Success: PNG write %i of %i\n", (num_tests - num_failures), num_tests);
    return num_failures;
}
