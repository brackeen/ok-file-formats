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
    uint32_t length;
} ok_png_write_chunk;

/**
 @struct ok_png_write_params
  
 Parameters for the #ok_png_write_to_file or #ok_png_write functions.
 
 @var width The width of the image, in pixels.
 
 @var height The height of the image, in pixels.
 
 @var data_stride The stride of the input image data, in bytes. If 0, the default "bytes per row" is assumed.
 
 @var data The input image data, which is written as-is without modification or verification.
 Note, even for 1, 2, and 4-bit images, rows must be byte-aligned.
 
 @var bit_depth The bit depth. If 0, the default is 8.
 
 The bit depth must be valid for the color type. The following bit depths are valid:
 
 Color type                         | Valid bit depths
 -----------------------------------+-----------------
 OK_PNG_WRITE_COLOR_TYPE_PALETTE    | 1, 2, 4, 8
 OK_PNG_WRITE_COLOR_TYPE_GRAY       | 1, 2, 4, 8, 16
 OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB        | 8, 16
 OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA  | 8, 16
  
 @var color_type The color type.
 
 @var flip_y If true, the image is written bottom-up instead of top-down.
 
 @var apple_cgbi_format If true, write PNGs in Apple's proprietary PNG format.
 This adds the CgBI chunk before the IHDR chunk and does not include zlib headers or checksums.
 Image data must be provided in premultiplied BGRA format (ok_png_write does not convert data).
 
 @var additional_chunks A NULL-terminated array of additional chunks to write.
 The chunks are written after the header chunk ("IHDR") and before the data chunk ("IDAT").
 Images with a color type of OK_PNG_WRITE_COLOR_TYPE_PALETTE must include a "PLTE" chunk.
 See the PNG spec for details.
 
 @var buffer_size The size of the buffer for compressed IDAT chunks. If the compressed output
 is larger than the buffer, multiple IDAT chunks are written.
 If this value is 0, a default value of 65536 is used. This value must be 0x7fffffff or less.
 
 @var alloc The memory allocator, or NULL to use the stdlib's malloc.
 
 @var free The memory deallocator, or NULL to use the stdlib's free.
 
 @var allocator_context The context passed to the allocator.
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
    
    // Compress options
    uint32_t buffer_size;
    void *(*alloc)(void *allocator_context, size_t length);
    void (*free)(void *allocator_context, void *memory);
    void *allocator_context;
} ok_png_write_params;

#ifndef OK_NO_STDIO

/// Writes an image to a file and returns true on success.
bool ok_png_write_to_file(FILE *file, ok_png_write_params params);

#endif

/// Write function for #ok_png_write. This function should return `true` on success
typedef bool (*ok_png_write_function)(void *context, const uint8_t *buffer, size_t count);

/// Writes an image and returns true on success.
bool ok_png_write(ok_png_write_function write_function, void *write_function_context, ok_png_write_params params);

// MARK: Deflate

typedef struct ok_deflate ok_deflate;

/**
 @var nowrap If `true`, no header or footer is written before or after the output.

 @var alloc The memory allocator, or NULL to use the stdlib's malloc.
 
 @var free The memory deallocator, or NULL to use the stdlib's free.
 
 @var allocator_context The context passed to the allocator.
 
 @var write The write function for output. Must not be NULL.
 This function should write the specified buffer and return true on success.
 
 @var write_context The context passed to the write function.
*/
typedef struct {
    bool nowrap;
    
    void *(*alloc)(void *allocator_context, size_t length);
    void (*free)(void *allocator_context, void *memory);
    void *allocator_context;
    
    bool (*write)(void *write_context, const uint8_t *data, size_t length);
    void *write_context;
} ok_deflate_params;

/// Create a deflater. The object must be freed with #ok_deflate_free
ok_deflate *ok_deflate_init(ok_deflate_params params);

/**
 Deflates data.

 This function may be called repeatedly.

 If `is_final` is true, this data represents the final data block in the stream.
 The footer is written (if nowrap is false) and the stream is reset. Another stream
 can be written using the same deflater.
 
 If `is_final` is false, some or all of the data may be buffered.

 @param data The data to deflate.
 @param length The data length. May be zero.
 @param is_final Flag indicating the data is the last in a stream.
 @return true on success, false on write error.
 */
bool ok_deflate_data(ok_deflate *deflate, const uint8_t *data, size_t length, bool is_final);

/// Frees a deflater created with #ok_deflate_init
void ok_deflate_free(ok_deflate *deflate);

#ifdef __cplusplus
}
#endif

#endif
