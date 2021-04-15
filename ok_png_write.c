// ok-file-formats
// https://github.com/brackeen/ok-file-formats

#include "ok_png_write.h"
#include <string.h>

#ifndef ok_assert
#include <assert.h>
#define ok_assert(expression) assert(expression);
#endif

#ifndef OK_NO_DEFAULT_ALLOCATOR
#include <stdlib.h>

static void *ok_stdlib_alloc(void *context, size_t length) {
    (void)context;
    return malloc(length);
}

static void ok_stdlib_free(void *context, void *memory) {
    (void)context;
    free(memory);
}

#endif

// "Although encoders and decoders should treat the length as unsigned, its value must not exceed 2^31-1 bytes."
// To test splitting output into multiple IDAT chunks, this can be redefined as a smaller value.
#define OK_PNG_WRITE_CHUNK_MAX_LENGTH 0x7fffffff

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

// MARK: PNG writer helper functions

static inline void ok_uint16_to_bytes(uint16_t value, uint8_t bytes[2]) {
    bytes[0] = value >> 8;
    bytes[1] = value & 0xff;
}

static inline void ok_uint32_to_bytes(uint32_t value, uint8_t bytes[4]) {
    bytes[0] = value >> 24;
    bytes[1] = (value >> 16) & 0xff;
    bytes[2] = (value >> 8) & 0xff;
    bytes[3] = value & 0xff;
}

static bool ok_png_chunk_write(const char *name, const uint8_t *data, uint32_t length,
                               ok_png_write_function write_function, void *write_function_context) {
    ok_assert(name != NULL && strlen(name) == 4);
    
    uint32_t crc = 0;
    
    // Header (length, name)
    uint8_t chunk_header[8];
    ok_uint32_to_bytes(length, chunk_header);
    chunk_header[4] = (uint8_t)name[0];
    chunk_header[5] = (uint8_t)name[1];
    chunk_header[6] = (uint8_t)name[2];
    chunk_header[7] = (uint8_t)name[3];
    if (!write_function(write_function_context, chunk_header, sizeof(chunk_header))) {
        return false;
    }
    ok_crc_update(&crc, chunk_header + 4, 4);
    
    // Data
    if (length > 0) {
        if (!write_function(write_function_context, data, length)) {
            return false;
        }
        ok_crc_update(&crc, data, length);
    }
    
    // CRC
    uint8_t chunk_footer[4];
    ok_uint32_to_bytes(crc, chunk_footer);
    return write_function(write_function_context, chunk_footer, sizeof(chunk_footer));
}

static uint8_t ok_color_type_channels(ok_png_write_color_type color_type) {
    switch (color_type) {
        case OK_PNG_WRITE_COLOR_TYPE_GRAY:
        case OK_PNG_WRITE_COLOR_TYPE_PALETTE:
        default:
            return 1;
        case OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA:
            return 2;
        case OK_PNG_WRITE_COLOR_TYPE_RGB:
            return 3;
        case OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA:
            return 4;
    }
}

// MARK: Deflate output buffer

typedef struct {
    uint8_t *data;
    uint32_t length;
    uint32_t capacity;
    
    ok_png_write_function write_function;
    void *write_function_context;
} ok_png_deflate_output_buffer;

static bool ok_png_deflate_output_buffer_write(void *write_context, const uint8_t *data, size_t length) {
    while (length > 0) {
        ok_png_deflate_output_buffer *output_buffer = (ok_png_deflate_output_buffer *)write_context;
        const size_t copy_length = ok_min(length, output_buffer->capacity - output_buffer->length);
        memcpy(output_buffer->data + output_buffer->length, data, copy_length);
        output_buffer->length += copy_length;
        data += copy_length;
        length -= copy_length;
        if (output_buffer->length == output_buffer->capacity) {
            ok_png_chunk_write("IDAT", output_buffer->data, output_buffer->length,
                               output_buffer->write_function, output_buffer->write_function_context);
            output_buffer->length = 0;
        }
    }
    return true;
}

// MARK: PNG write

bool ok_png_write(ok_png_write_function write_function, void *write_function_context, ok_png_write_params params) {
    // Set default parameters
    if (params.bit_depth == 0) {
        params.bit_depth = 8;
    }
    if (params.buffer_size == 0) {
        params.buffer_size = 0x10000;
    }
    const uint8_t bits_per_pixel = params.bit_depth * ok_color_type_channels(params.color_type);
    const uint64_t bytes_per_row = ((uint64_t)params.width * bits_per_pixel + 7) / 8;
    if (params.data_stride == 0) {
        params.data_stride = bytes_per_row;
    }
    
#ifndef OK_NO_DEFAULT_ALLOCATOR
    if (params.alloc == NULL && params.free == NULL) {
        params.alloc = ok_stdlib_alloc;
        params.free = ok_stdlib_free;
    }
#endif
    
    // Validate input parameters
    {
        const int b = params.bit_depth;
        const bool valid_bit_depth =
            (params.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY && (b == 1 || b == 2 || b == 4 || b == 8 || b == 16)) ||
            (params.color_type == OK_PNG_WRITE_COLOR_TYPE_RGB && (b == 8 || b == 16)) ||
            (params.color_type == OK_PNG_WRITE_COLOR_TYPE_PALETTE && (b == 1 || b == 2 || b == 4 || b == 8)) ||
            (params.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA && (b == 8 || b == 16)) ||
            (params.color_type == OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA && (b == 8 || b == 16));
        ok_assert(params.width > 0);
        ok_assert(params.height > 0);
        ok_assert(params.data != NULL);
        ok_assert(params.data_stride >= bytes_per_row); // stride is too small
        ok_assert(valid_bit_depth);
        ok_assert(params.buffer_size <= OK_PNG_WRITE_CHUNK_MAX_LENGTH); // buffer too large
        ok_assert(write_function != NULL);
        if (params.width == 0 || params.height == 0 || params.data == NULL ||
            params.data_stride < bytes_per_row || !valid_bit_depth ||
            params.buffer_size > OK_PNG_WRITE_CHUNK_MAX_LENGTH ||
            write_function == NULL) {
            return false;
        }
        
        const bool has_allocator = params.alloc != NULL && params.free != NULL;
        ok_assert(has_allocator);
        if (!has_allocator) {
            return NULL;
        }
    }
    
    // Validate additional chunks
    bool custom_cgbi_chunk = false;
    bool custom_ihdr_chunk = false;
    bool custom_idat_chunk = false;
    bool custom_iend_chunk = false;
    size_t palette_color_count = 0;
    if (params.additional_chunks) {
        bool trns_found = false;
        for (ok_png_write_chunk **chunk_ptr = params.additional_chunks; *chunk_ptr != NULL; chunk_ptr++) {
            ok_png_write_chunk *chunk = *chunk_ptr;
            
            // Valid name
            bool valid_chunk_name = chunk->name != NULL && strlen(chunk->name) == 4;
            ok_assert(valid_chunk_name);
            if (!valid_chunk_name) {
                return false;
            }
            
            // Valid data
            bool valid_chunk_data = (chunk->data == NULL && chunk->length == 0) || (chunk->data != NULL && chunk->length > 0);
            ok_assert(valid_chunk_data);
            if (!valid_chunk_data) {
                return false;
            }
            
            // Valid data length
            bool valid_chunk_data_length = chunk->length <= OK_PNG_WRITE_CHUNK_MAX_LENGTH;
            ok_assert(valid_chunk_data_length);
            if (!valid_chunk_data_length) {
                return false;
            }
            
            // Allow custom versions of all primary chunks (don't validate)
            custom_ihdr_chunk = strcmp("IHDR", chunk->name) == 0;
            custom_idat_chunk = strcmp("IDAT", chunk->name) == 0;
            custom_iend_chunk = strcmp("IEND", chunk->name) == 0;
            custom_cgbi_chunk = strcmp("CgBI", chunk->name) == 0;
            
            // Validate PLTE chunk
            if (strcmp("PLTE", chunk->name) == 0) {
                ok_assert(palette_color_count == 0); // PLTE chunk already added
                if (palette_color_count > 0) {
                    return false;
                }
                ok_assert(!trns_found); // PLTE chunk must appear before tRNS chunk
                if (trns_found) {
                    return false;
                }
                
                // Valid color type
                bool valid_color_type = !(params.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY ||
                                          params.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA);
                ok_assert(valid_color_type);
                if (!valid_color_type) {
                    return false;
                }
                
                // Valid length
                size_t max_color_count;
                switch (params.color_type) {
                    case OK_PNG_WRITE_COLOR_TYPE_PALETTE:
                        max_color_count = 1 << params.bit_depth;
                        break;
                    case OK_PNG_WRITE_COLOR_TYPE_RGB:
                    case OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA:
                        max_color_count = 256;
                        break;
                    case OK_PNG_WRITE_COLOR_TYPE_GRAY:
                    case OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA:
                    default:
                        max_color_count = 0;
                        break;
                }
                palette_color_count = chunk->length / 3;
                bool valid_palette_length = palette_color_count <= max_color_count && chunk->length == palette_color_count * 3;
                ok_assert(valid_palette_length);
                if (!valid_palette_length) {
                    return false;
                }
            }
            
            // Validate tRNS chunk
            if (strcmp("tRNS", chunk->name) == 0) {
                ok_assert(!trns_found); // tRNS chunk already added
                if (trns_found) {
                    return false;
                }
                bool palette_missing = params.color_type == OK_PNG_WRITE_COLOR_TYPE_PALETTE && palette_color_count == 0;
                ok_assert(!palette_missing);
                if (palette_missing) {
                    return false;
                }
                
                // Valid color type
                bool valid_color_type = (params.color_type == OK_PNG_WRITE_COLOR_TYPE_PALETTE ||
                                         params.color_type == OK_PNG_WRITE_COLOR_TYPE_GRAY ||
                                         params.color_type == OK_PNG_WRITE_COLOR_TYPE_RGB);
                ok_assert(valid_color_type);
                if (!valid_color_type) {
                    return false;
                }
                
                // Valid length
                size_t min_length;
                size_t max_length;
                switch (params.color_type) {
                    case OK_PNG_WRITE_COLOR_TYPE_PALETTE:
                        min_length = 1;
                        max_length = palette_color_count;
                        break;
                    case OK_PNG_WRITE_COLOR_TYPE_GRAY:
                        min_length = 2; max_length = 2; // Single-color transparency, 16-bit key
                        break;
                    case OK_PNG_WRITE_COLOR_TYPE_RGB:
                        min_length = 6; max_length = 6; // Single-color transparency, 16-bit key
                        break;
                    case OK_PNG_WRITE_COLOR_TYPE_GRAY_ALPHA:
                    case OK_PNG_WRITE_COLOR_TYPE_RGB_ALPHA:
                    default:
                        min_length = 0; max_length = 0;
                        break;
                }
                
                bool valid_trns_length = chunk->length >= min_length && chunk->length <= max_length;
                ok_assert(valid_trns_length);
                if (!valid_trns_length) {
                    return false;
                }
                trns_found = true;
            }
        }
    }
    if (params.color_type == OK_PNG_WRITE_COLOR_TYPE_PALETTE) {
        ok_assert(palette_color_count != 0); // PLTE not found
        if (palette_color_count == 0) {
            return false;
        }
    }
    
    // Initialize CRC table
    ok_crc_table_initialize();

    // Write PNG signature
    const uint8_t png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (!write_function(write_function_context, png_signature, sizeof(png_signature))) {
        return false;
    }
    
    // Write CgBI chunk
    if (params.apple_cgbi_format && !custom_cgbi_chunk) {
        const uint8_t cgbi_data[4] = { 0x50, 0x00, 0x20, 0x02 }; // lower 16 bits are probably CGBitmapInfo mask
        if (!ok_png_chunk_write("CgBI", cgbi_data, sizeof(cgbi_data),write_function, write_function_context)) {
            return false;
        }
    }
    
    // Write IHDR chunk
    if (!custom_ihdr_chunk) {
        uint8_t ihdr_data[13];
        ok_uint32_to_bytes(params.width, ihdr_data + 0);
        ok_uint32_to_bytes(params.height, ihdr_data + 4);
        ihdr_data[8] = params.bit_depth;
        ihdr_data[9] = (uint8_t)params.color_type;
        ihdr_data[10] = 0; // compression method
        ihdr_data[11] = 0; // filter method
        ihdr_data[12] = 0; // interlace method
        if (!ok_png_chunk_write("IHDR", ihdr_data, sizeof(ihdr_data), write_function, write_function_context)) {
            return false;
        }
    }
    
    // Write additional chunks
    if (params.additional_chunks) {
        for (ok_png_write_chunk **chunk_ptr = params.additional_chunks; *chunk_ptr != NULL; chunk_ptr++) {
            ok_png_write_chunk *chunk = *chunk_ptr;
            if (!ok_png_chunk_write(chunk->name, chunk->data, chunk->length, write_function, write_function_context)) {
                return false;
            }
        }
    }
    
    // Write IDAT chunk(s)
    if (!custom_idat_chunk) {
        // Create output buffer for deflater
        ok_png_deflate_output_buffer deflate_output_buffer;
        deflate_output_buffer.data = (uint8_t *)params.alloc(params.allocator_context, params.buffer_size);
        deflate_output_buffer.length = 0;
        deflate_output_buffer.capacity = params.buffer_size;
        deflate_output_buffer.write_function = write_function;
        deflate_output_buffer.write_function_context = write_function_context;
        if (deflate_output_buffer.data == NULL) {
            return false;
        }
        
        // Create deflater
        ok_deflate *deflate = ok_deflate_init((ok_deflate_params){
            .nowrap = params.apple_cgbi_format || custom_cgbi_chunk,
            .alloc = params.alloc,
            .free = params.free,
            .allocator_context = params.allocator_context,
            .write = ok_png_deflate_output_buffer_write,
            .write_context = &deflate_output_buffer,
        });
        if (deflate == NULL) {
            params.free(params.allocator_context, deflate_output_buffer.data);
            return false;
        }
       
        // Deflate data, one row at a time. Write a IDAT chunk when deflate_output_buffer is full
        bool idat_success = true;
        for (uint32_t y = 0; idat_success && y < params.height; y++) {
            const uint8_t filter = 0;
            const uint8_t *src;
            if (params.flip_y) {
                src = params.data + (params.height - 1 - y) * params.data_stride;
            } else {
                src = params.data + y * params.data_stride;
            }
            idat_success &= ok_deflate_data(deflate, &filter, 1, false);
            idat_success &= ok_deflate_data(deflate, src, bytes_per_row, y == params.height - 1);
        }
        ok_deflate_free(deflate);
        
        // Write final IDAT chunk
        if (idat_success && deflate_output_buffer.length > 0) {
            idat_success = ok_png_chunk_write("IDAT", deflate_output_buffer.data, deflate_output_buffer.length,
                                              write_function, write_function_context);
        }
        params.free(params.allocator_context, deflate_output_buffer.data);
        if (!idat_success) {
            return false;
        }
    }
    
    // Write IEND chunk
    return custom_iend_chunk || ok_png_chunk_write("IEND", NULL, 0, write_function, write_function_context);
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


// MARK: Deflate

#define OK_DEFLATE_BUFFER_LENGTH 0xffff
#define OK_DEFLATE_BLOCK_TYPE_NO_COMPRESSION 0
//#define OK_DEFLATE_BLOCK_TYPE_FIXED_HUFFMAN 1
//#define OK_DEFLATE_BLOCK_TYPE_DYNAMIC_HUFFMAN 2

struct ok_deflate {
    ok_deflate_params params;
    bool header_written;
    uint32_t adler;
    uint32_t bit_buffer;
    uint8_t bit_buffer_length;
    uint16_t buffer_length;
    uint8_t buffer[OK_DEFLATE_BUFFER_LENGTH];
};

ok_deflate *ok_deflate_init(ok_deflate_params params) {
#ifndef OK_NO_DEFAULT_ALLOCATOR
    if (params.alloc == NULL && params.free == NULL) {
        params.alloc = ok_stdlib_alloc;
        params.free = ok_stdlib_free;
    }
#endif
    bool has_allocator = params.alloc != NULL && params.free != NULL;
    ok_assert(has_allocator);
    if (!has_allocator) {
        return NULL;
    }
    
    bool has_write_function = params.write != NULL;
    ok_assert(has_write_function);
    if (!has_write_function) {
        return NULL;
    }

    ok_deflate *deflate = params.alloc(params.allocator_context, sizeof(ok_deflate));
    if (deflate) {
        deflate->params = params;
        deflate->header_written = false;
        deflate->adler = ok_adler_init();
        deflate->bit_buffer = 0;
        deflate->bit_buffer_length = 0;
        deflate->buffer_length = 0;
    }
    return deflate;
}

static bool ok_deflate_bit_buffer_flush(ok_deflate *deflate) {
    while (deflate->bit_buffer_length >= 8) {
        uint8_t v = deflate->bit_buffer & 0xff;
        if (!deflate->params.write(deflate->params.write_context, &v, 1)) {
            return false;
        }
        deflate->bit_buffer >>= 8;
        deflate->bit_buffer_length -= 8;
    }
    return true;
}

static bool ok_deflate_write_bits(ok_deflate *deflate, uint32_t value, uint8_t bits) {
    ok_assert(deflate->bit_buffer_length + bits <= 32);
    deflate->bit_buffer |= value << deflate->bit_buffer_length;
    deflate->bit_buffer_length += bits;
    return ok_deflate_bit_buffer_flush(deflate);
}

static bool ok_deflate_write_byte_align(ok_deflate *deflate) {
    uint8_t b = deflate->bit_buffer_length & 7;
    if (b == 0) {
        return ok_deflate_bit_buffer_flush(deflate);
    } else {
        return ok_deflate_write_bits(deflate, 0, 8 - b);
    }
}

static bool ok_deflate_write_stored_block(ok_deflate *deflate, const uint8_t *buffer, uint16_t buffer_length, bool is_final) {
    ok_assert(buffer_length <= 0xffff);
    const uint8_t block_length[4] = {
        (buffer_length & 0xff), (buffer_length >> 8),
        ((~buffer_length) & 0xff), ((~buffer_length) >> 8),
    };
    bool success = true;
    success &= ok_deflate_write_bits(deflate, is_final ? 1 : 0, 1); // final block flag
    success &= ok_deflate_write_bits(deflate, OK_DEFLATE_BLOCK_TYPE_NO_COMPRESSION, 2);
    success &= ok_deflate_write_byte_align(deflate);
    success &= deflate->params.write(deflate->params.write_context, block_length, sizeof(block_length));
    success &= deflate->params.write(deflate->params.write_context, buffer, buffer_length);
    return success;
}

bool ok_deflate_data(ok_deflate *deflate, const uint8_t *data, size_t length, bool is_final) {
    do {
        const uint8_t *current_buffer;
        uint16_t current_buffer_length;
        if (deflate->buffer_length == 0 && (length >= OK_DEFLATE_BUFFER_LENGTH || is_final)) {
            // Deflate data directly without copying to buffer
            current_buffer = data;
            current_buffer_length = (uint16_t)ok_min(length, OK_DEFLATE_BUFFER_LENGTH);
            data += current_buffer_length;
            length -= current_buffer_length;
        } else {
            // Copy to buffer
            if (length > 0) {
                size_t copy_length = ok_min(length, sizeof(deflate->buffer) - deflate->buffer_length);
                memcpy(deflate->buffer + deflate->buffer_length, data, copy_length);
                deflate->buffer_length += copy_length;
                data += copy_length;
                length -= copy_length;
            }
            current_buffer = deflate->buffer;
            current_buffer_length = deflate->buffer_length;
        }
        
        const bool is_final_write = length == 0 && is_final;
        if (current_buffer_length == OK_DEFLATE_BUFFER_LENGTH || is_final_write) {
            // Write zlib header (RFC 1950)
            if (!deflate->header_written && !deflate->params.nowrap) {
                const uint16_t zlib_compression_info = 7; // 4 bits
                const uint16_t zlib_compression_method = 8; // 4 bits
                const uint16_t zlib_flag_compression_level = 0; // 2 bits
                const uint16_t zlib_flag_dict = 0; // 1 bit
                const uint16_t zlib_raw_header = ((zlib_compression_info << 12) |
                                                  (zlib_compression_method << 8) |
                                                  (zlib_flag_compression_level << 6) |
                                                  (zlib_flag_dict << 5));
                const uint16_t zlib_flag_check = 31 - (zlib_raw_header % 31); // 5 bits (ensure zlib_header is divisible by 31)
                const uint16_t zlib_header = zlib_raw_header | zlib_flag_check;
                uint8_t zlib_header_buffer[2];
                ok_uint16_to_bytes(zlib_header, zlib_header_buffer);
                if (!deflate->params.write(deflate->params.write_context, zlib_header_buffer, sizeof(zlib_header_buffer))) {
                    return false;
                }
                deflate->header_written = true;
            }
            
            // Deflate
            if (!ok_deflate_write_stored_block(deflate, current_buffer, current_buffer_length, is_final_write)) {
                return false;
            }
            
            // Update adler
            if (!deflate->params.nowrap) {
                deflate->adler = ok_adler_update(deflate->adler, current_buffer, current_buffer_length);
            }
            deflate->buffer_length = 0;
            
            // Write footer (RFC 1950) and reset
            if (is_final_write) {
                if (!ok_deflate_write_byte_align(deflate)) {
                    return false;
                }
                if (!deflate->params.nowrap) {
                    uint8_t footer[4];
                    ok_uint32_to_bytes(deflate->adler, footer);
                    if (!deflate->params.write(deflate->params.write_context, footer, sizeof(footer))) {
                        return false;
                    }
                }
                
                // Reset
                deflate->header_written = false;
                deflate->adler = ok_adler_init();
            }
        }
    } while (length > 0);
    return true;
}

void ok_deflate_free(ok_deflate *deflate) {
    if (deflate) {
        deflate->params.free(deflate->params.allocator_context, deflate);
    }
}
