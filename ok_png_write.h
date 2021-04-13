// ok-file-formats
// https://github.com/brackeen/ok-file-formats

#ifndef OK_PNG_WRITE_H
#define OK_PNG_WRITE_H

/**
 @file
 Functions to write an uncompressed PNG file.
 
 Caveats:
 * PNGs are not compressed.
 * No support for interlaced output.
 
 Example:
     #include "ok_png_write.h"
 
     int main() {
         const uint8_t image_data[8] = { 0xff, 0xff, 0xbd, 0xff, 0xff, 0xbd, 0xc3, 0xff };
         const uint8_t palette[6] = { 0x00, 0x00, 0x00, 0xd2, 0x32, 0x14 };
         const uint8_t palette_alpha[2] = { 0x00, 0xff };
 
         FILE *file = fopen("out_image.png", "wb");
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
    OK_PNG_WRITE_COLOR_TYPE_RGB = 2,
    OK_PNG_WRITE_COLOR_TYPE_PALETTE = 3,
    OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA = 4,
    OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA = 6,
} ok_png_write_color_type;

/**
 @struct ok_png_write_chunk
 
 Defines a PNG chunk to write.
 
 @var name A 4-character chunk name. Typical chunk names are PLTE, tRNS, and gAMA. See the PNG spec for details.
 @var data The chunk data. If length is 0, data may be NULL.
 @var length The chunk data length, in bytes.
 */
typedef struct {
    const char *name;
    const uint8_t *data;
    size_t length;
} ok_png_write_chunk;

/**
 @struct ok_png_write_params
  
 Parameters for the #ok_png_write_to_file or #ok_png_write functions.
 
 The bit depth must be valid for the color type. The following bit depths are valid:
 
 Color type                         | Valid bit depths
 -----------------------------------+-----------------
 OK_PNG_WRITE_COLOR_TYPE_PALETTE    | 1, 2, 4, 8
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 1, 2, 4, 8, 16
 OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB        | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA  | 8, 16
 
 Large uncompressed images can be written as long as one row fits in one IDAT chunk.
 For uncompressed images, the image width is limited to the following values:
 
 Color type                         | Bit depth | Max Width
 -----------------------------------+-----------+-----------
 OK_PNG_WRITE_COLOR_TYPE_PALETTE    | 1 or 2    | 4294967295
 OK_PNG_WRITE_COLOR_TYPE_PALETTE    | 4         | 4294639618
 OK_PNG_WRITE_COLOR_TYPE_PALETTE    | 8         | 2147319809
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
 Note, for 1, 2, and 4-bit images, each row must be byte-aligned.
 
 @var bit_depth The bit depth. If 0, the default is 8.
 
 @var color_type The color type.
 
 @var flip_y If true, the image is written bottom-up instead of top-down.
 
 @var apple_cgbi_format If true, write PNGs in Apple's proprietary PNG format.
 This adds the CgBI chunk before the IHDR chunk and does not include deflate headers or checksums.
 Image data must be provided in premultiplied BGRA format (ok_png_write does not convert data).
 
 @var additional_chunks A NULL-terminated array of additional chunks to write.
 The chunks are written after the header chunk ("IHDR") and before the data chunk ("IDAT").
 Images with a color type of OK_PNG_WRITE_COLOR_TYPE_PALETTE must include a "PLTE" chunk.
 See the PNG spec for details.
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint64_t data_stride;
    const uint8_t *data;
    uint8_t bit_depth;
    ok_png_write_color_type color_type;
    bool flip_y;
    bool apple_cgbi_format;
    ok_png_write_chunk **additional_chunks;
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
