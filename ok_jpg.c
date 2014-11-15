/*
 + JPEG spec
   http://www.w3.org/Graphics/JPEG/itu-t81.pdf
   http://www.w3.org/Graphics/JPEG/jfif3.pdf
 + Another easy-to-read JPEG decoder (written in python)
   https://github.com/enmasse/jpeg_read/blob/master/jpeg_read.py
 */

#include "ok_jpg.h"
#include <memory.h>
#include <stdarg.h>
#include <stddef.h> // For ptrdiff_t
#include <stdio.h> // For vsnprintf
#include <stdlib.h>
#include <errno.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// Output channels are seperated so that output can be either RGBA or BGRA.
typedef void (*convert_row_func)(const int in_width, const int out_width, const uint8_t *Y,
                                 const uint8_t *Cb, const uint8_t *Cr,
                                 const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                                 uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
typedef struct {
    uint8_t id;
    uint8_t H;
    uint8_t V;
    uint8_t Tq;
    uint8_t Td;
    uint8_t Ta;
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint8_t upsample_h;
    uint8_t upsample_v;
    int pred;
} component;

typedef struct {
    uint16_t code[256];
    uint8_t val[256];
    uint8_t size[257];
    uint8_t lookup_num_bits[256];
    uint8_t lookup_val[256];
    int maxcode[16];
    int mincode[16];
    int valptr[16];
    int count; // "lastk" in spec
} huffman_table;

typedef struct {
    // Output image
    ok_image *image;
    
    // Decode options
    ok_color_format color_format;
    bool flip_y;
    bool info_only;
    
    // Input
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;

    // State
    bool eoi_found;
    bool complete;
    int next_marker;
    int restart_intervals;
    int restart_intervals_remaining;
    uint32_t input_buffer;
    int input_buffer_bits;

    // JPEG data
    int data_units_x;
    int data_units_y;
    int num_components;
    component components[3];
    uint8_t q_table[4][64];
    convert_row_func convert_row;
    
    // 0 = DC table, 1 = AC table
    huffman_table huffman_tables[2][4];
} jpg_decoder;

__attribute__((__format__ (__printf__, 2, 3)))
static void ok_image_error(ok_image *image, const char *format, ... ) {
    if (image != NULL) {
        image->width = 0;
        image->height = 0;
        if (image->data != NULL) {
            free(image->data);
            image->data = NULL;
        }
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(image->error_message, sizeof(image->error_message), format, args);
            va_end(args);
        }
    }
}

static bool ok_read(jpg_decoder *decoder, uint8_t *data, const size_t length) {
    if (decoder->read_func(decoder->reader_data, data, length) == length) {
        return true;
    }
    else {
        ok_image_error(decoder->image, "Read error: error calling read function.");
        return false;
    }
}

static bool ok_seek(jpg_decoder *decoder, const int length) {
    if (decoder->seek_func(decoder->reader_data, length) == 0) {
        return true;
    }
    else {
        ok_image_error(decoder->image, "Read error: error calling seek function.");
        return false;
    }
}

static ok_image *decode_jpg(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                            const ok_color_format color_format, const bool flip_y, const bool info_only);

//
// Public API
//

ok_image *ok_jpg_read_info(void *user_data, ok_read_func read_func, ok_seek_func seek_func) {
    return decode_jpg(user_data, read_func, seek_func, OK_COLOR_FORMAT_RGBA, false, true);
}

ok_image *ok_jpg_read(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                      const ok_color_format color_format, const bool flip_y) {
    return decode_jpg(user_data, read_func, seek_func, color_format, flip_y, false);
}

void ok_jpg_image_free(ok_image *image) {
    if (image != NULL) {
        if (image->data != NULL) {
            free(image->data);
        }
        free(image);
    }
}

//
// JPEG bit reading
//

static inline uint16_t readBE16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

// Load bits without reading them
static inline bool load_bits(jpg_decoder *decoder, const int num_bits) {
    // TODO: Optimize. Buffer data instead of calling ok_read for every byte?
    // This is the number one thing that need optimization!
    while (decoder->input_buffer_bits < num_bits) {
        if (decoder->next_marker != 0) {
            decoder->input_buffer <<= 8;
            decoder->input_buffer_bits += 8;
        }
        else {
            uint8_t b;
            if (!ok_read(decoder, &b, 1)) {
                return false;
            }
            if (b == 0xff) {
                uint8_t marker;
                if (!ok_read(decoder, &marker, 1)) {
                    return false;
                }
                if (marker != 0) {
                    decoder->next_marker = marker;
                    b = 0;
                }
            }
            decoder->input_buffer = (decoder->input_buffer << 8) | b;
            decoder->input_buffer_bits += 8;
        }
    }
    return true;
}

// Assumes at least 1 bit of data was previously loaded in load_bits
static inline int next_bit(jpg_decoder *decoder) {
    decoder->input_buffer_bits--;
    return (decoder->input_buffer >> decoder->input_buffer_bits) & 1;
}

// Assumes at least num_bits of data was previously loaded in load_bits
static inline int peek_bits(jpg_decoder *decoder, const int num_bits) {
    int b = decoder->input_buffer_bits - num_bits;
    return (decoder->input_buffer >> b) & ((1 << num_bits) - 1);
}

// Assumes at least num_bits of data was previously loaded in load_bits
static inline void consume_bits(jpg_decoder *decoder, const int num_bits) {
    decoder->input_buffer_bits -= num_bits;
}

static inline void dump_bits(jpg_decoder *decoder) {
    decoder->input_buffer = 0;
    decoder->input_buffer_bits = 0;
}

static inline int load_next_bits(jpg_decoder *decoder, const int num_bits) {
    if (!load_bits(decoder, num_bits)) {
        return -1;
    }
    decoder->input_buffer_bits -= num_bits;
    return (decoder->input_buffer >> decoder->input_buffer_bits) & ((1 << num_bits) - 1);
}

//
// Huffman decoding
//

static void generate_huffman_table(huffman_table *huff, const uint8_t *bits) {
    // JPEG spec: "Generate_size_table"
    int k = 0;
    for (int i = 1; i <= 16; i++) {
        for (int j = 1; j <= bits[i]; j++) {
            huff->size[k++] = i;
        }
    }
    huff->size[k] = 0;
    huff->count = k;
    
    // JPEG spec: "Generate_code_table"
    k = 0;
    int code = 0;
    int si = huff->size[0];
    while (true) {
        huff->code[k] = code;
        code++;
        k++;
        int si2 = huff->size[k];
        if (si2 == 0) {
            break;
        }
        if (si2 > si) {
            code <<= (si2 - si);
            si = si2;
        }
    }
    
    // JPEG spec: "Decoder_tables"
    int j = 0;
    for (int i = 0; i < 16; i++) {
        if (bits[i + 1] == 0) {
            huff->maxcode[i] = -1;
        }
        else {
            huff->valptr[i] = j;
            huff->mincode[i] = huff->code[j];
            j += bits[i + 1];
            huff->maxcode[i] = huff->code[j - 1];
        }
    }
}

static void generate_huffman_table_lookups(huffman_table *huff) {

    // Look up table for codes that use 8 bits or less (most of them)
    for (int q = 0; q < 256; q++) {
        huff->lookup_num_bits[q] = 0;
        for (int i = 0; i < 8; i++) {
            int num_bits = i + 1;
            int code = q >> (8 - num_bits);
            if (code <= huff->maxcode[i]) {
                huff->lookup_num_bits[q] = num_bits;
                
                int j = huff->valptr[i];
                j += code - huff->mincode[i];
                huff->lookup_val[q] = huff->val[j];
                break;
            }
        }
    }
}

static inline int huffman_decode(jpg_decoder *decoder, const huffman_table *table) {
    // JPEG spec: "Decode" (Figure F.16)

    // First, try 8-bit lookup tables (most codes wil be 8-bit or less)
    if (!load_bits(decoder, 8)) {
        return -1;
    }
    int code = peek_bits(decoder, 8);
    int num_bits = table->lookup_num_bits[code];
    if (num_bits != 0) {
        consume_bits(decoder, num_bits);
        return table->lookup_val[code];
    }
    
    // Next, try a code up to 16-bits
    // TODO: Needs optimization for codes > 8 bits?
    consume_bits(decoder, 8);
    if (!load_bits(decoder, 8)) {
        return -1;
    }
    for (int i = 8; i < 16; i++) {
        code = (code << 1) | next_bit(decoder);
        if (code <= table->maxcode[i]) {
            int j = table->valptr[i];
            j += code - table->mincode[i];
            return table->val[j];
        }
    }
    // Shouldn't happen
    ok_image_error(decoder->image, "Invalid huffman code");
    return -1;
}

//
// JPEG color conversion and upsampling
//

static inline uint8_t clip_uint8(const int x) {
    return ((unsigned int)x) < 0xff ? x : (x < 0 ? 0 : 0xff);
}

static inline uint8_t clip_fp_uint8(const int fx) {
    return ((unsigned int)fx) < 0xff0000 ? (fx >> 16) : (fx < 0 ? 0 : 0xff);
}

static inline uint8_t clip_fp20_uint8(const int fx) {
    return ((unsigned int)fx) < 0xff00000 ? (fx >> 20) : (fx < 0 ? 0 : 0xff);
}

// From the JFIF spec. Converted to 16:16 fixed point.
static const int fx1 = 91881;  // 1.402
static const int fx2 = -22553; // 0.34414
static const int fx3 = -46802; // 0.71414
static const int fx4 = 116130; // 1.772

static inline void convert_YCbCr_to_RGB(uint8_t Y, uint8_t Cb, uint8_t Cr,
                                        uint8_t *r, uint8_t *g, uint8_t *b) {
    const int fy = (Y << 16) + (1 << 15);
    const int fr = fy + fx1 * (Cr - 128);
    const int fg = fy + fx2 * (Cb - 128) + fx3 * (Cr - 128);
    const int fb = fy + fx4 * (Cb - 128);
    *r = clip_fp_uint8(fr);
    *g = clip_fp_uint8(fg);
    *b = clip_fp_uint8(fb);
}

// Cb and Cr are 8:4 fixed point
static inline void convert_YCbCr_fp_to_RGB(uint8_t Y, int Cb, int Cr,
                                           uint8_t *r, uint8_t *g, uint8_t *b) {
    const int fy = (Y << 20) + (1 << 19);
    const int fr = fy + fx1 * Cr;
    const int fg = fy + fx2 * Cb + fx3 * Cr;
    const int fb = fy + fx4 * Cb;
    *r = clip_fp20_uint8(fr);
    *g = clip_fp20_uint8(fg);
    *b = clip_fp20_uint8(fb);
}

// Convert row from grayscale to RGBA (no upsampling)
static void convert_row_grayscale(const int in_width, const int out_width, const uint8_t *Y,
                                  const uint8_t *Cb, const uint8_t *Cr,
                                  const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                                  uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *Y_end = Y + out_width;
    while (Y < Y_end) {
        *r = *g = *b = *Y;
        *a = 0xff;
        r += 4; g += 4; b += 4; a += 4;
        Y++;
    }
}

// Convert row from YCbCr to RGBA (no upsampling)
static void convert_row_h1v1(const int in_width, const int out_width, const uint8_t *Y,
                             const uint8_t *Cb, const uint8_t *Cr,
                             const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                             uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *Y_end = Y + out_width;
    while (Y < Y_end) {
        convert_YCbCr_to_RGB(*Y, *Cb, *Cr, r, g, b);
        *a = 0xff;
        r += 4; g += 4; b += 4; a += 4;
        Y++; Cb++; Cr++;
    }
}

// Convert row from YCbCr to RGBA (upsample Cb and Cr vertically via triangle filter)
static void convert_row_h1v2(const int in_width, const int out_width, const uint8_t *Y,
                             const uint8_t *Cb, const uint8_t *Cr,
                             const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                             uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *Y_end = Y + out_width;
    while (Y < Y_end) {
        int Cb_s = (*Cb)*12 + (*Cb_minor << 2) - 0x800;
        int Cr_s = (*Cr)*12 + (*Cr_minor << 2) - 0x800;
        convert_YCbCr_fp_to_RGB(*Y, Cb_s, Cr_s, r, g, b);
        *a = 0xff;
        r += 4; g += 4; b += 4; a += 4;
        Y++; Cb++; Cr++; Cb_minor++; Cr_minor++;
    }
}

// Convert row from YCbCr to RGBA (upsample Cb and Cr horizontally via triangle filter)
static void convert_row_h2v1(const int in_width, const int out_width, const uint8_t *Y,
                             const uint8_t *Cb, const uint8_t *Cr,
                             const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                             uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *Y_end = Y + out_width;

    // First pixel
    convert_YCbCr_to_RGB(*Y, *Cb, *Cr, r, g, b);
    *a = 0xff;
    r += 4; g += 4; b += 4; a += 4;
    Y++;
    
    while (Y < Y_end - 1) {
        for (int x = 0; x < 2; x++) {
            int Cb_s = Cb[x]*12 + (Cb[1-x] << 2) - 0x800;
            int Cr_s = Cr[x]*12 + (Cr[1-x] << 2) - 0x800;
            convert_YCbCr_fp_to_RGB(*Y, Cb_s, Cr_s, r, g, b);
            *a = 0xff;
            r += 4; g += 4; b += 4; a += 4;
            Y++;
        }
        
        Cb++; Cr++;
    }
    
    // Last pixel
    if (Y < Y_end) {
        convert_YCbCr_to_RGB(*Y, *Cb, *Cr, r, g, b);
        *a = 0xff;
    }
}

// Convert row from YCbCr to RGBA (upsample Cb and Cr via triangle filter)
static void convert_row_h2v2(const int in_width, const int out_width, const uint8_t *Y,
                             const uint8_t *Cb, const uint8_t *Cr,
                             const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                             uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    const uint8_t *Y_end = Y + out_width;
    
    // First pixel
    int Cb_s = Cb[0]*12 + (Cb_minor[0] << 2) - 0x800;
    int Cr_s = Cr[0]*12 + (Cr_minor[0] << 2) - 0x800;
    convert_YCbCr_fp_to_RGB(*Y, Cb_s, Cr_s, r, g, b);
    *a = 0xff;
    r += 4; g += 4; b += 4; a += 4;
    Y++;
    
    while (Y < Y_end - 1) {
        for (int x = 0; x < 2; x++) {
            Cb_s = Cb[x]*9 + (Cb[1-x] + Cb_minor[x])*3 + Cb_minor[1-x] - 0x800;
            Cr_s = Cr[x]*9 + (Cr[1-x] + Cr_minor[x])*3 + Cr_minor[1-x] - 0x800;
            convert_YCbCr_fp_to_RGB(*Y, Cb_s, Cr_s, r, g, b);
            *a = 0xff;
            r += 4; g += 4; b += 4; a += 4;
            Y++;
        }
        
        Cb++; Cr++; Cb_minor++; Cr_minor++;
    }
    
    // Last pixel
    if (Y < Y_end) {
        Cb_s = Cb[0]*12 + (Cb_minor[0] << 2) - 0x800;
        Cr_s = Cr[0]*12 + (Cr_minor[0] << 2) - 0x800;
        convert_YCbCr_fp_to_RGB(*Y, Cb_s, Cr_s, r, g, b);
        *a = 0xff;
    }
}

// Box filter
static void convert_row_generic(const int in_width, const int out_width, const uint8_t *Y,
                                const uint8_t *Cb, const uint8_t *Cr,
                                const uint8_t *Cb_minor, const uint8_t *Cr_minor,
                                uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    for (int x = 0; x < out_width; x++) {
        int off = (in_width * x) / out_width;
        convert_YCbCr_to_RGB(*Y, Cb[off], Cr[off], r, g, b);
        *a = 0xff;
        r += 4; g += 4; b += 4; a += 4;
        Y++;
    }
}

//
// IDCT
//

// 1D Inverse Discrete Cosine Transform
// This function was created by first creating a naive implementation, and then
// unrolling loops and optimizing by hand.
// Once loops were unrolled, redundant computations were obvious, and they could be eliminated.
// 1. Converted to integer (fixed-point)
// 2. Scaled output by sqrt(2).
// TODO: Optimize. Use SIMD code, and/or one of these papers on IDCTs:
// http://www3.matapp.unimib.it/corsi-2007-2008/matematica/istituzioni-di-analisi-numerica/jpeg/papers/ \
// 11-multiplications.pdf (Loeffler)
// http://www.reznik.org/papers/SPIE07_MPEG-C_IDCT.pdf (Reznik)
static inline void idct_1d(int *out, const int v0, const int v1, const int v2, const int v3, const int v4,
                           const int v5, const int v6, const int v7, const int out_shift) {
    // Constants scaled by (1 << 12).
    static const int c1_sqrt2 = 5681; // cos(1*pi/16) * sqrt(2)
    static const int c2_sqrt2 = 5352; // cos(2*pi/16) * sqrt(2)
    static const int c3_sqrt2 = 4816; // cos(3*pi/16) * sqrt(2)
    static const int c5_sqrt2 = 3218; // cos(5*pi/16) * sqrt(2)
    static const int c6_sqrt2 = 2217; // cos(6*pi/16) * sqrt(2)
    static const int c7_sqrt2 = 1130; // cos(7*pi/16) * sqrt(2)
    
    int a0 = (v0 << 12) + (1 << (out_shift - 1));
    int a1 = (v4 << 12);
    int a2 = a0 + a1;
    int a3 = a0 - a1;

    // Quick check to avoid mults
    if (v1 == 0 && v2 == 0 && v3 == 0 &&
        v5 == 0 && v6 == 0 && v7 == 0) {
        a2 >>= out_shift;
        a3 >>= out_shift;
        out[0] = a2;
        out[1] = a3;
        out[2] = a3;
        out[3] = a2;
        out[4] = a2;
        out[5] = a3;
        out[6] = a3;
        out[7] = a2;
        return;
    }
    
    // 20 mults, 18 adds
    int a4 = v2 * c2_sqrt2 + v6 * c6_sqrt2;
    int a5 = v2 * c6_sqrt2 - v6 * c2_sqrt2;
    const int p1 = a2 + a4;
    const int p2 = a3 + a5;
    const int p3 = a3 - a5;
    const int p4 = a2 - a4;
    const int q1 = v1 * c1_sqrt2 + v3 * c3_sqrt2 + v5 * c5_sqrt2 + v7 * c7_sqrt2;
    const int q2 = v1 * c3_sqrt2 - v3 * c7_sqrt2 - v5 * c1_sqrt2 - v7 * c5_sqrt2;
    const int q3 = v1 * c5_sqrt2 - v3 * c1_sqrt2 + v5 * c7_sqrt2 + v7 * c3_sqrt2;
    const int q4 = v1 * c7_sqrt2 - v3 * c5_sqrt2 + v5 * c3_sqrt2 - v7 * c1_sqrt2;
    
    // 8 adds, 8 shifts
    out[0] = (p1 + q1) >> out_shift;
    out[1] = (p2 + q2) >> out_shift;
    out[2] = (p3 + q3) >> out_shift;
    out[3] = (p4 + q4) >> out_shift;
    out[4] = (p4 - q4) >> out_shift;
    out[5] = (p3 - q3) >> out_shift;
    out[6] = (p2 - q2) >> out_shift;
    out[7] = (p1 - q1) >> out_shift;
}

// From JPEG spec, "A.3.3"
static void idct(int *in, uint8_t *out, const int out_stride) {
    int temp[8*8];
    int *v = in;
    int *r = temp;
    for (int u = 0; u < 8; u++) {
        // idct_1d scales output by ((1 << 12) * sqrt(2)).
        // Shift-right by 8 so output is scaled ((1 << 4) * sqrt(2)).
        idct_1d(r, v[0], v[8], v[16], v[24], v[32], v[40], v[48], v[56], 8);
        r += 8;
        v++;
    }
    v = temp;
    r = in;
    for (int y = 0; y < 8; y++) {
        // Input is scaled by ((1 << 4) * sqrt(2)).
        // idct_1d scales output by ((1 << 12) * sqrt(2)), for a total output scale of (1 << 17).
        // Shift by 19 to get rid of the scale and to divide by 4 at the same time.
        // (Divide by 4 per the IDCT formula, JPEG spec section A.3.3)
        idct_1d(r, v[0], v[8], v[16], v[24], v[32], v[40], v[48], v[56], 19);
        for (int x = 0; x < 8; x++) {
            // Add 128 per JPEG spec section F.2.1.5
            out[x] = clip_uint8(r[x] + 128);
        }
        out += out_stride;
        v++;
    }
}

//
// Entropy decoding
//

static const uint8_t zig_zag[] = {
    0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

static inline int extend(const int v, const uint8_t t) {
    // Figure F.12
    if (v < (1 << (t - 1))) {
        return v + ((-1) << t) + 1;
    }
    else {
        return v;
    }
}

static bool decode_data_unit(jpg_decoder *decoder, component *c, uint8_t *out) {

    int block[8*8];

    const uint8_t *q_table = decoder->q_table[c->Tq];
    memset(block, 0, sizeof(block));
    
    // Decode DC coefficients - F.2.2.1
    huffman_table *dc = decoder->huffman_tables[0] + c->Td;
    int t = huffman_decode(decoder, dc);
    if (t < 0) {
        return false;
    }
    int diff;
    if (t == 0) {
        diff = 0;
    }
    else {
        diff = load_next_bits(decoder, t);
        if (diff < 0) {
            return false;
        }
        diff = extend(diff, t);
    }
    
    c->pred += diff;
    block[0] = c->pred * q_table[0];
    
    // Decode AC coefficients - Figure F.13 and F.14
    huffman_table *ac = decoder->huffman_tables[1] + c->Ta;
    int k = 1;
    while (k < 64) {
        int rs = huffman_decode(decoder, ac);
        if (rs < 0) {
            return false;
        }
        int s = rs & 0x0f;
        int r = rs >> 4;
        if (s == 0) {
            if (r != 0x0f) {
                break;
            }
            k += 16;
        }
        else {
            k += r;
            if (k > 63) {
                ok_image_error(decoder->image, "Invalid block index");
                return false;
            }
            int zzk = load_next_bits(decoder, s);
            if (zzk < 0) {
                return false;
            }
            block[zig_zag[k]] = extend(zzk, s) * q_table[k];
            k++;
        }
    }
    
    idct(block, out, c->stride);
    return true;
}

static void decode_restart(jpg_decoder *decoder) {
    decoder->restart_intervals_remaining = decoder->restart_intervals;
    for (int i = 0; i < decoder->num_components; i++) {
        decoder->components[i].pred = 0;
    }
}

static bool decode_scan(jpg_decoder *decoder) {
    ok_image *image = decoder->image;
    int next_restart = 0;
    decode_restart(decoder);
    for (int data_unit_y = 0; data_unit_y < decoder->data_units_y; data_unit_y++) {
        for (int data_unit_x = 0; data_unit_x < decoder->data_units_x; data_unit_x++) {
            
            for (int i = 0; i < decoder->num_components; i++) {
                component *c = decoder->components + i;
                for (int y = 0; y < c->V; y++) {
                    ptrdiff_t offset_y = (data_unit_y * c->V + y) * 8 * c->stride;
                    for (int x = 0; x < c->H; x++) {
                        ptrdiff_t offset_x = (data_unit_x * c->H + x) * 8;
                        if (!decode_data_unit(decoder, c, c->data + offset_y + offset_x)) {
                            return false;
                        }
                    }
                }
            }
            
            if (decoder->restart_intervals_remaining > 0) {
                decoder->restart_intervals_remaining--;
                
                // Read the restart marker (if we're not at the end)
                if (decoder->restart_intervals_remaining == 0) {
                    
                    const bool at_end = (data_unit_x == decoder->data_units_x - 1 &&
                                         data_unit_y == decoder->data_units_y - 1);
                    if (!at_end) {
                        dump_bits(decoder);
                        if (decoder->next_marker != 0) {
                            if (decoder->next_marker == 0xD0 + next_restart) {
                                decoder->next_marker = 0;
                            }
                            else {
                                ok_image_error(image, "Invalid restart marker (1)");
                                return false;
                            }
                        }
                        else {
                            uint8_t buffer[2];
                            if (!ok_read(decoder, buffer, 2)) {
                                return false;
                            }
                            if (!(buffer[0] == 0xff && buffer[1] == 0xD0 + next_restart)) {
                                ok_image_error(image, "Invalid restart marker (2)");
                                return false;
                            }
                        }
                        next_restart = (next_restart + 1) & 7;
                        
                        decode_restart(decoder);
                    }
                }
            }
        }
    }
    decoder->complete = true;
    dump_bits(decoder);
    return true;
}

//
// Segment reading
//

#define intDivCeil(x, y) (((x) + (y) - 1) / (y))

static bool read_sof(jpg_decoder *decoder) {
    ok_image *image = decoder->image;
    uint8_t buffer[3*3];
    if (!ok_read(decoder, buffer, 8)) {
        return false;
    }
    int length = readBE16(buffer) - 8;
    int P = buffer[2];
    if (P != 8) {
        ok_image_error(image, "Invalid JPEG (component size=%i)", P);
        return false;
    }
    image->height = readBE16(buffer + 3);
    image->width = readBE16(buffer + 5);
    if (image->width == 0 || image->height == 0) {
        // height == 0 is supposed to be legal?
        ok_image_error(image, "Invalid JPEG dimensions %ix%i", image->width, image->height);
        return false;
    }
    decoder->num_components = buffer[7];
    if (decoder->num_components != 1 && decoder->num_components != 3) {
        ok_image_error(image, "Invalid JPEG (num_components=%i)", decoder->num_components);
        return false;
    }
    
    if (length < 3 * decoder->num_components) {
        ok_image_error(image, "Invalid JPEG (SOF segment too short)");
        return false;
    }
    if (!ok_read(decoder, buffer, 3 * decoder->num_components)) {
        return false;
    }
    
    int maxH = 1;
    int maxV = 1;
    for (int i = 0; i < decoder->num_components; i++) {
        component *c = decoder->components + i;
        c->id = buffer[i*3+0];
        c->H = buffer[i*3+1] >> 4;
        c->V = buffer[i*3+1] & 0x0F;
        c->Tq = buffer[i*3+2];
        
        if (c->H == 0 || c->V == 0 || c->H > 4 || c->V > 4 || c->Tq > 3) {
            ok_image_error(image, "Invalid JPEG (Bad component)");
            return false;
        }
        
        maxH = max(maxH, c->H);
        maxV = max(maxV, c->V);
        length -= 3;
    }
    
    // Skip remaining length, if any
    if (length > 0) {
        if (!ok_seek(decoder, length)) {
            return false;
        }
    }
    
    // Allocate data
    if (!decoder->info_only) {
        if (image->data != NULL) {
            ok_image_error(image, "Invalid JPEG (Multiple SOF markers)");
            return false;
        }
        
        decoder->data_units_x = intDivCeil(image->width, maxH * 8);
        decoder->data_units_y = intDivCeil(image->height, maxV * 8);

        for (int i = 0; i < decoder->num_components; i++) {
            component *c = decoder->components + i;
            c->upsample_h = maxH / c->H;
            c->upsample_v = maxV / c->V;
            c->width = intDivCeil(image->width * c->H, maxH);
            c->height = intDivCeil(image->height * c->V, maxV);
            c->stride = decoder->data_units_x * c->H * 8;
            uint32_t data_height = decoder->data_units_y * c->V * 8;
            
            uint64_t size = (uint64_t)c->stride * data_height;
            size_t platform_size = (size_t)size;
            if (platform_size == size) {
                c->data = malloc(platform_size);
            }
            if (c->data == NULL) {
                ok_image_error(image, "Couldn't allocate memory for %u x %u component", c->stride, data_height);
                return false;
            }
        }
        
        // Setup row conversion
        component *c = decoder->components;
        if (decoder->num_components == 1) {
            decoder->convert_row = convert_row_grayscale;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1 &&
                 image->width <= 4) {
            decoder->convert_row = convert_row_generic;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1 &&
                 c[1].upsample_h == 1 && c[1].upsample_v == 1 &&
                 c[2].upsample_h == 1 && c[2].upsample_v == 1) {
            decoder->convert_row = convert_row_h1v1;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1 &&
                 c[1].upsample_h == 2 && c[1].upsample_v == 1 &&
                 c[2].upsample_h == 2 && c[2].upsample_v == 1) {
            decoder->convert_row = convert_row_h2v1;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1 &&
                 c[1].upsample_h == 1 && c[1].upsample_v == 2 &&
                 c[2].upsample_h == 1 && c[2].upsample_v == 2) {
            decoder->convert_row = convert_row_h1v2;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1 &&
                 c[1].upsample_h == 2 && c[1].upsample_v == 2 &&
                 c[2].upsample_h == 2 && c[2].upsample_v == 2) {
            decoder->convert_row = convert_row_h2v2;
        }
        else if (c[0].upsample_h == 1 && c[0].upsample_v == 1) {
            decoder->convert_row = convert_row_generic;
        }
        else {
            // Shoudn't happen with grayscale or RGB images
            ok_image_error(image, "Can't upsample image (%ix%i, %ix%i, %ix%i)", c[0].upsample_h, c[0].upsample_v,
                           c[1].upsample_h, c[1].upsample_v, c[2].upsample_h, c[2].upsample_v);
            return false;
        }

        // Allocate dest data
        uint64_t size = (uint64_t)image->width * image->height * 4;
        size_t platform_size = (size_t)size;
        if (platform_size == size) {
            image->data = malloc(platform_size);
        }
        if (image->data == NULL) {
            ok_image_error(image, "Couldn't allocate memory for %u x %u image", image->width, image->height);
            return false;
        }
    }
    return true;
}

static bool read_dqt(jpg_decoder *decoder) {
    ok_image *image = decoder->image;
    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    while (length >= 65) {
        if (!ok_read(decoder, buffer, 1)) {
            return false;
        }

        int Pq = buffer[0] >> 4;
        int Tq = buffer[0] & 0x0f;
        
        if (Pq != 0) {
            ok_image_error(image, "Unsupported JPEG (extended)");
            return false;
        }
        if (Tq > 3) {
            ok_image_error(image, "Invalid JPEG (Tq)");
            return false;
        }
        if (!ok_read(decoder, decoder->q_table[Tq], 64)) {
            return false;
        }
        length -= 65;
    }
    if (length != 0) {
        ok_image_error(image, "Invalid DQT segment length");
        return false;
    }
    else {
        return true;
    }
}

static bool read_dri(jpg_decoder *decoder) {
    uint8_t buffer[4];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    if (length != 2) {
        ok_image_error(decoder->image, "Invalid DRI segment length");
        return false;
    }
    else {
        decoder->restart_intervals = readBE16(buffer + 2);
        return true;
    }
}

static bool read_dht(jpg_decoder *decoder) {
    ok_image *image = decoder->image;
    uint8_t buffer[17];
    if (!ok_read(decoder, buffer, 2)) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    while (length >= 17) {
        if (!ok_read(decoder, buffer, 17)) {
            return false;
        }
        length -= 17;
        
        int Tc = buffer[0] >> 4;
        int Th = buffer[0] & 0x0f;
        if (Tc > 1 || Th > 3) {
            ok_image_error(image, "Invalid JPEG (Bad DHT Tc/Th)");
            return false;
        }
        huffman_table *table = decoder->huffman_tables[Tc] + Th;
        generate_huffman_table(table, buffer);
        if (table->count > 0) {
            if (table->count > 256 || table->count > length) {
                ok_image_error(image, "Invalid DHT segment length");
                return false;
            }
            if (!ok_read(decoder, table->val, table->count)) {
                return false;
            }
            length -= table->count;
        }
        generate_huffman_table_lookups(table);
    }
    if (length != 0) {
        ok_image_error(image, "Invalid DHT segment length");
        return false;
    }
    else {
        return true;
    }
}

static bool read_sos(jpg_decoder *decoder) {
    ok_image *image = decoder->image;
    int expected_size = 6 + decoder->num_components * 2;
    uint8_t buffer[expected_size];
    if (!ok_read(decoder, buffer, expected_size)) {
        return false;
    }
    int length = readBE16(buffer);
    if (length != expected_size) {
        ok_image_error(image, "Invalid SOS segment (L)");
        return false;
    }
    if (buffer[2] != decoder->num_components) {
        ok_image_error(image, "Invalid SOS segment (Ns)");
        return false;
    }
    
    uint8_t *src = buffer + 3;
    for (int i = 0; i < decoder->num_components; i++, src+=2) {
        component *comp = decoder->components + i;
        int C = src[0];
        if (C != comp->id) {
            ok_image_error(image, "Invalid SOS segment (C)");
            return false;
        }
        
        comp->Td = src[1] >> 4;
        comp->Ta = src[1] & 0x0f;
        if (comp->Td > 3 || comp->Ta > 3) {
            ok_image_error(image, "Invalid SOS segment (Td/Ta)");
            return false;
        }
    }
    
    if (src[0] != 0 || src[1] != 63 || src[2] != 0) {
        ok_image_error(image, "Invalid SOS segment (Ss/Se/Ah/Al)");
        return false;
    }
    
    return decode_scan(decoder);
}

static bool skip_segment(jpg_decoder *decoder) {
    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    if (!ok_seek(decoder, length)) {
        return false;
    }
    return true;
}

//
// JPEG decoding entry point
//

static void decode_jpg2(jpg_decoder *decoder) {
    ok_image *image = decoder->image;

    // Read header
    uint8_t jpg_header[2];
    if (!ok_read(decoder, jpg_header, 2)) {
        return;
    }
    if (jpg_header[0] != 0xFF || jpg_header[1] != 0xD8) {
        ok_image_error(image, "Invalid signature (not a JPG file)");
        return;
    }
    
    while (!decoder->eoi_found) {
        // Read marker
        uint8_t buffer[2];
        int marker;
        if (decoder->next_marker != 0) {
            marker = decoder->next_marker;
            decoder->next_marker = 0;
        }
        else {
            if (!ok_read(decoder, buffer, 2)) {
                return;
            }
            if (buffer[0] != 0xFF) {
                ok_image_error(image, "Invalid JPG marker 0x%02X%02X", buffer[0], buffer[1]);
                return;
            }
            marker = buffer[1];
        }
        
        bool success = true;
        if (marker == 0xC0) {
            // SOF
            success = read_sof(decoder);
            if (success && decoder->info_only) {
                return;
            }
        }
        else if (marker == 0xC4) {
            // DHT
            success = decoder->info_only ? skip_segment(decoder) : read_dht(decoder);
        }
        else if (marker == 0xD9) {
            // EOI
            decoder->eoi_found = true;
        }
        else if (marker == 0xDA) {
            // SOS
            if (!decoder->info_only) {
                success = read_sos(decoder);
            }
            else {
                success = skip_segment(decoder);
                if (success) {
                    // Scan to next marker
                    while (true) {
                        if (!ok_read(decoder, buffer, 1)) {
                            return;
                        }
                        if (buffer[0] == 0xff) {
                            if (!ok_read(decoder, buffer, 1)) {
                                return;
                            }
                            if (buffer[0] != 0) {
                                decoder->next_marker = buffer[0];
                                break;
                            }
                        }
                    }
                }
            }
        }
        else if (marker == 0xDB) {
            // DQT
            success = decoder->info_only ? skip_segment(decoder) : read_dqt(decoder);
        }
        else if (marker == 0xDD) {
            // DRI
            success = read_dri(decoder);
        }
        else if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
            // APP or Comment
            success = skip_segment(decoder);
        }
        else if ((marker & 0xF0) == 0xC0) {
            ok_image_error(image, "Unsupported JPEG (marker 0xFF%02X) - progressive, extended, or lossless", marker);
            success = false;
        }
        else {
            ok_image_error(image, "Unsupported or corrupt JPEG (marker 0xFF%02X)", marker);
            success = false;
        }
        
        if (!success) {
            return;
        }
    }

    if (!decoder->complete) {
        ok_image_error(image, "Incomplete image data");
        return;
    }
    
    // Upsample and color convert
    for (uint32_t y = 0; y < image->height; y++) {
        const uint32_t stride = image->width * 4;
        uint8_t *row;
        if (decoder->flip_y) {
            row = image->data + ((image->height - y - 1) * stride);
        }
        else {
            row = image->data + (y * stride);
        }
        
        component *c = decoder->components;
        uint32_t y1 = y;
        uint32_t y2 = y;
        
        if ((c+1)->upsample_v == 2 && y > 0) {
            y1 = y/2;
            y2 = (y + 1) / 2 - ((y + 1) & 1);
            y2 = min(y2, (c+1)->height - 1);
        }
        const uint8_t *Y = c->data + y * c->stride;
        const uint8_t *Cb = (c+1)->data + y1 * (c+1)->stride;
        const uint8_t *Cr = (c+2)->data + y1 * (c+2)->stride;
        const uint8_t *Cb_minor = (c+1)->data + y2 * (c+1)->stride;
        const uint8_t *Cr_minor = (c+2)->data + y2 * (c+2)->stride;
        
        if (decoder->color_format == OK_COLOR_FORMAT_RGBA || decoder->color_format == OK_COLOR_FORMAT_RGBA_PRE) {
            decoder->convert_row((c+1)->width, image->width, Y, Cb, Cr, Cb_minor, Cr_minor, row, row+1, row+2, row+3);
        }
        else {
            decoder->convert_row((c+1)->width, image->width, Y, Cb, Cr, Cb_minor, Cr_minor, row+2, row+1, row, row+3);
        }
    }
}

static ok_image *decode_jpg(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                            const ok_color_format color_format, const bool flip_y, const bool info_only) {
    ok_image *image = calloc(1, sizeof(ok_image));
    if (image == NULL) {
        return NULL;
    }
    if (read_func == NULL || seek_func == NULL) {
        ok_image_error(image, "Invalid argument: read_func or seek_func is NULL");
        return image;
    }

    jpg_decoder *decoder = calloc(1, sizeof(jpg_decoder));
    if (decoder == NULL) {
        ok_image_error(image, "Couldn't allocate decoder.");
        return image;
    }
    
    decoder->image = image;
    decoder->reader_data = user_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;
    decoder->color_format = color_format;
    decoder->flip_y = flip_y;
    decoder->info_only = info_only;
    
    decode_jpg2(decoder);
    
    // Cleanup
    for (int i = 0; i < 3; i++) {
        component *c = decoder->components + i;
        if (c->data != NULL) {
            free(c->data);
            c->data = NULL;
        }
    }
    free(decoder);
    
    return image;
}
