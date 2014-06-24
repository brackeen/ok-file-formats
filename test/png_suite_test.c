#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "test_common.h"
#include "ok_png.h"
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// This is just copied form a directory listing of the PNG Suite files
static const char *filenames[] = {
    "basi0g01",
    "basi0g02",
    "basi0g04",
    "basi0g08",
    "basi0g16",
    "basi2c08",
    "basi2c16",
    "basi3p01",
    "basi3p02",
    "basi3p04",
    "basi3p08",
    "basi4a08",
    "basi4a16",
    "basi6a08",
    "basi6a16",
    "basn0g01",
    "basn0g02",
    "basn0g04",
    "basn0g08",
    "basn0g16",
    "basn2c08",
    "basn2c16",
    "basn3p01",
    "basn3p02",
    "basn3p04",
    "basn3p08",
    "basn4a08",
    "basn4a16",
    "basn6a08",
    "basn6a16",
    "bgai4a08",
    "bgai4a16",
    "bgan6a08",
    "bgan6a16",
    "bgbn4a08",
    "bggn4a16",
    "bgwn6a08",
    "bgyn6a16",
    "ccwn2c08",
    "ccwn3p08",
    "cdfn2c08",
    "cdhn2c08",
    "cdsn2c08",
    "cdun2c08",
    "ch1n3p04",
    "ch2n3p08",
    "cm0n0g04",
    "cm7n0g04",
    "cm9n0g04",
    "cs3n2c16",
    "cs3n3p08",
    "cs5n2c08",
    "cs5n3p08",
    "cs8n2c08",
    "cs8n3p08",
    "ct0n0g04",
    "ct1n0g04",
    "cten0g04",
    "ctfn0g04",
    "ctgn0g04",
    "cthn0g04",
    "ctjn0g04",
    "ctzn0g04",
    "f00n0g08",
    "f00n2c08",
    "f01n0g08",
    "f01n2c08",
    "f02n0g08",
    "f02n2c08",
    "f03n0g08",
    "f03n2c08",
    "f04n0g08",
    "f04n2c08",
    "f99n0g04",
    "g03n0g16",
    "g03n2c08",
    "g03n3p04",
    "g04n0g16",
    "g04n2c08",
    "g04n3p04",
    "g05n0g16",
    "g05n2c08",
    "g05n3p04",
    "g07n0g16",
    "g07n2c08",
    "g07n3p04",
    "g10n0g16",
    "g10n2c08",
    "g10n3p04",
    "g25n0g16",
    "g25n2c08",
    "g25n3p04",
    "oi1n0g16",
    "oi1n2c16",
    "oi2n0g16",
    "oi2n2c16",
    "oi4n0g16",
    "oi4n2c16",
    "oi9n0g16",
    "oi9n2c16",
    "PngSuite",
    "pp0n2c16",
    "pp0n6a08",
// Xcode's pngcrush croaks on these two files. Ignore.
#if !defined(TARGET_OS_IPHONE)
    "ps1n0g08",
#endif
    "ps1n2c16",
#if !defined(TARGET_OS_IPHONE)
    "ps2n0g08",
#endif
    "ps2n2c16",
    "s01i3p01",
    "s01n3p01",
    "s02i3p01",
    "s02n3p01",
    "s03i3p01",
    "s03n3p01",
    "s04i3p01",
    "s04n3p01",
    "s05i3p02",
    "s05n3p02",
    "s06i3p02",
    "s06n3p02",
    "s07i3p02",
    "s07n3p02",
    "s08i3p02",
    "s08n3p02",
    "s09i3p02",
    "s09n3p02",
    "s32i3p04",
    "s32n3p04",
    "s33i3p04",
    "s33n3p04",
    "s34i3p04",
    "s34n3p04",
    "s35i3p04",
    "s35n3p04",
    "s36i3p04",
    "s36n3p04",
    "s37i3p04",
    "s37n3p04",
    "s38i3p04",
    "s38n3p04",
    "s39i3p04",
    "s39n3p04",
    "s40i3p04",
    "s40n3p04",
    "tbbn0g04",
    "tbbn2c16",
    "tbbn3p08",
    "tbgn2c16",
    "tbgn3p08",
    "tbrn2c08",
    "tbwn0g16",
    "tbwn3p08",
    "tbyn3p08",
    "tm3n3p02",
    "tp0n0g08",
    "tp0n2c08",
    "tp0n3p08",
    "tp1n3p08",
    "z00n2c08",
    "z03n2c08",
    "z06n2c08",
    "z09n2c08",
    // Invalid files:
    //"xc1n0g08",
    //"xc9n2c08",
    //"xcrn0g04",
    //"xcsn0g01",
    //"xd0n2c08",
    //"xd3n2c08",
    //"xd9n2c08",
    //"xdtn0g01",
    //"xhdn0g08",
    //"xlfn0g04",
    //"xs1n0g01",
    //"xs2n0g01",
    //"xs4n0g01",
    //"xs7n0g01",    
};

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

static bool test_image(read_type input_read_type,
                       const char *path_to_png_suite, 
                       const char *path_to_rgba_files,
                       const char *name) {
    const bool flip_y = false;
    const bool print_image_on_error = false;
    
    char *rgba_filename = get_full_path(path_to_rgba_files, name, "rgba");
    size_t rgba_data_length;
    uint8_t *rgba_data = read_file(rgba_filename, &rgba_data_length);
    free(rgba_filename);

#if TARGET_OS_IPHONE
    // On iOS, Apple stores PNG data with premultiplied alpha.
    // Unpremultiplying would lose precision, so instead, premultiply the raw RGBA data
    // for comparison purposes.
    ok_color_format color_format = OK_COLOR_FORMAT_RGBA_PRE;
    for (uint8_t *src = rgba_data; src < rgba_data + rgba_data_length; src += 4) {
        const uint8_t a = src[3];
        if (a == 0) {
            src[0] = 0;
            src[1] = 0;
            src[2] = 0;
        }
        else if (a < 255) {
            src[0] = (a * src[0] + 127) / 255;
            src[1] = (a * src[1] + 127) / 255;
            src[2] = (a * src[2] + 127) / 255;
        }
    }
#else
    ok_color_format color_format = OK_COLOR_FORMAT_RGBA;
#endif

    // Load via ok_png
    ok_image *image = read_image(path_to_png_suite, name, "png", input_read_type, color_format, flip_y);
    
    // Test equality
    bool success = compare(name, "png", image, rgba_data, rgba_data_length,
                           (input_read_type == READ_TYPE_INFO_ONLY), 0, print_image_on_error);
    
    // Cleanup
    if (rgba_data != NULL) {
        free(rgba_data);
    }
    ok_image_free(image);
    
    return success;
}

void png_suite_test(const char *path_to_png_suite, const char *path_to_rgba_files) {
    const int num_files = sizeof(filenames)/sizeof(filenames[0]);
    printf("Testing %i files in path \"%s\".\n", num_files, path_to_png_suite);
    
    double startTime = (double)clock()/CLOCKS_PER_SEC;
    int num_failures = 0;
    for (int i = 0; i < num_files; i++) {
        for (int j = 0; j < READ_TYPE_COUNT; j++) {
            bool success = test_image(j, path_to_png_suite, path_to_rgba_files, filenames[i]);
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
