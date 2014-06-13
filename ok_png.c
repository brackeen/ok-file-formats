#include "ok_png.h"
#include "ok__internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>

// Set this to use the regular zlib library instead of the internal inflater
//#define USE_ZLIB

#ifdef USE_ZLIB
#include "zlib.h"
#endif

#if __STDC_VERSION__ >= 199901L
#define RESTRICT restrict
#else
#define RESTRICT 
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#define PNG_TYPE(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | d )

static const uint32_t CHUNK_IHDR = PNG_TYPE('I','H','D','R');
static const uint32_t CHUNK_PLTE = PNG_TYPE('P','L','T','E');
static const uint32_t CHUNK_TRNS = PNG_TYPE('t','R','N','S');
static const uint32_t CHUNK_IDAT = PNG_TYPE('I','D','A','T');
static const uint32_t CHUNK_IEND = PNG_TYPE('I','E','N','D');
static const uint32_t CHUNK_CGBI = PNG_TYPE('C','g','B','I');

static const uint8_t COLOR_TYPE_GRAYSCALE = 0;
static const uint8_t COLOR_TYPE_RGB = 2;
static const uint8_t COLOR_TYPE_PALETTE = 3;
static const uint8_t COLOR_TYPE_GRAYSCALE_WITH_ALPHA = 4;
static const uint8_t COLOR_TYPE_RGB_WITH_ALPHA = 6;
static const int SAMPLES_PER_PIXEL[] = { 1, 0, 3, 1, 2, 0, 4 };

typedef enum {
    FILTER_NONE = 0,
    FILTER_SUB,
    FILTER_UP,
    FILTER_AVG,
    FILTER_PAETH,
    NUM_FILTERS
} filter_type;

typedef struct {
    // Image
    ok_image *image;
    
    // Input
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;
    
    // Decode options
    ok_color_format color_format;
    bool flip_y;
    bool info_only;
    
    // Decoding
#ifdef USE_ZLIB
    z_stream zlib_stream;
    bool zlib_initialized;
#else
    struct ok_inflater *inflater;
    uint32_t inflater_bytes_read;
#endif
    uint8_t *inflate_buffer;
    uint8_t *curr_scanline;
    uint8_t *prev_scanline;
    uint32_t scanline;
    uint8_t interlace_pass; // 0 for uninitialized, 1 for non-interlaced, 1..7 for interlaced
    bool ready_for_next_interlace_pass;
    uint8_t *temp_data_row;
    bool decoding_completed;
    
    // PNG data
    uint8_t bit_depth;
    uint8_t color_type;
    uint8_t interlace_method;
    uint8_t palette[256 * 4];
    uint32_t palette_length;
    uint16_t single_transparent_color_key[3];
    bool has_single_transparent_color;
    bool is_ios_format;
    
} png_decoder;

static bool ok_read(png_decoder *decoder, uint8_t *data, const size_t length) {
    if (decoder->read_func(decoder->reader_data, data, length) == length) {
        return true;
    }
    else {
        ok_image_error(decoder->image, "Read error: error calling read function.");
        return false;
    }
}

static bool ok_seek(png_decoder *decoder, const int length) {
    if (decoder->seek_func(decoder->reader_data, length) == 0) {
        return true;
    }
    else {
        ok_image_error(decoder->image, "Read error: error calling seek function.");
        return false;
    }
}

static void decode_png(ok_image *image, void* reader_data,
                       ok_read_func read_func, ok_seek_func seek_func,
                       const ok_color_format color_format, const bool flip_y, const bool info_only);

static ok_image *read_png(const char *file_name, const ok_color_format color_format, const bool flip_y,
                          const bool info_only) {
    ok_image *image = calloc(1, sizeof(ok_image));
    if (file_name != NULL) {
        FILE *fp = fopen(file_name, "rb");
        if (fp != NULL) {
            decode_png(image, fp, ok_file_read_func, ok_file_seek_func, color_format, flip_y, info_only);
            fclose(fp);
        }
        else {
            ok_image_error(image, "%s", strerror(errno));
        }
    }
    else {
        ok_image_error(image, "Invalid argument: file_name is NULL");
    }
    return image;
}

static ok_image *read_png_from_file(FILE *fp, const ok_color_format color_format, const bool flip_y,
                                    const bool info_only) {
    ok_image *image = calloc(1, sizeof(ok_image));
    if (fp != NULL) {
        decode_png(image, fp, ok_file_read_func, ok_file_seek_func, color_format, flip_y, info_only);
    }
    else {
        ok_image_error(image, "Invalid argument: file is NULL");
    }
    return image;
}

static ok_image *read_png_from_memory(const void *buffer, const size_t buffer_length,
                                      const ok_color_format color_format, const bool flip_y,
                                      const bool info_only) {
    ok_image *image = calloc(1, sizeof(ok_image));
    if (buffer != NULL) {
        ok_memory_source memory;
        memory.buffer = (uint8_t *)buffer;
        memory.remaining_bytes = buffer_length;
        decode_png(image, &memory, ok_memory_read_func, ok_memory_seek_func, color_format, flip_y, info_only);
    }
    else {
        ok_image_error(image, "Invalid argument: buffer is NULL");
    }
    return image;
}

static ok_image *read_png_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                                         const ok_color_format color_format, const bool flip_y,
                                         const bool info_only) {
    ok_image *image = calloc(1, sizeof(ok_image));
    if (read_func != NULL && seek_func != NULL) {
        decode_png(image, user_data, read_func, seek_func, color_format, flip_y, info_only);
    }
    else {
        ok_image_error(image, "Invalid argument: read_func or seek_func is NULL");
    }
    return image;
}

// Public API

ok_image *ok_png_read_info(const char *file_name) {
    return read_png(file_name, OK_COLOR_FORMAT_RGBA, false, true);
}

ok_image *ok_png_read_info_from_file(FILE *file) {
    return read_png_from_file(file, OK_COLOR_FORMAT_RGBA, false, true);
}

ok_image *ok_png_read_info_from_memory(const void *buffer, const size_t buffer_length) {
    return read_png_from_memory(buffer, buffer_length, OK_COLOR_FORMAT_RGBA, false, true);
}

ok_image *ok_png_read_info_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func) {
    return read_png_from_callbacks(user_data, read_func, seek_func, OK_COLOR_FORMAT_RGBA, false, true);
}

ok_image *ok_png_read(const char *file_name, const ok_color_format color_format, const bool flip_y) {
    return read_png(file_name, color_format, flip_y, false);
}

ok_image *ok_png_read_from_file(FILE *file, const ok_color_format color_format, const bool flip_y) {
    return read_png_from_file(file, color_format, flip_y, false);
}

ok_image *ok_png_read_from_memory(const void *buffer, const size_t buffer_length,
                                  const ok_color_format color_format, const bool flip_y) {
    return read_png_from_memory(buffer, buffer_length, color_format, flip_y, false);
}

ok_image *ok_png_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                                     const ok_color_format color_format, const bool flip_y) {
    return read_png_from_callbacks(user_data, read_func, seek_func, color_format, flip_y, false);
}

// Main read functions

static inline void premultiply(uint8_t *dst) {
    const uint8_t a = dst[3];
    if (a == 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
    }
    else if (a < 255) {
        dst[0] = (a * dst[0] + 127) / 255;
        dst[1] = (a * dst[1] + 127) / 255;
        dst[2] = (a * dst[2] + 127) / 255;
    }
}

static inline void unpremultiply(uint8_t *dst) {
    const uint8_t a = dst[3];
    if (a > 0 && a < 255) {
        dst[0] = 255 * dst[0] / a;
        dst[1] = 255 * dst[1] / a;
        dst[2] = 255 * dst[2] / a;
    }
}

static bool read_header(png_decoder *decoder, const uint32_t chunk_length) {
    ok_image *image = decoder->image;
    if (chunk_length != 13) {
        ok_image_error(image, "Invalid IHDR chunk length: %u", chunk_length);
        return false;
    }
    uint8_t chunk_data[13];
    if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
        return false;
    }
    image->width = readBE32(chunk_data);
    image->height = readBE32(chunk_data + 4);
    decoder->bit_depth = chunk_data[8];
    decoder->color_type = chunk_data[9];
    uint8_t compression_method = chunk_data[10];
    uint8_t filter_method = chunk_data[11];
    decoder->interlace_method = chunk_data[12];
    
    if (compression_method != 0) {
        ok_image_error(image, "Invalid compression method: %i", (int)compression_method);
        return false;
    }
    else if (filter_method != 0) {
        ok_image_error(image, "Invalid filter method: %i", (int)filter_method);
        return false;
    }
    else if (decoder->interlace_method != 0 && decoder->interlace_method != 1) {
        ok_image_error(image, "Invalid interlace method: %i", (int)decoder->interlace_method);
        return false;
    }
    
    const int c = decoder->color_type;
    const int b = decoder->bit_depth;
    const bool valid =
    (c == COLOR_TYPE_GRAYSCALE && (b == 1 || b == 2 || b == 4 || b == 8 || b == 16)) ||
    (c == COLOR_TYPE_RGB && (b == 8 || b == 16)) ||
    (c == COLOR_TYPE_PALETTE && (b == 1 || b == 2 || b == 4 || b == 8)) ||
    (c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA && (b == 8 || b == 16)) ||
    (c == COLOR_TYPE_RGB_WITH_ALPHA && (b == 8 || b == 16));
    
    if (!valid) {
        ok_image_error(image, "Invalid combination of color type (%i) and bit depth (%i)", c, b);
        return false;
    }
    
    image->has_alpha = c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA || c == COLOR_TYPE_RGB_WITH_ALPHA;
    decoder->interlace_pass = 0;
    decoder->ready_for_next_interlace_pass = true;
    return true;
}

static bool read_palette(png_decoder *decoder, const uint32_t chunk_length) {
    ok_image *image = decoder->image;
    decoder->palette_length = chunk_length / 3;
    
    if (decoder->palette_length > 256 || decoder->palette_length * 3 != chunk_length) {
        ok_image_error(image, "Invalid palette chunk length: %u", chunk_length);
        return false;
    }
    const bool src_is_bgr = decoder->is_ios_format;
    const bool dst_is_bgr = decoder->color_format == OK_COLOR_FORMAT_BGRA ||
    decoder->color_format == OK_COLOR_FORMAT_BGRA_PRE;
    const bool should_byteswap = src_is_bgr != dst_is_bgr;
    uint8_t *dst = decoder->palette;
    uint8_t buffer[3];
    for (uint32_t i = 0; i < decoder->palette_length; i++) {
        if (!ok_read(decoder, buffer, 3)) {
            return false;
        }
        if (should_byteswap) {
            *dst++ = buffer[2];
            *dst++ = buffer[1];
            *dst++ = buffer[0];
        }
        else {
            *dst++ = buffer[0];
            *dst++ = buffer[1];
            *dst++ = buffer[2];
        }
        *dst++ = 0xff;
    }
    return true;
}

static bool read_transparency(png_decoder *decoder, const uint32_t chunk_length) {
    ok_image *image = decoder->image;
    image->has_alpha = true;
    
    if (decoder->color_type == COLOR_TYPE_PALETTE) {
        if (chunk_length > decoder->palette_length) {
            ok_image_error(image, "Invalid transparency length for palette color type: %u", chunk_length);
            return false;
        }
        
        const bool should_premultiply = decoder->color_format == OK_COLOR_FORMAT_BGRA_PRE ||
        decoder->color_format == OK_COLOR_FORMAT_RGBA_PRE;
        uint8_t *dst = decoder->palette;
        for (uint32_t i = 0; i < chunk_length; i++) {
            if (!ok_read(decoder, (dst + 3), 1)) {
                return false;
            }
            if (should_premultiply) {
                premultiply(dst);
            }
            dst += 4;
        }
        return true;
    }
    else if (decoder->color_type == COLOR_TYPE_GRAYSCALE) {
        if (chunk_length != 2) {
            ok_image_error(image, "Invalid transparency length for grayscale color type: %u", chunk_length);
            return false;
        }
        else {
            uint8_t buffer[2];
            if (!ok_read(decoder, buffer, sizeof(buffer))) {
                return false;
            }
            const uint16_t v = readBE16(buffer);
            decoder->single_transparent_color_key[0] = v;
            decoder->single_transparent_color_key[1] = v;
            decoder->single_transparent_color_key[2] = v;
            decoder->has_single_transparent_color = true;
            return true;
        }
    }
    else if (decoder->color_type == COLOR_TYPE_RGB) {
        if (chunk_length != 6) {
            ok_image_error(image, "Invalid transparency length for truecolor color type: %u", chunk_length);
            return false;
        }
        else {
            uint8_t buffer[6];
            if (!ok_read(decoder, buffer, sizeof(buffer))) {
                return false;
            }
            decoder->single_transparent_color_key[0] = readBE16(buffer + 0);
            decoder->single_transparent_color_key[1] = readBE16(buffer + 2);
            decoder->single_transparent_color_key[2] = readBE16(buffer + 4);
            decoder->has_single_transparent_color = true;
            return true;
        }
    }
    else {
        ok_image_error(image, "Invalid transparency for color type %i", (int)decoder->color_type);
        return false;
    }
}

static inline int paeth_predictor(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    else if (pb <= pc) {
        return b;
    }
    else {
        return c;
    }
}

static void decode_filter(uint8_t * RESTRICT curr, const uint8_t * RESTRICT prev,
                          const uint32_t length, const int filter, const int bpp) {
    switch (filter) {
        case FILTER_NONE:
            // Do nothing
            break;
        case FILTER_SUB: {
            // Input = Sub
            // Raw(x) = Sub(x) + Raw(x-bpp)
            // For all x < 0, assume Raw(x) = 0.
            for (uint32_t i = bpp; i < length; i++) {
                curr[i] = curr[i] + curr[i - bpp];
            }
            break;
        }
        case FILTER_UP: {
            // Input = Up
            // Raw(x) = Up(x) + Prior(x)
            for (uint32_t i = 0; i < length; i++) {
                curr[i] = curr[i] + prev[i];
            }
            break;
        }
        case FILTER_AVG: {
            // Input = Average
            // Raw(x) = Average(x) + floor((Raw(x-bpp)+Prior(x))/2)
            for (int i = 0; i < bpp; i++) {
                curr[i] = curr[i] + (prev[i] >> 1);
            }
            for (uint32_t j = bpp; j < length; j++) {
                curr[j] = curr[j] + ((curr[j - bpp] + prev[j]) >> 1);
            }
            break;
        }
        case FILTER_PAETH: {
            // Input = Paeth
            // Raw(x) = Paeth(x) + PaethPredictor(Raw(x-bpp), Prior(x), Prior(x-bpp))
            for (int i = 0; i < bpp; i++) {
                curr[i] = curr[i] + prev[i];
            }
            for (uint32_t j = bpp; j < length; j++) {
                curr[j] = curr[j] + paeth_predictor(curr[j - bpp], prev[j], prev[j - bpp]);
            }
            break;
        }
    }
}

static bool transform_scanline(png_decoder *decoder, const uint8_t *src, const uint32_t width) {
    ok_image *image = decoder->image;
    const uint32_t dst_stride = image->width * 4;
    uint8_t * dst_start;
    uint8_t * dst_end;
    if (decoder->interlace_method == 0) {
        const uint32_t dst_y = decoder->flip_y ? (image->height - decoder->scanline - 1) : decoder->scanline;
        dst_start = image->data + (dst_y * dst_stride);
    }
    else if (decoder->interlace_pass == 7) {
        const uint32_t t_scanline = decoder->scanline * 2 + 1;
        const uint32_t dst_y = decoder->flip_y ? (image->height - t_scanline - 1) : t_scanline;
        dst_start = image->data + (dst_y * dst_stride);
    }
    else {
        dst_start = decoder->temp_data_row;
    }
    dst_end = dst_start + width * 4;
    
    const int c = decoder->color_type;
    const int d = decoder->bit_depth;
    const bool t = decoder->has_single_transparent_color;
    const bool has_full_alpha = c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA || c == COLOR_TYPE_RGB_WITH_ALPHA;
    const bool src_is_premultiplied = decoder->is_ios_format;
    const bool dst_is_premultiplied = decoder->color_format == OK_COLOR_FORMAT_RGBA_PRE ||
    decoder->color_format == OK_COLOR_FORMAT_BGRA_PRE;
    const bool src_is_bgr = decoder->is_ios_format;
    const bool dst_is_bgr = decoder->color_format == OK_COLOR_FORMAT_BGRA ||
    decoder->color_format == OK_COLOR_FORMAT_BGRA_PRE;
    bool should_byteswap = (c == COLOR_TYPE_RGB || c == COLOR_TYPE_RGB_WITH_ALPHA) && src_is_bgr != dst_is_bgr;
    
    // Simple transforms
    if (c == COLOR_TYPE_GRAYSCALE && d == 8 && !t) {
        uint8_t *dst = dst_start;
        while (dst < dst_end) {
            uint8_t v = *src++;
            *dst++ = v;
            *dst++ = v;
            *dst++ = v;
            *dst++ = 0xff;
        }
    }
    else if (c == COLOR_TYPE_PALETTE && d == 8) {
        uint8_t *dst = dst_start;
        const uint8_t *palette = decoder->palette;
        while (dst < dst_end) {
            const uint8_t *psrc = palette + (*src++ * 4);
            *dst++ = *psrc++;
            *dst++ = *psrc++;
            *dst++ = *psrc++;
            *dst++ = *psrc++;
        }
    }
    else if (c == COLOR_TYPE_RGB && d == 8 && !t) {
        if (should_byteswap) {
            uint8_t *dst = dst_start;
            while (dst < dst_end) {
                *dst++ = src[2];
                *dst++ = src[1];
                *dst++ = src[0];
                *dst++ = 0xff;
                src += 3;
            }
            should_byteswap = false;
        }
        else {
            uint8_t *dst = dst_start;
            while (dst < dst_end) {
                *dst++ = *src++;
                *dst++ = *src++;
                *dst++ = *src++;
                *dst++ = 0xff;
            }
        }
    }
    else if (c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA && d == 8) {
        uint8_t *dst = dst_start;
        while (dst < dst_end) {
            uint8_t v = *src++;
            uint8_t a = *src++;
            *dst++ = v;
            *dst++ = v;
            *dst++ = v;
            *dst++ = a;
        }
    }
    else if (c == COLOR_TYPE_RGB_WITH_ALPHA && d == 8) {
        memcpy(dst_start, src, width * 4);
    }
    else {
        // Complex transforms: 1-, 2-, 4- and 16-bit, and 8-bit with single-color transparency.
        const uint8_t *palette = decoder->palette;
        const int bitmask = (1 << d) - 1;
        int bit = 8 - d;
        uint16_t tr = decoder->single_transparent_color_key[0];
        uint16_t tg = decoder->single_transparent_color_key[1];
        uint16_t tb = decoder->single_transparent_color_key[2];
        if (d <= 8) {
            tr = (tr & bitmask) * (255 / bitmask);
            tg = (tg & bitmask) * (255 / bitmask);
            tb = (tb & bitmask) * (255 / bitmask);
        }
        uint8_t *dst = dst_start;
        while (dst < dst_end) {
            uint16_t r = 0;
            uint16_t g = 0;
            uint16_t b = 0;
            uint16_t a = 0xffff;
            
            if (d < 8) {
                if (bit < 0) {
                    bit = 8 - d;
                    src++;
                }
                int v = (*src >> bit) & bitmask;
                if (c == COLOR_TYPE_GRAYSCALE) {
                    r = g = b = v * (255 / bitmask);
                }
                else {
                    const uint8_t *psrc = palette + (v * 4);
                    r = *psrc++;
                    g = *psrc++;
                    b = *psrc++;
                    a = *psrc++;
                }
                bit -= d;
            }
            else if (d == 8) {
                if (c == COLOR_TYPE_GRAYSCALE) {
                    r = g = b = *src++;
                }
                else if (c == COLOR_TYPE_PALETTE) {
                    const uint8_t *psrc = palette + (*src++ * 4);
                    r = *psrc++;
                    g = *psrc++;
                    b = *psrc++;
                    a = *psrc++;
                }
                else if (c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA) {
                    r = g = b = *src++;
                    a = *src++;
                }
                else if (c == COLOR_TYPE_RGB) {
                    r = *src++;
                    g = *src++;
                    b = *src++;
                }
                else if (c == COLOR_TYPE_RGB_WITH_ALPHA) {
                    r = *src++;
                    g = *src++;
                    b = *src++;
                    a = *src++;
                }
            }
            else if (d == 16) {
                if (c == COLOR_TYPE_GRAYSCALE) {
                    r = g = b = readBE16(src);
                    src += 2;
                }
                else if (c == COLOR_TYPE_GRAYSCALE_WITH_ALPHA) {
                    r = g = b = readBE16(src);
                    a = readBE16(src + 2);
                    src += 4;
                }
                else if (c == COLOR_TYPE_RGB) {
                    r = readBE16(src);
                    g = readBE16(src + 2);
                    b = readBE16(src + 4);
                    src += 6;
                }
                else if (c == COLOR_TYPE_RGB_WITH_ALPHA) {
                    r = readBE16(src);
                    g = readBE16(src + 2);
                    b = readBE16(src + 4);
                    a = readBE16(src + 6);
                    src += 8;
                }
            }
            
            if (t && r == tr && g == tg && b == tb) {
                a = 0;
                if (dst_is_premultiplied) {
                    r = b = g = 0;
                }
            }
            
            if (d == 16) {
                // This is libpng's formula for scaling 16-bit to 8-bit
                r = (r * 255 + 32895) >> 16;
                g = (g * 255 + 32895) >> 16;
                b = (b * 255 + 32895) >> 16;
                a = (a * 255 + 32895) >> 16;
            }
            
            if (should_byteswap) {
                *dst++ = b;
                *dst++ = g;
                *dst++ = r;
                *dst++ = a;
            }
            else {
                *dst++ = r;
                *dst++ = g;
                *dst++ = b;
                *dst++ = a;
            }
        }
        should_byteswap = false;
    }

    // Color format convert: Premultiply, un-premultiply, and swap if needed
    if (should_byteswap) {
        if (has_full_alpha && src_is_premultiplied && !dst_is_premultiplied) {
            // Convert from BGRA_PRE to RGBA
            for (uint8_t *dst = dst_start; dst < dst_end; dst += 4) {
                const uint8_t v = dst[0];
                dst[0] = dst[2];
                dst[2] = v;
                unpremultiply(dst);
            }
        }
        else if (has_full_alpha && !src_is_premultiplied && dst_is_premultiplied) {
            // Convert from BGRA to RGBA_PRE
            for (uint8_t *dst = dst_start; dst < dst_end; dst += 4) {
                const uint8_t v = dst[0];
                dst[0] = dst[2];
                dst[2] = v;
                premultiply(dst);
            }
        }
        else {
            // Convert from BGRA to RGBA (or BGRA_PRE to RGBA_PRE)
            for (uint8_t *dst = dst_start; dst < dst_end; dst += 4) {
                const uint8_t v = dst[0];
                dst[0] = dst[2];
                dst[2] = v;
            }
        }
    }
    else if (has_full_alpha) {
        if (src_is_premultiplied && !dst_is_premultiplied) {
            // Convert from RGBA_PRE to RGBA
            for (uint8_t *dst = dst_start; dst < dst_end; dst += 4) {
                unpremultiply(dst);
            }
        }
        else if (!src_is_premultiplied && dst_is_premultiplied) {
            // Convert from RGBA to RGBA_PRE
            for (uint8_t *dst = dst_start; dst < dst_end; dst += 4) {
                premultiply(dst);
            }
        }
        else {
            // Do nothing: Already in correct format, RGBA or RGBA_PRE
        }
    }
    
    // If interlaced, copy from the temp buffer
    if (decoder->interlace_method == 1 && decoder->interlace_pass < 7) {
        const int i = decoder->interlace_pass;
        const uint32_t s = decoder->scanline;
        //                                       1      2      3      4      5      6      7
        static const uint32_t dst_x[]  = {0,     0,     4,     0,     2,     0,     1,     0 };
        static const uint32_t dst_dx[] = {0,     8,     8,     4,     4,     2,     2,     1 };
               const uint32_t dst_y[]  = {0,   s*8,   s*8, 4+s*8,   s*4, 2+s*4,   s*2, 1+s*2 };
        
        uint32_t x = dst_x[i];
        uint32_t y = dst_y[i];
        uint32_t dx = 4 * (dst_dx[i] - 1);
        if (decoder->flip_y) {
            y = (image->height - y - 1);
        }
        
        src = dst_start;
        uint8_t *src_end = dst_end;
        uint8_t *dst = image->data + (y * dst_stride) + (x * 4);
        while (src < src_end) {
            *dst++ = *src++;
            *dst++ = *src++;
            *dst++ = *src++;
            *dst++ = *src++;
            dst += dx;
        }
    }
    
    return true;
}

static uint32_t get_width_for_pass(const png_decoder *decoder) {
    const uint32_t w = decoder->image->width;
    if (decoder->interlace_method == 0) {
        return w;
    }
    
    switch (decoder->interlace_pass) {
        case 1:
            return (w + 7) / 8;
        case 2:
            return (w + 3) / 8;
        case 3:
            return (w + 3) / 4;
        case 4:
            return (w + 1) / 4;
        case 5:
            return (w + 1) / 2;
        case 6:
            return w / 2;
        case 7:
            return w;
        default:
            return 0;
    }
}

static uint32_t get_height_for_pass(const png_decoder *decoder) {
    const uint32_t h = decoder->image->height;
    if (decoder->interlace_method == 0) {
        return h;
    }
    
    switch (decoder->interlace_pass) {
        case 1:
            return (h + 7) / 8;
        case 2:
            return (h + 7) / 8;
        case 3:
            return (h + 3) / 8;
        case 4:
            return (h + 3) / 4;
        case 5:
            return (h + 1) / 4;
        case 6:
            return (h + 1) / 2;
        case 7:
            return h / 2;
        default:
            return 0;
    }
}

static bool read_data(png_decoder *decoder, uint32_t bytes_remaining) {
    ok_image *image = decoder->image;
    const int inflate_buffer_size = 64 * 1024;
    const int num_passes = decoder->interlace_method == 0 ? 1 : 7;
    const int bits_per_pixel = decoder->bit_depth * SAMPLES_PER_PIXEL[decoder->color_type];
    const int bytes_per_pixel = (bits_per_pixel + 7) / 8;
    const int max_bytes_per_scanline = 1 + (image->width * bits_per_pixel + 7) / 8;
    
    // Create buffers
    if (image->data == NULL) {
        uint64_t size = (uint64_t)image->width * image->height * 4;
        size_t platform_size = (size_t)size;
        if (platform_size == size) {
            image->data = malloc(platform_size);
        }
        if (image->data == NULL) {
            ok_image_error(image, "Couldn't allocate memory for %u x %u image",
                        image->width, image->height);
            return false;
        }
    }
    if (decoder->prev_scanline == NULL) {
        decoder->prev_scanline = malloc(max_bytes_per_scanline);
    }
    if (decoder->curr_scanline == NULL) {
        decoder->curr_scanline = malloc(max_bytes_per_scanline);
    }
    if (decoder->inflate_buffer == NULL) {
        decoder->inflate_buffer = malloc(inflate_buffer_size);
    }
    if (decoder->interlace_method == 1 && decoder->temp_data_row == NULL) {
        decoder->temp_data_row = malloc(image->width * 4);
    }
    if (decoder->curr_scanline == NULL || decoder->prev_scanline == NULL || decoder->inflate_buffer == NULL ||
        (decoder->interlace_method == 1 && decoder->temp_data_row == NULL)) {
        ok_image_error(image, "Couldn't allocate buffers");
        return false;
    }
    
    // Setup inflater
#ifdef USE_ZLIB
    if (!decoder->zlib_initialized) {
        if (inflateInit2(&decoder->zlib_stream, decoder->is_ios_format ? -15 : 15) != Z_OK) {
            ok_image_error(image, "Couldn't init zlib");
            return false;
        }
        decoder->zlib_initialized = true;
    }
#else
    if (decoder->inflater == NULL) {
        decoder->inflater = ok_inflater_init(decoder->is_ios_format);
        if (decoder->inflater == NULL) {
            ok_image_error(image, "Couldn't init inflater");
            return false;
        }
    }
#endif
    
    // Sanity check - this happened with one file in the PNG suite
    if (decoder->decoding_completed) {
        if (bytes_remaining > 0) {
            return ok_seek(decoder, bytes_remaining);
        }
        else {
            return true;
        }
    }

    // Read data
    uint32_t curr_width = get_width_for_pass(decoder);
    uint32_t curr_height = get_height_for_pass(decoder);
    uint32_t curr_bytes_per_scanline = 1 + (curr_width * bits_per_pixel + 7) / 8;
    while (true) {
        // Setup pass
        while (decoder->ready_for_next_interlace_pass) {
            decoder->ready_for_next_interlace_pass = false;
            decoder->scanline = 0;
            decoder->interlace_pass++;
            if (decoder->interlace_pass == num_passes + 1) {
                // Done decoding - skip any remaining chunk data
                decoder->decoding_completed = true;
                if (bytes_remaining > 0) {
                    return ok_seek(decoder, bytes_remaining);
                }
                else {
                    return true;
                }
            }
            curr_width = get_width_for_pass(decoder);
            curr_height = get_height_for_pass(decoder);
            curr_bytes_per_scanline = 1 + (curr_width * bits_per_pixel + 7) / 8;
            if (curr_width == 0 || curr_height == 0) {
                // No data for this pass - happens if width or height <= 4
                decoder->ready_for_next_interlace_pass = true;
            }
            else {
                memset(decoder->curr_scanline, 0, curr_bytes_per_scanline);
                memset(decoder->prev_scanline, 0, curr_bytes_per_scanline);
#ifdef USE_ZLIB
                decoder->zlib_stream.next_out = decoder->curr_scanline;
                decoder->zlib_stream.avail_out = curr_bytes_per_scanline;
#else 
                decoder->inflater_bytes_read = 0;
#endif
            }
        }
        
        // Read compressed data
#ifdef USE_ZLIB
        if (decoder->zlib_stream.avail_in == 0)
#else
        if (ok_inflater_needs_input(decoder->inflater))
#endif
        {
            if (bytes_remaining == 0) {
                // Need more data, but there is no remaining data in this chunk.
                // There may be another IDAT chunk.
                return true;
            }
            const uint32_t len = min(inflate_buffer_size, bytes_remaining);
            if (!ok_read(decoder, decoder->inflate_buffer, len)) {
                return false;
            }
            bytes_remaining -= len;
#ifdef USE_ZLIB
            decoder->zlib_stream.next_in = decoder->inflate_buffer;
            decoder->zlib_stream.avail_in = len;
#else
            ok_inflater_set_input(decoder->inflater, decoder->inflate_buffer, len);
#endif
        }
        
        // Decompress data
#ifdef USE_ZLIB
        int status = inflate(&decoder->zlib_stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            ok_image_error(image, "Error inflating data");
            return false;
        }
        
        // Get one scanline
        if (decoder->zlib_stream.avail_out == 0)
#else 
        intptr_t len = ok_inflater_inflate(decoder->inflater,
                                           decoder->curr_scanline + decoder->inflater_bytes_read,
                                           curr_bytes_per_scanline - decoder->inflater_bytes_read);
        if (len < 0) {
            ok_image_error(image, ok_inflater_error_message(decoder->inflater));
            return false;
        }
        decoder->inflater_bytes_read += len;
        if (decoder->inflater_bytes_read == curr_bytes_per_scanline)
#endif
        {
            // Apply filter
            const int filter = decoder->curr_scanline[0];
            if (filter > 0 && filter < NUM_FILTERS) {
                decode_filter(decoder->curr_scanline + 1, decoder->prev_scanline + 1, curr_bytes_per_scanline - 1,
                              filter, bytes_per_pixel);
            }
            else if (filter != 0) {
                ok_image_error(image, "Invalid filter type: %i", filter);
                return false;
            }

            // Transform
            if (!transform_scanline(decoder, decoder->curr_scanline + 1, curr_width)) {
                return false;
            }
            
            // Setup for next scanline or pass
            decoder->scanline++;
            if (decoder->scanline == curr_height) {
                decoder->ready_for_next_interlace_pass = true;
            }
            else {
                uint8_t* temp = decoder->curr_scanline;
                decoder->curr_scanline = decoder->prev_scanline;
                decoder->prev_scanline = temp;
#ifdef USE_ZLIB
                decoder->zlib_stream.next_out = decoder->curr_scanline;
                decoder->zlib_stream.avail_out = curr_bytes_per_scanline;
#else
                decoder->inflater_bytes_read = 0;
#endif
            }
        }
    }
}

static void decode_png2(png_decoder *decoder) {
    ok_image *image = decoder->image;
    
    uint8_t png_header[8];
    if (!ok_read(decoder, png_header, sizeof(png_header))) {
        return;
    }
    uint8_t png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (memcmp(png_header, png_signature, 8) != 0) {
        ok_image_error(decoder->image, "Invalid signature (not a PNG file)");
        return;
    }
    
    // When info_only is true, we only care about the IHDR chunk and whether or not the tRNS chunk exists.
    bool end_found = false;
    while (!end_found) {
        uint8_t chunk_header[8];
        uint8_t chunk_footer[4];
        if (!ok_read(decoder, chunk_header, sizeof(chunk_header))) {
            return;
        }
        const uint32_t chunk_length = readBE32(chunk_header);
        const uint32_t chunk_type = readBE32(chunk_header + 4);
        bool success = false;
        if (chunk_type == CHUNK_IHDR) {
            success = read_header(decoder, chunk_length);
            if (success && decoder->info_only) {
                // If the image has alpha, then we have all the info we need.
                // Otherwise, continue scanning to see if the tRNS chunk exists.
                if (image->has_alpha) {
                    return;
                }
            }
        }
        else if (chunk_type == CHUNK_CGBI) {
            success = ok_seek(decoder, chunk_length);
            decoder->is_ios_format = true;
        }
        else if (chunk_type == CHUNK_PLTE && !decoder->info_only) {
            success = read_palette(decoder, chunk_length);
        }
        else if (chunk_type == CHUNK_TRNS) {
            if (decoder->info_only) {
                // No need to parse this chunk, we have all the info we need.
                image->has_alpha = true;
                return;
            }
            else {
                success = read_transparency(decoder, chunk_length);
            }
        }
        else if (chunk_type == CHUNK_IDAT) {
            if (decoder->info_only) {
                // Both IHDR and tRNS must come before IDAT, so we have all the info we need.
                return;
            }
            success = read_data(decoder, chunk_length);
        }
        else if (chunk_type == CHUNK_IEND) {
            success = ok_seek(decoder, chunk_length);
            end_found = true;
        }
        else {
            // Ignore this chunk
            success = ok_seek(decoder, chunk_length);
        }
        
        if (!success) {
            return;
        }
        
        // Read the footer (CRC) and ignore it
        if (!ok_read(decoder, chunk_footer, sizeof(chunk_footer))) {
            return;
        }
    }
    
    // Sanity check
    if (!decoder->decoding_completed) {
        ok_image_error(image, "Missing imaga data");
    }
}

static void decode_png(ok_image *image, void* reader_data,
                       ok_read_func read_func, ok_seek_func seek_func,
                       const ok_color_format color_format, const bool flip_y, const bool info_only) {
    if (image == NULL) {
        return;
    }
    png_decoder *decoder = calloc(1, sizeof(png_decoder));
    if (decoder == NULL) {
        ok_image_error(image, "Couldn't allocate decoder.");
        return;
    }

    decoder->image = image;
    decoder->reader_data = reader_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;
    decoder->color_format = color_format;
    decoder->flip_y = flip_y;
    decoder->info_only = info_only;
    
    decode_png2(decoder);
    
    // Cleanup decoder
#ifdef USE_ZLIB
    if (decoder->zlib_initialized) {
        inflateEnd(&decoder->zlib_stream);
    }
#else
    ok_inflater_free(decoder->inflater);
#endif
    if (decoder->curr_scanline != NULL) {
        free(decoder->curr_scanline);
    }
    if (decoder->prev_scanline != NULL) {
        free(decoder->prev_scanline);
    }
    if (decoder->inflate_buffer != NULL) {
        free(decoder->inflate_buffer);
    }
    if (decoder->temp_data_row != NULL) {
        free(decoder->temp_data_row);
    }
    free(decoder);
}
        
//
// Inflater
// Written from RFC 1950 and RFC 1951.
// Some of the comments are copy-and-paste from the RFCs.
//

#define BUFFER_SIZE_BITS 16
#if BUFFER_SIZE_BITS != 16
#error The circular buffer pointers, buffer_start_pos, buffer_end_pos, and buffer_offset, only work with 16-bit buffers.
#endif

// 32k for back buffer, 32k for forward buffer
#define BUFFER_SIZE (1 << BUFFER_SIZE_BITS)

#define BLOCK_TYPE_NO_COMPRESSION 0
#define BLOCK_TYPE_FIXED_HUFFMAN 1
#define BLOCK_TYPE_DYNAMIC_HUFFMAN 2
        
typedef enum {
    STATE_READY_FOR_HEAD = 0,
    STATE_READY_FOR_NEXT_BLOCK,
    STATE_READING_STORED_BLOCK_HEADER,
    STATE_READING_STORED_BLOCK,
    STATE_READING_DYNAMIC_BLOCK_HEADER,
    STATE_READING_DYNAMIC_CODE_LENGTHS,
    STATE_READING_DYNAMIC_LITERAL_TREE,
    STATE_READING_DYNAMIC_DISTANCE_TREE,
    STATE_READING_DYNAMIC_COMPRESSED_BLOCK,
    STATE_READING_FIXED_COMPRESSED_BLOCK,
    STATE_READING_DYNAMIC_DISTANCE,
    STATE_READING_FIXED_DISTANCE,
    STATE_DONE,
    STATE_ERROR,
    NUM_STATES,
} inflater_state;

#define VALUE_BITS 9
#define VALUE_BIT_MASK ((1 << VALUE_BITS) - 1)
#define MAX_NUM_CODES 289
#define MAX_CODE_LENGTH 16

static const int DISTANCE_TABLE[] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const int DISTANCE_TABLE_LENGTH = 30;

static const int LENGTH_TABLE[] = {
    3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
    15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258
};

static const int BIT_LENGTH_TABLE[] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};
static const int BIT_LENGTH_TABLE_LENGTH = 19;

typedef struct {
    uint16_t lookup_table[1 << (MAX_CODE_LENGTH - 1)];
    int bits;
    int bit_mask;
} huffman_tree;

struct ok_inflater {
    // Options
    bool nowrap;
    
    // Input
    uint8_t *input;
    uint8_t *input_end;
    uint32_t input_buffer;
    int input_buffer_bits;
    
    // Inflate data
    uint8_t *buffer;
    uint16_t buffer_start_pos;
    uint16_t buffer_end_pos;
    bool final_block;
    inflater_state state;
    int state_count;
    int state_literal;
    int state_distance;
    
    // For dynamic blocks
    int num_literal_codes;
    int num_distance_codes;
    int num_code_length_codes;
    uint8_t tree_codes[MAX_NUM_CODES];
    huffman_tree *code_length_huffman;
    int huffman_code;
    
    // Huffman
    huffman_tree *literal_huffman;
    huffman_tree *distance_huffman;
    huffman_tree *fixed_literal_huffman;
    huffman_tree *fixed_distance_huffman;
    
    // Error
    char error_message[80];
    
};

static void inflater_error(ok_inflater *inflater, const char *format, ... ) {
    if (inflater != NULL) {
        inflater->state = STATE_ERROR;
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(inflater->error_message, sizeof(inflater->error_message), format, args);
            va_end(args);
        }
    }
}

//
// Buffer writing / flushing
//

// is_buffer_full is commented out because it is not used, but it helps to understand how the buffer works.
//inline static bool is_buffer_full(const ok_inflater *inflater) {
//    return (uint16_t)(inflater->buffer_end_pos + 1) == inflater->buffer_start_pos;
//}

// Number of bytes that can be written until full or buffer wrap
inline static uint16_t can_write(const ok_inflater *inflater) {
    if (inflater->buffer_start_pos == 0) {
        return -inflater->buffer_end_pos - 1;
    }
    else if (inflater->buffer_start_pos > inflater->buffer_end_pos) {
        return inflater->buffer_start_pos - inflater->buffer_end_pos - 1;
    }
    else {
        return -inflater->buffer_end_pos;
    }
}

inline static uint16_t can_write_total(const ok_inflater *inflater) {
    return inflater->buffer_start_pos - inflater->buffer_end_pos - 1;
}

inline static void write_byte(ok_inflater *inflater, const uint8_t b) {
    inflater->buffer[inflater->buffer_end_pos++] = b;
}

inline static size_t write_bytes(ok_inflater *inflater, const uint8_t* src, const size_t len) {
    size_t bytes_remaining = len;
    while (bytes_remaining > 0) {
        size_t n = min(bytes_remaining, can_write(inflater));
        if (n == 0) {
            return len - bytes_remaining;
        }
        memcpy(inflater->buffer + inflater->buffer_end_pos, src, n);
        inflater->buffer_end_pos += n;
        bytes_remaining -= n;
        src += n;
    }
    return len;
}

inline static size_t write_byte_n(ok_inflater *inflater, const uint8_t b, const size_t len) {
    size_t bytes_remaining = len;
    while (bytes_remaining > 0) {
        size_t n = min(bytes_remaining, can_write(inflater));
        if (n == 0) {
            return len - bytes_remaining;
        }
        memset(inflater->buffer + inflater->buffer_end_pos, b, n);
        inflater->buffer_end_pos += n;
        bytes_remaining -= n;
    }
    return len;
}

inline static uint16_t can_flush_total(const ok_inflater *inflater) {
    return inflater->buffer_end_pos - inflater->buffer_start_pos;
}

// Number of bytes that can be flushed until empty of buffer wrap
inline static uint16_t can_flush(const ok_inflater *inflater) {
    if (inflater->buffer_start_pos <= inflater->buffer_end_pos) {
        return inflater->buffer_end_pos - inflater->buffer_start_pos;
    }
    else {
        return -inflater->buffer_start_pos;
    }
}

inline static size_t flush(ok_inflater *inflater, uint8_t *dst, const size_t len) {
    size_t bytes_remaining = len;
    while (bytes_remaining > 0) {
        size_t n = min(bytes_remaining, can_flush(inflater));
        if (n == 0) {
            return len - bytes_remaining;
        }
        memcpy(dst, inflater->buffer + inflater->buffer_start_pos, n);
        inflater->buffer_start_pos += n;
        bytes_remaining -= n;
        dst += n;
    }
    return len;
}

//
// Read from input
//

inline static void skip_byte_align(ok_inflater *inflater) {
    int skip_bits = inflater->input_buffer_bits & 7;
    inflater->input_buffer >>= skip_bits;
    inflater->input_buffer_bits -= skip_bits;
}

inline static bool load_bits(ok_inflater *inflater, const int num_bits) {
    while (inflater->input_buffer_bits < num_bits) {
        if (inflater->input == inflater->input_end) {
            return false;
        }
        inflater->input_buffer |= (*inflater->input++ << inflater->input_buffer_bits);
        inflater->input_buffer_bits += 8;
    }
    return true;
}

// Assumes at least num_bits bits are loaded into buffer (call load_bits first)
inline static uint32_t read_bits(ok_inflater *inflater, const int num_bits) {
    uint32_t ans = inflater->input_buffer & ((1 << num_bits) - 1);
    inflater->input_buffer >>= num_bits;
    inflater->input_buffer_bits -= num_bits;
    return ans;
}

// Assumes at least num_bits bits are loaded into buffer (call load_bits first)
inline static uint32_t peek_bits(ok_inflater *inflater, const int num_bits) {
    return inflater->input_buffer & ((1 << num_bits) - 1);
}

// Huffman

inline static uint32_t reverse_bits(uint32_t value, int num_bits) {
    uint32_t rev_value = value & 1;
    for (int i = num_bits - 1; i > 0; i--) {
        value >>= 1;
        rev_value <<= 1;
        rev_value |= value & 1;
    }
    return rev_value;
}

static int decode_literal(ok_inflater *inflater, const uint16_t *tree_lookup_table, const int tree_bits) {
    if (!load_bits(inflater, tree_bits)) {
        return -1;
    }
    int p = peek_bits(inflater, tree_bits);
    int value = tree_lookup_table[p];
    read_bits(inflater, value >> VALUE_BITS);
    return value & VALUE_BIT_MASK;
}

static bool make_huffman_tree_from_array(huffman_tree *tree, const uint8_t* code_length, const int length) {
    tree->bits = 1;
    
    // Count the number of codes for each code length.
    // Let code_length_count[n] be the number of codes of length n, n >= 1.
    int code_length_count[MAX_CODE_LENGTH];
    int i;
    for (i = 0; i < MAX_CODE_LENGTH; i++) {
        code_length_count[i] = 0;
    }
    for (i = 0; i < length; i++) {
        code_length_count[code_length[i]]++;
    }
    
    // Find the numerical value of the smallest code for each code length:
    int next_code[MAX_CODE_LENGTH];
    int code = 0;
    for (i = 1; i < MAX_CODE_LENGTH; i++) {
        code = (code + code_length_count[i-1]) << 1;
        next_code[i] = code;
        if (code_length_count[i] != 0) {
            tree->bits = i;
        }
    }
    
    // Init lookup table
    const int max = 1 << tree->bits;
    tree->bit_mask = (max) - 1;
    memset(tree->lookup_table, 0, sizeof(tree->lookup_table[0]) * max);
    
    // Assign numerical values to all codes, using consecutive values for all
    // codes of the same length with the base values determined at step 2.
    // Codes that are never used (which have a bit length of zero) must not be
    // assigned a value.
    for (i = 0;  i < length; i++) {
        int len = code_length[i];
        if (len != 0) {
            code = next_code[len];
            next_code[len]++;
            
            tree->lookup_table[reverse_bits(code, len)] = i | (len << VALUE_BITS);
        }
    }
    
    // Fill in the missing parts of the lookup table
    int next_limit = 1;
    int num_bits = 0;
    int mask = 0;
    for (i = 1; i < max; i++) {
        if (i == next_limit) {
            mask = (1 << num_bits) - 1;
            num_bits++;
            next_limit <<= 1;
        }
        if (tree->lookup_table[i] == 0) {
            tree->lookup_table[i] = tree->lookup_table[i & mask];
        }
    }
    
    return true;
}

static bool inflate_huffman_tree(ok_inflater *inflater, huffman_tree *tree, huffman_tree *code_length_huffman,
                                 int num_codes) {
    if (num_codes < 0 || num_codes >= MAX_NUM_CODES) {
        inflater_error(inflater, "Invalid num_codes");
        return false;
    }
    const uint16_t *tree_lookup_table = code_length_huffman->lookup_table;
    const int tree_bits = code_length_huffman->bits;
    // 0 - 15: Represent code lengths of 0 - 15
    //     16: Copy the previous code length 3 - 6 times.
    //         (2 bits of length)
    //     17: Repeat a code length of 0 for 3 - 10 times.
    //         (3 bits of length)
    //     18: Repeat a code length of 0 for 11 - 138 times
    //         (7 bits of length)
    while (inflater->state_count < num_codes) {
        if (inflater->huffman_code < 0) {
            inflater->huffman_code = decode_literal(inflater, tree_lookup_table, tree_bits);
            if (inflater->huffman_code < 0) {
                return false;
            }
        }
        if (inflater->huffman_code <= 15) {
            inflater->tree_codes[inflater->state_count++] = inflater->huffman_code;
        }
        else {
            int value = 0;
            int len;
            int len_bits;
            switch (inflater->huffman_code) {
                case 16:
                    len = 3;
                    len_bits = 2;
                    if (inflater->state_count == 0) {
                        inflater_error(inflater, "Invalid previous code");
                        return false;
                    }
                    value = inflater->tree_codes[inflater->state_count-1];
                    break;
                case 17:
                    len = 3;
                    len_bits = 3;
                    break;
                case 18:
                    len = 11;
                    len_bits = 7;
                    break;
                default:
                    inflater_error(inflater, "Invalid huffman code");
                    return false;
            }
            if (!load_bits(inflater, len_bits)) {
                return false;
            }
            len += read_bits(inflater, len_bits);
            if (len > num_codes - inflater->state_count) {
                inflater_error(inflater, "Invalid length");
                return false;
            }
            memset(inflater->tree_codes + inflater->state_count, value, len);
            inflater->state_count += len;
        }
        inflater->huffman_code = -1;
    }
    make_huffman_tree_from_array(tree, inflater->tree_codes, num_codes);
    return true;
}

// Inflate

static bool inflate_zlib_header(ok_inflater *inflater) {
    if (!load_bits(inflater, 16)) {
        return false;
    }
    else {
        int compression_method = read_bits(inflater, 4);
        int compression_info = read_bits(inflater, 4);
        int flag_check = read_bits(inflater, 5);
        int flag_dict = read_bits(inflater, 1);
        int flag_compression_level = read_bits(inflater, 2);
        
        int bits = (compression_info << 12) | (compression_method << 8) |
        (flag_compression_level << 6) | (flag_dict << 5) |  flag_check;
        if (bits % 31 != 0) {
            inflater_error(inflater, "Invalid zlib header");
            return false;
        }
        if (compression_method != 8) {
            inflater_error(inflater, "Invalid compression method: %i", compression_method);
            return false;
        }
        if (compression_info > 7) {
            inflater_error(inflater, "Invalid window size: %i", compression_info);
            return false;
        }
        if (flag_dict) {
            inflater_error(inflater, "Needs external dictionary");
            return false;
        }
        
        inflater->state = STATE_READY_FOR_NEXT_BLOCK;
        return true;
    }
}

static bool inflate_init_fixed_huffman(ok_inflater *inflater) {
    if (inflater->fixed_literal_huffman == NULL) {
        huffman_tree *tree = malloc(sizeof(huffman_tree));
        if (tree != NULL) {
            const int num_codes = 288;
            uint8_t code_length[num_codes];
            int i;
            for (i = 0; i < 144; i++) {
                code_length[i] = 8;
            }
            for (i = 144; i < 256; i++) {
                code_length[i] = 9;
            }
            for (i = 256; i < 280; i++) {
                code_length[i] = 7;
            }
            for (i = 280; i < 288; i++) {
                code_length[i] = 8;
            }
            make_huffman_tree_from_array(tree, code_length, num_codes);
            inflater->fixed_literal_huffman = tree;
        }
    }
    if (inflater->fixed_distance_huffman == NULL) {
        huffman_tree *tree = malloc(sizeof(huffman_tree));
        if (tree != NULL) {
            uint8_t distance_code_length[32];
            for (int i = 0; i < 32; i++) {
                distance_code_length[i] = 5;
            }
            make_huffman_tree_from_array(tree, distance_code_length, 32);
            inflater->fixed_distance_huffman = tree;
        }
    }
    return inflater->fixed_literal_huffman != NULL && inflater->fixed_distance_huffman != NULL;
}

static bool inflate_next_block(ok_inflater *inflater) {
    if (inflater->final_block) {
        inflater->state = STATE_DONE;
        skip_byte_align(inflater);
        return true;
    }
    else if (!load_bits(inflater, 3)) {
        return false;
    }
    else {
        inflater->final_block = read_bits(inflater, 1);
        int block_type = read_bits(inflater, 2);
        switch (block_type) {
            case BLOCK_TYPE_NO_COMPRESSION:
                inflater->state = STATE_READING_STORED_BLOCK_HEADER;
                break;
            case BLOCK_TYPE_DYNAMIC_HUFFMAN:
                inflater->state = STATE_READING_DYNAMIC_BLOCK_HEADER;
                break;
            case BLOCK_TYPE_FIXED_HUFFMAN:
            {
                if (!inflate_init_fixed_huffman(inflater)) {
                    inflater_error(inflater, "Couldn't initilize fixed huffman trees");
                    return false;
                }
                inflater->state = STATE_READING_FIXED_COMPRESSED_BLOCK;
                inflater->huffman_code = -1;
                break;
            }
            default:
                inflater_error(inflater, "Invalid block type: %i", block_type);
                break;
        }
        return true;
    }
}

static bool inflate_stored_block_header(ok_inflater *inflater) {
    skip_byte_align(inflater);
    if (!load_bits(inflater, 32)) {
        return false;
    }
    else {
        int len = read_bits(inflater, 16);
        int clen = read_bits(inflater, 16);
        if ((len & 0xffff) != ((~clen) & 0xffff)) {
            inflater_error(inflater, "Invalid stored block");
            return false;
        }
        else if (len == 0) {
            inflater->state = STATE_READY_FOR_NEXT_BLOCK;
            return true;
        }
        else {
            inflater->state = STATE_READING_STORED_BLOCK;
            inflater->state_count = len;
            return true;
        }
    }
}

static bool inflate_stored_block(ok_inflater *inflater) {
    const size_t can_read = inflater->input_end - inflater->input;
    if (can_read == 0) {
        return false;
    }
    else {
        size_t len = write_bytes(inflater, inflater->input, min(can_read, (size_t)inflater->state_count));
        if (len == 0) {
            // Buffer full
            return false;
        }
        inflater->input += len;
        inflater->state_count -= len;
        if (inflater->state_count == 0) {
            inflater->state = STATE_READY_FOR_NEXT_BLOCK;
        }
        return true;
    }
}

static int decode_distance(ok_inflater *inflater, int value) {
    if (value < 0 || value >= DISTANCE_TABLE_LENGTH) {
        inflater_error(inflater, "Invalid distance");
        return -1;
    }
    int distance = DISTANCE_TABLE[value];
    int extra_bits = (value >> 1) - 1;
    if (extra_bits > 0) {
        if (!load_bits(inflater, extra_bits)) {
            return -1;
        }
        distance += read_bits(inflater, extra_bits);
    }
    return distance;
}

static int decode_length(ok_inflater *inflater, int value) {
    int len = LENGTH_TABLE[value];
    int extra_bits = (value >> 2) - 1;
    if (extra_bits > 0 && extra_bits <= 5) {
        if (!load_bits(inflater, extra_bits)) {
            return -1;
        }
        len += read_bits(inflater, extra_bits);
    }
    return len;
}

static bool inflate_distance(ok_inflater *inflater) {
    const bool is_fixed = inflater->state == STATE_READING_FIXED_DISTANCE;
    if (inflater->state_count < 0) {
        inflater->state_count = decode_length(inflater, inflater->huffman_code);
        if (inflater->state_count < 0) {
            // Needs input
            return false;
        }
    }
    if (inflater->state_literal < 0) {
        huffman_tree *curr_distance_huffman = is_fixed ? inflater->fixed_distance_huffman : inflater->distance_huffman;
        const uint16_t *tree_lookup_table = curr_distance_huffman->lookup_table;
        const int tree_bits = curr_distance_huffman->bits;
        inflater->state_literal = decode_literal(inflater, tree_lookup_table, tree_bits);
        if (inflater->state_literal < 0) {
            // Needs input
            return false;
        }
    }
    if (inflater->state_distance < 0) {
        inflater->state_distance = decode_distance(inflater, inflater->state_literal);
        if (inflater->state_distance < 0) {
            // Needs input
            return false;
        }
        else if (inflater->state_distance == 0 || inflater->state_distance >= BUFFER_SIZE) {
            inflater_error(inflater, "Invalid distance");
            return false;
        }
    }
    
     // Copy len bytes from offset to buffer_end_pos
    if (inflater->state_count > 0) {
        uint16_t buffer_offset = inflater->buffer_end_pos - inflater->state_distance;
        if (inflater->state_distance == 1) {
            // Optimization: can use memset
            const size_t n = inflater->state_count;
            const size_t n2 = write_byte_n(inflater, inflater->buffer[buffer_offset], n);
            inflater->state_count -= n2;
            if (n2 != n) {
                // Full buffer
                return false;
            }
        }
        else  if (buffer_offset + inflater->state_count < BUFFER_SIZE) {
            // Optimization: the offset won't wrap
            size_t bytes_copyable = inflater->state_distance;
            while (inflater->state_count > 0) {
                const size_t n = min((size_t)inflater->state_count, bytes_copyable);
                const size_t n2 = write_bytes(inflater, inflater->buffer + buffer_offset, n);
                inflater->state_count -= n2;
                bytes_copyable += n2;
                if (n2 != n) {
                    // Full buffer
                    return false;
                }
            }
        }
        else {
            // This could be optimized, but it happens much less often, so it's probably not worth it
            while (inflater->state_count > 0) {
                size_t n = min(inflater->state_count, inflater->state_distance);
                n = min(n, BUFFER_SIZE - buffer_offset);
                const size_t n2 = write_bytes(inflater, inflater->buffer + buffer_offset, n);
                inflater->state_count -= n2;
                buffer_offset += n2;
                if (n2 != n) {
                    // Full buffer
                    return false;
                }
            }
        }
    }
    
    if (is_fixed) {
        inflater->state = STATE_READING_FIXED_COMPRESSED_BLOCK;
    }
    else {
        inflater->state = STATE_READING_DYNAMIC_COMPRESSED_BLOCK;
    }
    inflater->huffman_code = -1;
    return true;
}

static bool inflate_dynamic_block_header(ok_inflater *inflater) {
    if (!load_bits(inflater, 14)) {
        return false;
    }
    else {
        inflater->num_literal_codes = read_bits(inflater, 5) + 257;
        inflater->num_distance_codes  = read_bits(inflater, 5) + 1;
        inflater->num_code_length_codes = read_bits(inflater, 4) + 4;
        
        for (int i = inflater->num_code_length_codes; i < BIT_LENGTH_TABLE_LENGTH; i++) {
            inflater->tree_codes[BIT_LENGTH_TABLE[i]] = 0;
        }
        
        inflater->state = STATE_READING_DYNAMIC_CODE_LENGTHS;
        inflater->state_count = inflater->num_code_length_codes;
        return true;
    }
}

static bool inflate_dynamic_block_code_lengths(ok_inflater *inflater) {
    while (inflater->state_count > 0) {
        if (!load_bits(inflater, 3)) {
            return false;
        }
        int index = inflater->num_code_length_codes - inflater->state_count;
        inflater->tree_codes[BIT_LENGTH_TABLE[index]] = read_bits(inflater, 3);
        inflater->state_count--;
    }
    make_huffman_tree_from_array(inflater->code_length_huffman,
                                 inflater->tree_codes, BIT_LENGTH_TABLE_LENGTH);
        
    inflater->state = STATE_READING_DYNAMIC_LITERAL_TREE;
    inflater->huffman_code = -1;
    inflater->state_count = 0;
    return true;
}

static bool inflate_compressed_block(ok_inflater *inflater) {
    const bool is_fixed = inflater->state == STATE_READING_FIXED_COMPRESSED_BLOCK;
    const huffman_tree *curr_literal_huffman = is_fixed ? inflater->fixed_literal_huffman : inflater->literal_huffman;
    
    // decode literal/length value from input stream
    
    size_t max_write = can_write_total(inflater);
    if (max_write == 0) {
        return false;
    }
    const uint16_t *tree_lookup_table = curr_literal_huffman->lookup_table;
    const int tree_bits = curr_literal_huffman->bits;
    while (true) {
        int value = decode_literal(inflater, tree_lookup_table, tree_bits);
        if (value < 0) {
            // Needs input
            return false;
        }
        else if (value < 256) {
            write_byte(inflater, value);
            max_write--;
            if (max_write == 0) {
                return false;
            }
        }
        else if (value == 256) {
            inflater->state = STATE_READY_FOR_NEXT_BLOCK;
            return true;
        }
        else if (value < 286) {
            if (is_fixed) {
                inflater->state = STATE_READING_FIXED_DISTANCE;
            }
            else {
                inflater->state = STATE_READING_DYNAMIC_DISTANCE;
            }
            inflater->huffman_code = value - 257;
            inflater->state_count = -1;
            inflater->state_literal = -1;
            inflater->state_distance = -1;
            return true;
        }
        else {
            inflater_error(inflater, "Invalid literal: %i: ", inflater->huffman_code);
            return false;
        }
    }
}

static bool inflate_literal_tree(ok_inflater *inflater) {
    bool done = inflate_huffman_tree(inflater, inflater->literal_huffman,
                                     inflater->code_length_huffman, inflater->num_literal_codes);
    if (done) {
        inflater->state = STATE_READING_DYNAMIC_DISTANCE_TREE;
        inflater->huffman_code = -1;
        inflater->state_count = 0;
        return true;
    }
    else {
        return false;
    }
}

static bool inflate_distance_tree(ok_inflater *inflater) {
    bool done = inflate_huffman_tree(inflater, inflater->distance_huffman,
                                     inflater->code_length_huffman, inflater->num_distance_codes);
    if (done) {
        inflater->state = STATE_READING_DYNAMIC_COMPRESSED_BLOCK;
        inflater->huffman_code = -1;
        return true;
    }
    else {
        return false;
    }
}

static bool inflate_noop(ok_inflater *inflater) {
    return false;
}

bool (*STATE_FUNCTIONS[NUM_STATES]) (ok_inflater *);

// Public Inflater API

ok_inflater *ok_inflater_init(const bool nowrap) {
    
    STATE_FUNCTIONS[STATE_READY_FOR_HEAD] = inflate_zlib_header;
    STATE_FUNCTIONS[STATE_READY_FOR_NEXT_BLOCK] = inflate_next_block;
    STATE_FUNCTIONS[STATE_READING_STORED_BLOCK_HEADER] = inflate_stored_block_header;
    STATE_FUNCTIONS[STATE_READING_STORED_BLOCK] = inflate_stored_block;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_BLOCK_HEADER] = inflate_dynamic_block_header;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_CODE_LENGTHS] = inflate_dynamic_block_code_lengths;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_LITERAL_TREE] = inflate_literal_tree;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_DISTANCE_TREE] = inflate_distance_tree;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_COMPRESSED_BLOCK] = inflate_compressed_block;
    STATE_FUNCTIONS[STATE_READING_FIXED_COMPRESSED_BLOCK] = inflate_compressed_block;
    STATE_FUNCTIONS[STATE_READING_DYNAMIC_DISTANCE] = inflate_distance;
    STATE_FUNCTIONS[STATE_READING_FIXED_DISTANCE] = inflate_distance;
    STATE_FUNCTIONS[STATE_DONE] = inflate_noop;
    STATE_FUNCTIONS[STATE_ERROR] = inflate_noop;
    
    ok_inflater *inflater = calloc(1, sizeof(ok_inflater));
    if (inflater != NULL) {
        inflater->nowrap = nowrap;
        inflater->state = nowrap ? STATE_READY_FOR_NEXT_BLOCK : STATE_READY_FOR_HEAD;
        inflater->buffer = malloc(BUFFER_SIZE);
        inflater->code_length_huffman = malloc(sizeof(huffman_tree));
        inflater->literal_huffman = malloc(sizeof(huffman_tree));
        inflater->distance_huffman = malloc(sizeof(huffman_tree));
        
        if (inflater->buffer == NULL ||
            inflater->code_length_huffman == NULL ||
            inflater->literal_huffman == NULL ||
            inflater->distance_huffman == NULL) {
            ok_inflater_free(inflater);
            inflater = NULL;
        }
    }
    return inflater;
}

void ok_inflater_reset(ok_inflater *inflater) {
    if (inflater != NULL) {
        inflater->input = NULL;
        inflater->input_end = NULL;
        inflater->input_buffer = 0;
        inflater->input_buffer_bits = 0;
        
        inflater->buffer_start_pos = 0;
        inflater->buffer_end_pos = 0;
        inflater->final_block = false;
        inflater->state = inflater->nowrap ? STATE_READY_FOR_NEXT_BLOCK : STATE_READY_FOR_HEAD;

        memset(inflater->error_message, 0, sizeof(inflater->error_message));
    }
}

void ok_inflater_free(ok_inflater *inflater) {
    if (inflater != NULL) {
        if (inflater->buffer != NULL) {
            free(inflater->buffer);
        }
        if (inflater->code_length_huffman != NULL) {
            free(inflater->code_length_huffman);
        }
        if (inflater->literal_huffman != NULL) {
            free(inflater->literal_huffman);
        }
        if (inflater->distance_huffman != NULL) {
            free(inflater->distance_huffman);
        }
        if (inflater->fixed_literal_huffman != NULL) {
            free(inflater->fixed_literal_huffman);
        }
        if (inflater->fixed_distance_huffman != NULL) {
            free(inflater->fixed_distance_huffman);
        }
        free(inflater);
    }
}

const char *ok_inflater_error_message(const struct ok_inflater *inflater) {
    if (inflater == NULL) {
        return "";
    }
    else {
        return inflater->error_message;
    }
}

bool ok_inflater_needs_input(const ok_inflater *inflater) {
    return inflater != NULL &&
    inflater->state != STATE_ERROR &&
    can_flush_total(inflater) == 0 &&
    inflater->input == inflater->input_end;
}

void ok_inflater_set_input(ok_inflater *inflater, const void *buffer, const size_t buffer_length) {
    if (inflater != NULL) {
        if (inflater->input == inflater->input_end) {
            inflater->input = (uint8_t*)buffer;
            inflater->input_end = inflater->input + buffer_length;
        }
        else {
            inflater_error(inflater, "ok_inflater_set_input was called with unread input data.");
        }
    }
}

intptr_t ok_inflater_inflate(ok_inflater *inflater, uint8_t *dst, const size_t dst_len) {
    if (inflater == NULL || inflater->state == STATE_ERROR) {
        return -1;
    }

    // Each state function should return false if input is needed or the buffer is full.
    // Run until one condition occurs:
    // 1. Output buffer can be filled,
    // 2. Internal buffer is full,
    // 3. Needs more input,
    // 4. Done inflating, or
    // 5. An error occured.
    while (can_flush_total(inflater) < dst_len &&
           (*STATE_FUNCTIONS[inflater->state])(inflater)) {
        
    }
    return flush(inflater, dst, dst_len);
}
