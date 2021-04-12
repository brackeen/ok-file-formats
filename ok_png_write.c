// ok-file-formats
// https://github.com/brackeen/ok-file-formats

#include "ok_png_write.h"

#ifndef ok_assert
#include <assert.h>
#define ok_assert(expression) assert(expression);
#endif

#ifndef OK_PNG_WRITE_IDAT_MAX_LENGTH
// "Although encoders and decoders should treat the length as unsigned, its value must not exceed 2^31-1 bytes."
// To test splitting output into multiple IDAT chunks, this can be redefined as a smaller value.
#define OK_PNG_WRITE_IDAT_MAX_LENGTH 0x7fffffff
#endif

#define ok_min(a, b) ((a) < (b) ? (a) : (b))

// MARK: PNG write to FILE

#ifndef OK_NO_STDIO

static bool ok_file_write(void *user_data, const uint8_t *buffer, size_t length) {
    return fwrite(buffer, 1, length, (FILE *)user_data) == length;
}

bool ok_png_write_to_file(FILE *file, ok_png_write_params image_data) {
    ok_assert(file != NULL);
    if (!file) {
        return false;
    }
    return ok_png_write(ok_file_write, file, image_data);
}

#endif

// MARK: CRC

static uint32_t ok_crc_table[256];

static void ok_crc_table_initialize(void) {
    static bool ok_crc_table_initialized = false;
    if (ok_crc_table_initialized) {
        return;
    }
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (uint32_t k = 0; k < 8; k++) {
            if (c & 1) {
                c = 0xedb88320L ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        ok_crc_table[n] = c;
    }
    ok_crc_table_initialized = true;
}

static void ok_crc_update(uint32_t *crc, const uint8_t *buffer, size_t length) {
    uint32_t c = (*crc) ^ 0xffffffffL;
    for (uint32_t n = 0; n < length; n++) {
        c = ok_crc_table[(c ^ buffer[n]) & 0xff] ^ (c >> 8);
    }
    *crc = c ^ 0xffffffffL;
}

// MARK: Adler

static uint32_t ok_adler_init(void) {
    return 1;
}

static uint32_t ok_adler_update(const uint32_t adler, const uint8_t *buffer, size_t length) {
    static const uint32_t adler_base = 65521;
    static const size_t adler_max_run_length = 5552;
    
    uint32_t adler_sum1 = adler & 0xffff;
    uint32_t adler_sum2 = (adler >> 16) & 0xffff;
    
    if (length == 1) {
        adler_sum1 += buffer[0];
        if (adler_sum1 >= adler_base) {
            adler_sum1 -= adler_base;
        }
        adler_sum2 += adler_sum1;
        if (adler_sum2 >= adler_base) {
            adler_sum2 -= adler_base;
        }
    } else {
        for (size_t i = 0; i < length; i += adler_max_run_length) {
            const size_t end = i + ok_min(length - i, adler_max_run_length);
            for (size_t j = i; j < end; j++) {
                adler_sum1 += buffer[j];
                adler_sum2 += adler_sum1;
            }
            adler_sum1 %= adler_base;
            adler_sum2 %= adler_base;
        }
    }
    return (adler_sum2 << 16) | adler_sum1;
}

// MARK: Write helper macros

// These macros require write_function and write_function_context be defined.
// Each macro returns false on failure.

#define ok_write(buffer, length, crc) \
    if (!write_function(write_function_context, (buffer), (length))) return false; \
    ok_crc_update(crc, (buffer), (length));

#define ok_write_uint8(value, crc) {\
    uint8_t buffer[1] = { (uint8_t)(value) }; \
    ok_write(buffer, sizeof(buffer), crc); \
}

#define ok_write_uint16(value, crc) {\
    uint8_t buffer[2] = { (uint8_t)((value) >> 8), (uint8_t)((value) & 0xff) }; \
    ok_write(buffer, sizeof(buffer), crc); \
}

#define ok_write_uint32(value, crc) {\
    uint8_t buffer[4] = { (uint8_t)((value) >> 24), (uint8_t)((value) >> 16), (uint8_t)((value) >> 8), (uint8_t)((value) & 0xff) }; \
    ok_write(buffer, sizeof(buffer), crc); \
}

#define ok_write_chunk_start(name, length, crc) \
    ok_write_uint32(length, crc); \
    *crc = 0; \
    ok_write((const uint8_t *)(name), 4, crc);

#define ok_write_chunk_end(crc) \
    ok_write_uint32(crc, &crc);

// MARK: PNG write

// Palette type not supported, as no PLTE or tRNS chunks are written
#define OK_PNG_WRITE_COLOR_TYPE_PALETTE 3

static uint8_t ok_color_type_channels(ok_png_write_color_type color_type) {
    switch (color_type) {
        case OK_PNG_WRITE_COLOR_TYPE_GRAY: default:
            return 1;
        case OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA:
            return 2;
        case OK_PNG_WRITE_COLOR_TYPE_RGB:
            return 3;
        case OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA:
            return 4;
    }
}

/*
 Writes uncompressed data, splitting the data into blocks and chunks.
 "chunk" = IDAT (max size 0x7fffffff)
 "block" = deflate stored block (max size 0xffff)
 For wide images, each row is split into multiple blocks.
 Otherwise, each block contains multiple rows.
 */
static bool ok_png_write_uncompressed_idat(ok_png_write_function write_function, void *write_function_context, ok_png_write_params image) {
    const uint16_t deflate_stored_block_max_length = 0xffff;
    const uint32_t deflate_header_length = 2;
    const uint32_t deflate_footer_length = 4; // adler-32
    const uint8_t bits_per_pixel = image.bit_depth * ok_color_type_channels(image.color_type);
    const uint64_t bytes_per_row = ((uint64_t)image.width * bits_per_pixel + 7) / 8;
    const uint64_t output_row_length = bytes_per_row + 1; // 1 byte for filter
    bool add_extra_chunk_for_adler = false;
    uint32_t adler = ok_adler_init();
    uint32_t crc = 0;
    
    uint64_t deflate_num_full_blocks_per_row;
    uint16_t deflate_full_block_length;
    uint16_t deflate_small_block_length;
    uint64_t deflate_data_iteration_full_length;
    uint32_t rowInc;
    if (output_row_length > deflate_stored_block_max_length) {
        // Write uncompressed data as N blocks per row
        rowInc = 1;
        deflate_num_full_blocks_per_row = output_row_length / deflate_stored_block_max_length;
        deflate_full_block_length = deflate_stored_block_max_length;
        deflate_small_block_length = (uint16_t)(output_row_length % deflate_full_block_length);
        deflate_data_iteration_full_length = (deflate_num_full_blocks_per_row * (deflate_full_block_length + 5) + // 5 for block header
                                              (deflate_small_block_length > 0 ? (deflate_small_block_length + 5) : 0)); // 5 for block header
        bool oneImageRowFitsInOneIDATChunk = deflate_header_length + deflate_data_iteration_full_length <= OK_PNG_WRITE_IDAT_MAX_LENGTH;
        ok_assert(oneImageRowFitsInOneIDATChunk); // Image width is too large
        if (!oneImageRowFitsInOneIDATChunk) {
            return false;
        }
    } else {
        // Write uncompressed data as N rows per block
        rowInc = ok_min(image.height, deflate_stored_block_max_length / output_row_length);
        deflate_num_full_blocks_per_row = 0;
        deflate_full_block_length = (uint16_t)(output_row_length * rowInc);
        deflate_small_block_length = (uint16_t)(output_row_length * (image.height % rowInc));
        deflate_data_iteration_full_length = 5 + deflate_full_block_length; // 5 for block header
    }
    
    uint32_t iterations_until_next_idat = 0;
    for (uint32_t row = 0; row < image.height; row += rowInc) {
        if (iterations_until_next_idat == 0) {
            // Determine idat length
            uint32_t idat_length;
            {
                const uint32_t deflate_current_header_length = row == 0 ? deflate_header_length : 0;
                const uint32_t remaining_full_iterations = (image.height - row) / rowInc;
                uint32_t remaining_iterations;
                uint64_t remaining_data_length;

                if (output_row_length > deflate_stored_block_max_length) {
                    remaining_iterations = remaining_full_iterations;
                    remaining_data_length = (deflate_current_header_length +
                                             remaining_full_iterations * deflate_data_iteration_full_length);
                } else {
                    remaining_iterations = remaining_full_iterations + (deflate_small_block_length > 0 ? 1 : 0);
                    remaining_data_length = (deflate_current_header_length +
                                             remaining_full_iterations * (deflate_full_block_length + 5) + // 5 for block header
                                             (deflate_small_block_length > 0 ? (deflate_small_block_length + 5) : 0)); // 5 for block header
                }
                
                if (remaining_data_length + deflate_footer_length <= OK_PNG_WRITE_IDAT_MAX_LENGTH) {
                    iterations_until_next_idat = remaining_iterations;
                    idat_length = (uint32_t)(remaining_data_length + deflate_footer_length);
                } else {
                    iterations_until_next_idat = (OK_PNG_WRITE_IDAT_MAX_LENGTH - deflate_current_header_length) / deflate_data_iteration_full_length;
                    if (iterations_until_next_idat >= remaining_iterations ||
                        remaining_data_length <= OK_PNG_WRITE_IDAT_MAX_LENGTH) {
                        // Everything fits but the footer (adler-32).
                        iterations_until_next_idat = remaining_iterations;
                        idat_length = (uint32_t)remaining_data_length;
                        add_extra_chunk_for_adler = true;
                    } else {
                        idat_length = (uint32_t)(deflate_current_header_length + iterations_until_next_idat * deflate_data_iteration_full_length);
                    }
                }
            }
            ok_write_chunk_start("IDAT", idat_length, &crc);

            // Write deflate header
            if (row == 0) {
                const uint16_t deflate_compression_info = 0x7; // 4 bits
                const uint16_t deflate_compression_method = 0x8; // 4 bits
                const uint16_t deflate_flag_compression_level = 0; // 2 bits
                const uint16_t deflate_flag_dict = 0; // 1 bit
                const uint16_t deflate_raw_header = ((deflate_compression_info << 12) |
                                                     (deflate_compression_method << 8) |
                                                     (deflate_flag_compression_level << 6) |
                                                     (deflate_flag_dict << 5));
                const uint16_t deflate_flag_check = 31 - (deflate_raw_header % 31); // 5 bits (ensure deflate_header is divisible by 31)
                const uint16_t deflate_header = deflate_raw_header | deflate_flag_check;
                ok_write_uint16(deflate_header, &crc);
            }
        }
        
        if (output_row_length > deflate_stored_block_max_length) {
            // Write uncompressed data as N blocks per row
            for (uint32_t i = 0; i < output_row_length; i += deflate_full_block_length) {
                const uint16_t deflate_block_length = (uint16_t)ok_min(deflate_full_block_length, output_row_length - i);
                
                // Write deflate block header (5 bytes)
                const uint8_t deflate_block_type = 0; // 2 bits (no compression)
                const uint8_t deflate_final_block_flag = row == image.height - 1 && i + deflate_block_length == output_row_length ? 1 : 0; // 1 bit
                const uint8_t deflate_block_header[5] = {
                    deflate_block_type << 1 | deflate_final_block_flag,
                    (deflate_block_length & 0xff), (deflate_block_length >> 8),
                    ((~deflate_block_length) & 0xff), ((~deflate_block_length) >> 8),
                };
                ok_write(deflate_block_header, sizeof(deflate_block_header), &crc);
                
                // Write filter and determine x, num_bytes
                uint32_t x;
                uint32_t num_bytes;
                if (i == 0) {
                    x = 0;
                    num_bytes = deflate_block_length - 1;
                    const uint8_t filter = 0;
                    ok_write_uint8(filter, &crc);
                    adler = ok_adler_update(adler, &filter, 1);
                } else {
                    x = i - 1;
                    num_bytes = deflate_block_length;
                }
                
                // Write row data
                const uint8_t *src;
                if (image.flip_y) {
                    src = image.data + x + (image.height - 1 - row) * image.data_stride;
                } else {
                    src = image.data + x + row * image.data_stride;
                }
                ok_write(src, num_bytes, &crc);
                
                // Update adler32
                adler = ok_adler_update(adler, src, num_bytes);
            }
        } else {
            // Write uncompressed data as N rows per block
            const uint32_t num_rows = ok_min(rowInc, image.height - row);
            const uint16_t deflate_block_length = (uint16_t)(output_row_length * num_rows);
            
            // Write deflate block header (5 bytes)
            const uint8_t deflate_block_type = 0; // 2 bits (no compression)
            const uint8_t deflate_final_block_flag = row + num_rows == image.height ? 1 : 0; // 1 bit
            const uint8_t deflate_block_header[5] = {
                deflate_block_type << 1 | deflate_final_block_flag,
                (deflate_block_length & 0xff), (deflate_block_length >> 8),
                ((~deflate_block_length) & 0xff), ((~deflate_block_length) >> 8),
            };
            ok_write(deflate_block_header, sizeof(deflate_block_header), &crc);
            
            // Write row data
            for (uint32_t y = row; y < row + num_rows; y++) {
                const uint8_t filter = 0;
                const uint8_t *src;
                if (image.flip_y) {
                    src = image.data + (image.height - 1 - y) * image.data_stride;
                } else {
                    src = image.data + y * image.data_stride;
                }
                ok_write_uint8(filter, &crc);
                ok_write(src, (size_t)bytes_per_row, &crc);
                
                // Update adler32
                adler = ok_adler_update(adler, &filter, 1);
                adler = ok_adler_update(adler, src, bytes_per_row);
            }
        }

        // Finish IDAT chunk
        iterations_until_next_idat--;
        if (iterations_until_next_idat == 0) {
            if (row + rowInc >= image.height && !add_extra_chunk_for_adler) {
                ok_write_uint32(adler, &crc);
            }
            ok_write_chunk_end(crc);
        }
    }

    // Write adler-32
    if (add_extra_chunk_for_adler) {
        ok_write_chunk_start("IDAT", 4, &crc);
        ok_write_uint32(adler, &crc);
        ok_write_chunk_end(crc);
    }
    return true;
}

bool ok_png_write(ok_png_write_function write_function, void *write_function_context, ok_png_write_params image) {
    // Set default parameters
    if (image.bit_depth == 0) {
        image.bit_depth = 8;
    }
    const uint8_t bits_per_pixel = image.bit_depth * ok_color_type_channels(image.color_type);
    const uint64_t bytes_per_row = ((uint64_t)image.width * bits_per_pixel + 7) / 8;
    if (image.data_stride == 0) {
        image.data_stride = bytes_per_row;
    }
    
    // Validate input parameters
    {
        const int b = image.bit_depth;
        const bool valid_bit_depth =
            (image.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY && (b == 1 || b == 2 || b == 4 || b == 8 || b == 16)) ||
            (image.color_type == OK_PNG_WRITE_COLOR_TYPE_RGB && (b == 8 || b == 16)) ||
            (image.color_type == OK_PNG_WRITE_COLOR_TYPE_PALETTE && (b == 1 || b == 2 || b == 4 || b == 8)) ||
            (image.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA && (b == 8 || b == 16)) ||
            (image.color_type == OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA && (b == 8 || b == 16));
        ok_assert(image.width > 0);
        ok_assert(image.height > 0);
        ok_assert(image.data != NULL);
        ok_assert(image.data_stride >= bytes_per_row); // stride is too small
        ok_assert(valid_bit_depth);
        ok_assert(write_function != NULL);
        if (image.width == 0 || image.height == 0 || image.data == NULL ||
            image.data_stride < bytes_per_row || !valid_bit_depth ||
            write_function == NULL) {
            return false;
        }
    }

    // Initialize CRC table
    uint32_t crc = 0;
    ok_crc_table_initialize();

    // Write PNG signature
    const uint8_t png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    ok_write(png_signature, sizeof(png_signature), &crc);
    
    // Write IHDR chunk
    ok_write_chunk_start("IHDR", 13, &crc);
    ok_write_uint32(image.width, &crc);
    ok_write_uint32(image.height, &crc);
    ok_write_uint8(image.bit_depth, &crc);
    ok_write_uint8(image.color_type, &crc);
    ok_write_uint8(0, &crc); // compression method
    ok_write_uint8(0, &crc); // filter method
    ok_write_uint8(0, &crc); // interlace method
    ok_write_chunk_end(crc);
    
    // Write IDAT chunk(s)
    if (!ok_png_write_uncompressed_idat(write_function, write_function_context, image)) {
        return false;
    }
    
    // Write IEND chunk
    ok_write_chunk_start("IEND", 0, &crc);
    ok_write_chunk_end(crc);
    return true;
}
