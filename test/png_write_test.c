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

static int api_test(const char *output_dir, bool verbose) {
    const char *test_name = "api_test_image";
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s.png", output_dir, test_name);
    
    const uint8_t image_data[8] = { 0xff, 0xff, 0xbd, 0xff, 0xff, 0xbd, 0xc3, 0xff };
    const uint8_t palette[6] = { 0x00, 0x00, 0x00, 0xd2, 0x32, 0x14 };
    const uint8_t palette_alpha[2] = { 0x00, 0xff };
    
    FILE *file = fopen(path, "wb");
    bool success = ok_png_write_to_file(file, (ok_png_write_params){
        .width = 8,
        .height = 8,
        .data = image_data,
        .bit_depth = 1,
        .color_type = OK_PNG_WRITE_COLOR_TYPE_PALETTE,
        .additional_chunks = (ok_png_write_chunk *[]){
            &(ok_png_write_chunk){
                .name = "PLTE",
                .data = palette,
                .length = sizeof(palette),
            },
            &(ok_png_write_chunk){
                .name = "tRNS",
                .data = palette_alpha,
                .length = sizeof(palette_alpha),
            },
            NULL
        }
    });
    fclose(file);
    if (!success) {
        return 1;
    }
   
#define C0 0x00, 0x00, 0x00, 0x00
#define C1 0xd2, 0x32, 0x14, 0xff
    const uint8_t expected_rgba[8 * 8 * 4] = {
        C1, C1, C1, C1, C1, C1, C1, C1, // 0xff
        C1, C1, C1, C1, C1, C1, C1, C1, // 0xff
        C1, C0, C1, C1, C1, C1, C0, C1, // 0xbd
        C1, C1, C1, C1, C1, C1, C1, C1, // 0xff
        C1, C1, C1, C1, C1, C1, C1, C1, // 0xff
        C1, C0, C1, C1, C1, C1, C0, C1, // 0xbd
        C1, C1, C0, C0, C0, C0, C1, C1, // 0xc3
        C1, C1, C1, C1, C1, C1, C1, C1, // 0xff
    };
#undef C0
#undef C1
    
    FILE *in_file = fopen(path, "rb");
    if (!in_file) {
        printf("Error: Couldn't open file for reading\n");
        return 1;
    }
    ok_png png = ok_png_read(in_file, OK_PNG_COLOR_FORMAT_RGBA);
    fclose(in_file);
    if (png.error_code != OK_PNG_SUCCESS) {
        printf("Error reading PNG data (error %i)\n", png.error_code);
        return 1;
    }
    
    // Compare written vs expected RGBA
    success = compare(test_name, "png", png.data, png.stride, png.width, png.height, expected_rgba, sizeof(expected_rgba), false, 0, verbose);
    
    free(png.data);
    remove(path);
    
    return success ? 0 : 1;
}

static int test_image(const char *output_dir, ok_png_write_params params, bool verbose) {
    ok_png png = { 0 };
    uint8_t *original_rgba_data = NULL;
    bool success = false;
    char test_name[PATH_MAX];
    snprintf(test_name, sizeof(test_name), "png_write_%ux%u", params.width, params.height);
       
    // Fill an image with data
    const size_t original_rgba_data_len = (size_t)params.width * 4 * params.height;
    original_rgba_data = malloc(original_rgba_data_len);
    if (!original_rgba_data) {
        printf("Error: Couldn't allocate memory\n");
        goto cleanup;
    }
    for (size_t i = 0; i < original_rgba_data_len; i++) {
        original_rgba_data[i] = i & 0xff;
    }
    params.data = original_rgba_data;
    
    // Write image to PNG file
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s.png", output_dir, test_name);
    FILE *out_file = fopen(path, "wb");
    if (!out_file) {
        printf("Error: Couldn't open file for writing\n");
        goto cleanup;
    }
    bool write_success = ok_png_write_to_file(out_file, params);
    fclose(out_file);
    if (!write_success) {
        printf("Error writing PNG data\n");
        goto cleanup;
    }
    
    // Read image from PNG file (without any data conversion)
    FILE *in_file = fopen(path, "rb");
    if (!in_file) {
        printf("Error: Couldn't open file for reading\n");
        goto cleanup;
    }
    if (params.apple_cgbi_format) {
        png = ok_png_read(in_file, (ok_png_decode_flags)(OK_PNG_COLOR_FORMAT_BGRA | OK_PNG_PREMULTIPLIED_ALPHA));
    } else {
        png = ok_png_read(in_file, OK_PNG_COLOR_FORMAT_RGBA);
    }
    fclose(in_file);
    if (png.error_code != OK_PNG_SUCCESS) {
        printf("Error reading PNG data (error %i)\n", png.error_code);
        goto cleanup;
    }
    
    // Compare original vs. read
    success = compare(test_name, "png", png.data, png.stride, png.width, png.height, original_rgba_data, original_rgba_data_len, false, 0, verbose);
    if (!success) {
        goto cleanup;
    }
    
#if defined(__APPLE__) && TARGET_OS_OSX
    if (!params.apple_cgbi_format) {
        char command[4096];
        snprintf(command, sizeof(command), "magick %s /dev/null", path);
        success = (system(command) == 0);
        if (!success) {
            printf("Error checking via convert command\n");
            goto cleanup;
        }
        goto cleanup;
    }
#endif
    
    // Cleanup
cleanup:
    remove(path);
    free(original_rgba_data);
    free(png.data);
    return success;
}

static size_t uncompressed_size(uint32_t width, uint32_t height, uint32_t bpp, bool apple_cgbi_format) {
    size_t data_size = (((size_t)width * bpp) + 1) * height; // RGBA data + filter per row
    size_t stored_block_count = (data_size + 0xffff - 1) / 0xffff; // Number of stored blocks (max size 0xffff)
    size_t wrapper = apple_cgbi_format ? 0 : 2 + 4; // zlib header (2 bytes), adler (4 bytes)
    return wrapper + 5 * stored_block_count + data_size;
}

// This tests writing various size images; doesn't test CRC or Adler checksums (ok_png reader ignores them)
int png_write_test(bool verbose) {
    // Get output dir
    char output_dir[PATH_MAX];
#if defined(USE_FC_DATADIR)
    if (fc_datadir("ok_file_formats", output_dir, PATH_MAX)) {
        printf("Error: Couldn't get datadir\n");
        return 1;
    }
#else
    output_dir[0] = '\0';
#endif
    
    int num_tests = 0;
    int num_failures = 0;

    num_tests++;
    num_failures += api_test(output_dir, verbose);
    
    // Test params. Image data filled in by test_image()
    const ok_png_write_params test_params[] = {
        (ok_png_write_params) {
            .width = 100, .height = 100,
            .color_type = OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA,
        },
        (ok_png_write_params) {
            .width = 65536, .height = 1, // Multiple stored blocks; Lots of deflating without buffering
            .color_type = OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA,
        },
        (ok_png_write_params) {
            .width = 100, .height = 1,
            .buffer_size = (uint32_t)uncompressed_size(100, 1, 4, false), // Exactly one IDAT chunk
            .color_type = OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA,
        },
        (ok_png_write_params) {
            .width = 100, .height = 1,
            .buffer_size = (uint32_t)uncompressed_size(100, 1, 4, false) - 4, // Adler in a separate IDAT chunk
            .color_type = OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA,
        },
    };
        
    for (size_t i = 0; i < sizeof(test_params) / sizeof(*test_params); i++) {
        ok_png_write_params params = test_params[i];
        
        // Test params as is
        num_tests++;
        if (!test_image(output_dir, params, verbose)) {
            num_failures++;
        }

        // Test again as CgBI
        params.apple_cgbi_format = true;
        num_tests++;
        if (!test_image(output_dir, params, verbose)) {
            num_failures++;
        }
    }
    
    printf("Success: PNG write %i of %i\n", (num_tests - num_failures), num_tests);
    return num_failures;
}
