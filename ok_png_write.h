// ok-file-formats
// https://github.com/brackeen/ok-file-formats

#ifndef OK_PNG_WRITE_H
#define OK_PNG_WRITE_H

/**
 @file
 Functions to write an uncompressed PNG file.
 
 Caveats:
 * PNGs are not compressed.
 * Palleted PNGs are not supported.
 
 Example:
     #include "ok_png_write.h"
 
     int main() {
         const uint8_t data[8] = { 0xff, 0xff, 0xbd, 0xff, 0xff, 0xbd, 0xc3, 0xff };
 
         FILE *file = fopen("out_image.png", "wb");
         bool success = ok_png_write_to_file(file, (ok_png_write_params){
             .width = 8,
             .height = 8,
             .data = data,
             .bit_depth = 1,
             .color_type = OK_PNG_WRITE_COLOR_TYPE_GRAY
         });
         printf("Success=%i\n", success);
         fclose(file);
         return 0;
     }
 */

#include <stdbool.h>
#include <stdint.h>
#ifndef OK_NO_STDIO
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OK_PNG_WRITE_COLOR_TYPE_GRAY = 0,
    OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA = 4,
    OK_PNG_WRITE_COLOR_TYPE_RGB = 2,
    OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA = 6,
} ok_png_write_color_type;

/**
 @struct ok_png_write_params
  
 Parameters for the #ok_png_write_to_file or #ok_png_write functions.
 
 The bit depth must be valid for the color type. The following bit depths are valid:
 
 Color type                         | Valid bit depths
 -----------------------------------+-----------------
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 1, 2, 4, 8, 16
 OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB        | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA  | 8, 16
 
 Large uncompressed images can be written as long as one row fits in one IDAT chunk.
 For uncompressed images, the image width is limited to the following values:
 
 Color type                         | Bit depth | Max Width
 -----------------------------------+-----------+-----------
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 1 or 2    | 4294967295
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 4         | 4294639618
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 8         | 2147319809
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 16        | 1073659904
 OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA | 8         | 1073659904
 OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA | 16        |  536829952
 OK_PNG_WRITE_COLOR_TYPE_RGB        | 8         |  715773269
 OK_PNG_WRITE_COLOR_TYPE_RGB        | 16        |  357886634
 OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA  | 8         |  536829952
 OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA  | 16        |  268414976
 
 @var width The width of the image, in pixels.
 
 @var height The height of the image, in pixels.
 
 @var data_stride The stride of the input image data, in bytes. If 0, the default "bytes per row" is assumed.
 
 @var data The input image data, which is written as-is without modification or verification.
 
 @var bit_depth The bit depth. If 0, the default is 8.
 
 @var color_type The color type.
 
 @var flip_y If true, the image is written bottom-up instead of top-down.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint64_t data_stride;
    const uint8_t *data;
    uint8_t bit_depth;
    ok_png_write_color_type color_type;
    bool flip_y;
} ok_png_write_params;

#ifndef OK_NO_STDIO

/// Writes an image to a file and returns true on success.
bool ok_png_write_to_file(FILE *file, ok_png_write_params params);

#endif

/// Write function for #ok_png_write. This function should return `true` on success
typedef bool (*ok_png_write_function)(void *context, const uint8_t *buffer, size_t count);

/// Writes an image and returns true on success.
bool ok_png_write(ok_png_write_function write_function, void *write_function_context, ok_png_write_params params);

#ifdef __cplusplus
}
#endif

#endif
