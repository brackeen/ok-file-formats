/*
 + JPEG spec
   http://www.w3.org/Graphics/JPEG/itu-t81.pdf
   http://www.w3.org/Graphics/JPEG/jfif3.pdf
   http://www.fifi.org/doc/jhead/exif-e.html
 + Another easy-to-read JPEG decoder (written in python)
   https://github.com/enmasse/jpeg_read/blob/master/jpeg_read.py
 */

#include "ok_jpg.h"
#include <memory.h>
#include <stdarg.h>
#include <stdio.h> // For vsnprintf
#include <stdlib.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// JPEG spec allows up to 4, but values greater than 2 are rare. The IDCT functions here only
// support up to 2.
#define MAX_SAMPLING_FACTOR 2
#define C_WIDTH (MAX_SAMPLING_FACTOR * 8)

typedef void (*idct_func)(const int *const input, uint8_t *output);

typedef struct {
    uint8_t id;
    uint8_t H;
    uint8_t V;
    uint8_t Tq;
    uint8_t Td;
    uint8_t Ta;
    uint8_t output[C_WIDTH * C_WIDTH];
    int pred;
    idct_func idct;
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
    ok_jpg *jpg;

    // Decode options
    ok_jpg_color_format color_format;
    bool flip_x;
    bool flip_y;
    bool rotate;
    bool info_only;

    // Input
    void *input_data;
    ok_jpg_input_func input_func;

    // State
    bool eoi_found;
    bool complete;
    int next_marker;
    int restart_intervals;
    int restart_intervals_remaining;
    uint32_t input_buffer;
    int input_buffer_bits;

    // JPEG data
    uint16_t in_width;
    uint16_t in_height;
    int data_units_x;
    int data_units_y;
    int num_components;
    component components[3];
    uint8_t q_table[4][8 * 8];

    // 0 = DC table, 1 = AC table
    huffman_table huffman_tables[2][4];
} jpg_decoder;

static void ok_jpg_error(ok_jpg *jpg, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

static void ok_jpg_error(ok_jpg *jpg, const char *format, ...) {
    if (jpg) {
        jpg->width = 0;
        jpg->height = 0;
        if (jpg->data) {
            free(jpg->data);
            jpg->data = NULL;
        }
        if (format) {
            va_list args;
            va_start(args, format);
            vsnprintf(jpg->error_message, sizeof(jpg->error_message), format, args);
            va_end(args);
        }
    }
}

static bool ok_read(jpg_decoder *decoder, uint8_t *data, const int length) {
    if (decoder->input_func(decoder->input_data, data, length) == length) {
        return true;
    } else {
        ok_jpg_error(decoder->jpg, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(jpg_decoder *decoder, const int length) {
    return ok_read(decoder, NULL, length);
}

static ok_jpg *decode_jpg(void *user_data, ok_jpg_input_func input_func,
                          const ok_jpg_color_format color_format, const bool flip_y,
                          const bool info_only);

// MARK: Public API

ok_jpg *ok_jpg_read_info(void *user_data, ok_jpg_input_func input_func) {
    return decode_jpg(user_data, input_func, OK_JPG_COLOR_FORMAT_RGBA, false, true);
}

ok_jpg *ok_jpg_read(void *user_data, ok_jpg_input_func input_func,
                    const ok_jpg_color_format color_format, const bool flip_y) {
    return decode_jpg(user_data, input_func, color_format, flip_y, false);
}

void ok_jpg_free(ok_jpg *jpg) {
    if (jpg) {
        if (jpg->data) {
            free(jpg->data);
        }
        free(jpg);
    }
}

// MARK: JPEG bit reading

static inline uint16_t readBE16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

static inline uint32_t readBE32(const uint8_t *data) {
    return (uint32_t)((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3]);
}

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (uint32_t)((data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0]);
}

// Load bits without reading them
static inline bool load_bits(jpg_decoder *decoder, const int num_bits) {
    // Needs optimization? Buffer the data instead of calling ok_read for every byte
    while (decoder->input_buffer_bits < num_bits) {
        if (decoder->next_marker != 0) {
            decoder->input_buffer <<= 8;
            decoder->input_buffer_bits += 8;
        } else {
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

// Assumes at least 8 bits of data was previously loaded in load_bits
static inline int peek_byte(jpg_decoder *decoder) {
    return (decoder->input_buffer >> (decoder->input_buffer_bits - 8)) & 0xff;
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
    return (int)(decoder->input_buffer >> decoder->input_buffer_bits) & ((1 << num_bits) - 1);
}

// MARK: Huffman decoding

static void generate_huffman_table(huffman_table *huff, const uint8_t *bits) {
    // JPEG spec: "Generate_size_table"
    int k = 0;
    for (uint8_t i = 1; i <= 16; i++) {
        for (int j = 1; j <= bits[i]; j++) {
            huff->size[k++] = i;
        }
    }
    huff->size[k] = 0;
    huff->count = k;

    // JPEG spec: "Generate_code_table"
    k = 0;
    uint16_t code = 0;
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
        } else {
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
        for (uint8_t i = 0; i < 8; i++) {
            uint8_t num_bits = i + 1;
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
    int code = peek_byte(decoder);
    int num_bits = table->lookup_num_bits[code];
    if (num_bits != 0) {
        consume_bits(decoder, num_bits);
        return table->lookup_val[code];
    }

    // Next, try a code up to 16-bits
    // Needs optimization for codes > 8 bits?
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
    ok_jpg_error(decoder->jpg, "Invalid huffman code");
    return -1;
}

// MARK: JPEG color conversion

static inline uint8_t clip_uint8(const int x) {
    return ((unsigned int)x) < 0xff ? (uint8_t)x : (x < 0 ? 0 : 0xff);
}

static inline uint8_t clip_fp_uint8(const int fx) {
    return ((unsigned int)fx) < 0xff0000 ? (uint8_t)(fx >> 16) : (fx < 0 ? 0 : 0xff);
}

static inline void convert_YCbCr_to_RGB(uint8_t Y, uint8_t Cb, uint8_t Cr,
                                        uint8_t *r, uint8_t *g, uint8_t *b) {
    // From the JFIF spec. Converted to 16:16 fixed point.
    static const int fx1 = 91881; // 1.402
    static const int fx2 = -22553; // 0.34414
    static const int fx3 = -46802; // 0.71414
    static const int fx4 = 116130; // 1.772

    const int fy = (Y << 16) + (1 << 15);
    const int fr = fy + fx1 * (Cr - 128);
    const int fg = fy + fx2 * (Cb - 128) + fx3 * (Cr - 128);
    const int fb = fy + fx4 * (Cb - 128);
    *r = clip_fp_uint8(fr);
    *g = clip_fp_uint8(fg);
    *b = clip_fp_uint8(fb);
}

// Convert from grayscale to RGBA
static void convert_data_unit_grayscale(const uint8_t *y,
                                        uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a,
                                        const int x_inc, const int y_inc,
                                        const int max_width, const int max_height) {
    const int in_row_offfset = C_WIDTH - max_width;
    const int out_row_offset = y_inc - x_inc * max_width;

    for (int v = 0; v < max_height; v++) {
        const uint8_t *y_end = y + max_width;
        while (y < y_end) {
            *r = *g = *b = *y;
            *a = 0xff;
            r += x_inc;
            g += x_inc;
            b += x_inc;
            a += x_inc;
            y++;
        }
        y += in_row_offfset;
        r += out_row_offset;
        g += out_row_offset;
        b += out_row_offset;
        a += out_row_offset;
    }
}

// Convert from YCbCr to RGBA
static void convert_data_unit_color(const uint8_t *y, const uint8_t *cb, const uint8_t *cr,
                                    uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a,
                                    const int x_inc, const int y_inc,
                                    const int max_width, const int max_height) {
    const int in_row_offfset = C_WIDTH - max_width;
    const int out_row_offset = y_inc - x_inc * max_width;

    for (int v = 0; v < max_height; v++) {
        const uint8_t *y_end = y + max_width;
        while (y < y_end) {
            convert_YCbCr_to_RGB(*y, *cb, *cr, r, g, b);
            *a = 0xff;
            r += x_inc;
            g += x_inc;
            b += x_inc;
            a += x_inc;
            y++;
            cb++;
            cr++;
        }
        y += in_row_offfset;
        cb += in_row_offfset;
        cr += in_row_offfset;
        r += out_row_offset;
        g += out_row_offset;
        b += out_row_offset;
        a += out_row_offset;
    }
}

static void convert_data_unit(jpg_decoder *decoder, const int data_unit_x, const int data_unit_y) {
    ok_jpg *jpg = decoder->jpg;
    component *c = decoder->components;
    int x = data_unit_x * c->H * 8;
    int y = data_unit_y * c->V * 8;
    const int width = min(c->H * 8, decoder->in_width - x);
    const int height = min(c->V * 8, decoder->in_height - y);
    int x_inc = 4;
    int y_inc = (int)jpg->width * 4;
    uint8_t *data = jpg->data;
    if (decoder->rotate) {
        int temp = x;
        x = y;
        y = temp;
    }
    if (decoder->flip_x) {
        data += y_inc - (x + 1) * x_inc;
        x_inc = -x_inc;
    } else {
        data += x * x_inc;
    }
    if (decoder->flip_y) {
        data += (((int)jpg->height - y - 1) * y_inc);
        y_inc = -y_inc;
    } else {
        data += y * y_inc;
    }
    if (decoder->rotate) {
        int temp = x_inc;
        x_inc = y_inc;
        y_inc = temp;
    }

    if (decoder->num_components == 1) {
        convert_data_unit_grayscale(c->output, data, data + 1, data + 2, data + 3,
                                    x_inc, y_inc, width, height);
    } else if (decoder->color_format == OK_JPG_COLOR_FORMAT_RGBA) {
        convert_data_unit_color(c->output, (c + 1)->output, (c + 2)->output,
                                data, data + 1, data + 2, data + 3,
                                x_inc, y_inc, width, height);
    } else {
        convert_data_unit_color(c->output, (c + 1)->output, (c + 2)->output,
                                data + 2, data + 1, data, data + 3,
                                x_inc, y_inc, width, height);
    }
}

// MARK: IDCT

// Output is scaled by (1 << 12) * sqrt(2) / (1 << out_shift)
static inline void idct_1d_8(int *out, const int out_shift,
                             const int v0, const int v1, const int v2, const int v3,
                             const int v4, const int v5, const int v6, const int v7) {
    // Constants scaled by (1 << 12).
    static const int c1 = 5681; // cos(1*pi/16) * sqrt(2)
    static const int c2 = 5352; // cos(2*pi/16) * sqrt(2)
    static const int c3 = 4816; // cos(3*pi/16) * sqrt(2)
    static const int c5 = 3218; // cos(5*pi/16) * sqrt(2)
    static const int c6 = 2217; // cos(6*pi/16) * sqrt(2)
    static const int c7 = 1130; // cos(7*pi/16) * sqrt(2)

    int t0, t1, t2;
    int p0, p1, p2, p3;

    t0 = (v0 << 12) + (1 << (out_shift - 1));
    t1 = (v4 << 12);
    p0 = p3 = t0 + t1;
    p1 = p2 = t0 - t1;

    // Quick check to avoid mults
    if (v1 == 0 && v2 == 0 && v3 == 0 &&
        v5 == 0 && v6 == 0 && v7 == 0) {
        p0 >>= out_shift;
        p1 >>= out_shift;
        out[0] = p0;
        out[1] = p1;
        out[2] = p1;
        out[3] = p0;
        out[4] = p0;
        out[5] = p1;
        out[6] = p1;
        out[7] = p0;
    } else {
        // Even part: 3 mults
        t0 = (v0 << 12) + (1 << (out_shift - 1));
        t1 = (v4 << 12);
        p0 = p3 = t0 + t1;
        p1 = p2 = t0 - t1;

        t0 = (v2 + v6) * c6;
        t1 = t0 + v2 * (c2 - c6);
        p0 += t1;
        p3 -= t1;

        t1 = t0 - v6 * (c2 + c6);
        p1 += t1;
        p2 -= t1;

        // Odd part: 9 mults
        t1 = (v1 + v3 + v5 + v7) * c3;
        t0 = t1 + (v1 + v5) * (-c3 + c5);
        t1 = t1 + (v3 + v7) * (-c3 - c5);

        t2 = (v1 + v7) * (-c3 + c7);
        const int q0 = t0 + t2 + v1 * (c1 + c3 - c5 - c7);
        const int q3 = t1 + t2 + v7 * (-c1 + c3 + c5 - c7);

        t2 = (v3 + v5) * (-c3 - c1);
        const int q1 = t1 + t2 + v3 * (c1 + c3 + c5 - c7);
        const int q2 = t0 + t2 + v5 * (c1 + c3 - c5 + c7);

        // Output
        out[0] = (p0 + q0) >> out_shift;
        out[1] = (p1 + q1) >> out_shift;
        out[2] = (p2 + q2) >> out_shift;
        out[3] = (p3 + q3) >> out_shift;
        out[4] = (p3 - q3) >> out_shift;
        out[5] = (p2 - q2) >> out_shift;
        out[6] = (p1 - q1) >> out_shift;
        out[7] = (p0 - q0) >> out_shift;
    }
}

// Output is scaled by (1 << 12) * sqrt(2) / (1 << out_shift)
static inline void idct_1d_16(int *out, const int out_shift,
                              const int v0, const int v1, const int v2, const int v3,
                              const int v4, const int v5, const int v6, const int v7) {
    // Constants scaled by (1 << 12).
    static const int c1 = 5765; // cos( 1*pi/32) * sqrt(2)
    static const int c2 = 5681; // cos( 2*pi/32) * sqrt(2)
    static const int c3 = 5543; // cos( 3*pi/32) * sqrt(2)
    static const int c4 = 5352; // cos( 4*pi/32) * sqrt(2)
    static const int c5 = 5109; // cos( 5*pi/32) * sqrt(2)
    static const int c6 = 4816; // cos( 6*pi/32) * sqrt(2)
    static const int c7 = 4478; // cos( 7*pi/32) * sqrt(2)
    static const int c9 = 3675; // cos( 9*pi/32) * sqrt(2)
    static const int c10 = 3218; // cos(10*pi/32) * sqrt(2)
    static const int c11 = 2731; // cos(11*pi/32) * sqrt(2)
    static const int c12 = 2217; // cos(12*pi/32) * sqrt(2)
    static const int c13 = 1682; // cos(13*pi/32) * sqrt(2)
    static const int c14 = 1130; // cos(14*pi/32) * sqrt(2)
    static const int c15 = 568; // cos(15*pi/32) * sqrt(2)

    int t0, t1, t2;
    int p0, p1, p2, p3, p4, p5, p6, p7;

    t0 = (v0 << 12) + (1 << (out_shift - 1));

    // Quick check to avoid mults
    if (v1 == 0 && v2 == 0 && v3 == 0 && v4 == 0 &&
        v5 == 0 && v6 == 0 && v7 == 0) {
        t0 >>= out_shift;
        for (int i = 0; i < 16; i++) {
            *out++ = t0;
        }
    } else {
        // Even part: 8 mults
        t1 = v4 * c4;
        p0 = p7 = t0 + t1;
        p3 = p4 = t0 - t1;

        t1 = v4 * c12;
        p1 = p6 = t0 + t1;
        p2 = p5 = t0 - t1;

        t0 = (v2 + v6) * c6;
        t1 = t0 + v2 * (c2 - c6);
        p0 += t1;
        p7 -= t1;

        t1 = t0 + v6 * (-c6 - c14);
        p1 += t1;
        p6 -= t1;

        t0 = (v2 - v6) * c10;
        t1 = t0 + v6 * (c10 - c2);
        p2 += t1;
        p5 -= t1;

        t1 = t0 + v2 * (c14 - c10);
        p3 += t1;
        p4 -= t1;

        // Odd part: 21 mults
        t1 = (v1 + v3 + v5 - v7) * c9;
        t0 = t1 + (v1 + v5) * (c15 - c9);
        t1 = t1 + (-v3 + v7) * (c1 + c9);

        t2 = (v1 - v7) * (c11 - c9);
        const int q1 = t0 + t2 + v1 * (c3 + c9 - c11 - c15);
        const int q5 = t1 + t2 + v7 * (-c1 - c9 + c11 + c13);

        t2 = (-v3 - v5) * (c13 + c9);
        const int q4 = t1 + t2 + v3 * (c1 - c5 + c9 + c13);
        const int q7 = t0 + t2 + v5 * (c9 + c11 + c13 - c15);

        t0 = (v1 - v3 - v5 + v7) * c7;
        t1 = (v3 - v7) * (c3 + c7);
        t2 = (v5 - v7) * (c5 + c7);
        const int q0 = t0 + t1 + t2 + v1 * (c1 - c7) + v7 * (c5 + c7 + c3 + c7);
        const int q2 = t0 + t1 + v1 * (c5 - c7) + v3 * (c15 - c3);
        const int q3 = t0 + v3 * (-c11 + c7) + v5 * (-c3 + c7) + v7 * (c15 - c7);
        const int q6 = t0 + t2 + v1 * (c13 - c7) + v5 * (c1 - c5);

        // Output
        out[0] = (p0 + q0) >> out_shift;
        out[1] = (p1 + q1) >> out_shift;
        out[2] = (p2 + q2) >> out_shift;
        out[3] = (p3 + q3) >> out_shift;
        out[4] = (p4 + q4) >> out_shift;
        out[5] = (p5 + q5) >> out_shift;
        out[6] = (p6 + q6) >> out_shift;
        out[7] = (p7 + q7) >> out_shift;
        out[8] = (p7 - q7) >> out_shift;
        out[9] = (p6 - q6) >> out_shift;
        out[10] = (p5 - q5) >> out_shift;
        out[11] = (p4 - q4) >> out_shift;
        out[12] = (p3 - q3) >> out_shift;
        out[13] = (p2 - q2) >> out_shift;
        out[14] = (p1 - q1) >> out_shift;
        out[15] = (p0 - q0) >> out_shift;
    }
}

// From JPEG spec, "A.3.3"
// IDCT a 8x8 block in a array of size (C_WIDTH x C_WIDTH)
// w and h must be either 8 or 16
//
// 1D Inverse Discrete Cosine Transform
// The 1D versions (idct_1d_8 and idct_1d_16) were created by first creating a naive implementation,
// and then unrolling loops and optimizing by hand.
// Once loops were unrolled, redundant computations were obvious, and they could be eliminated.
// 1. Converted to integer (fixed-point)
// 2. Scaled output by sqrt(2).
static void idct_wxh(const int *const input, uint8_t *output, const int w, const int h) {
    int temp[16 * h];
    int temp_row[w];
    const int *in = input;
    int *out = temp;

    // idct_1d scales output by ((1 << 12) * sqrt(2)).
    // Shift-right by 8 so output is scaled ((1 << 4) * sqrt(2)).
    if (h == 8) {
        for (int u = 0; u < 8; u++) {
            idct_1d_8(out, 8,
                      in[0 * 8], in[1 * 8], in[2 * 8], in[3 * 8],
                      in[4 * 8], in[5 * 8], in[6 * 8], in[7 * 8]);
            in++;
            out += 16;
        }
    } else { // h == 16
        for (int u = 0; u < 8; u++) {
            idct_1d_16(out, 8,
                       in[0 * 8], in[1 * 8], in[2 * 8], in[3 * 8],
                       in[4 * 8], in[5 * 8], in[6 * 8], in[7 * 8]);
            in++;
            out += 16;
        }
    }

    // Input is scaled by ((1 << 4) * sqrt(2)).
    // idct_1d scales output by ((1 << 12) * sqrt(2)), for a total output scale of (1 << 17).
    // Shift by 19 to get rid of the scale and to divide by 4 at the same time.
    // (Divide by 4 per the IDCT formula, JPEG spec section A.3.3)
    in = temp;
    if (w == 8) {
        for (int y = 0; y < h; y++) {
            idct_1d_8(temp_row, 19,
                      in[0 * 16], in[1 * 16], in[2 * 16], in[3 * 16],
                      in[4 * 16], in[5 * 16], in[6 * 16], in[7 * 16]);
            for (int x = 0; x < 8; x++) {
                output[x] = clip_uint8(temp_row[x] + 128);
            }
            in++;
            output += C_WIDTH;
        }
    } else { // w == 16
        for (int y = 0; y < h; y++) {
            idct_1d_16(temp_row, 19,
                       in[0 * 16], in[1 * 16], in[2 * 16], in[3 * 16],
                       in[4 * 16], in[5 * 16], in[6 * 16], in[7 * 16]);
            for (int x = 0; x < 16; x++) {
                output[x] = clip_uint8(temp_row[x] + 128);
            }
            in++;
            output += C_WIDTH;
        }
    }
}

// IDCT a 8x8 input block to 8x8 in an output of size (C_WIDTH x C_WIDTH)
static void idct_8x8(const int *const input, uint8_t *output) {
    idct_wxh(input, output, 8, 8);
}

// IDCT a 8x8 block to 8x16 in an output of size (C_WIDTH x C_WIDTH)
static void idct_8x16(const int *const input, uint8_t *output) {
    idct_wxh(input, output, 8, 16);
}

// IDCT a 8x8 block to 16x8 in an output of size (C_WIDTH x C_WIDTH)
static void idct_16x8(const int *const input, uint8_t *output) {
    idct_wxh(input, output, 16, 8);
}

// IDCT a 8x8 block to 16x16 in an output of size (C_WIDTH x C_WIDTH)
static void idct_16x16(const int *const input, uint8_t *output) {
    idct_wxh(input, output, 16, 16);
}

// MARK: Entropy decoding

// clang-format off
static const uint8_t zig_zag[] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};
// clang-format on

static inline int extend(const int v, const int t) {
    // Figure F.12
    if (v < (1 << (t - 1))) {
        return v + ((-1) << t) + 1;
    } else {
        return v;
    }
}

static bool decode_data_unit(jpg_decoder *decoder, component *c, uint8_t *out) {
    int block[8 * 8];
    memset(block, 0, sizeof(block));

    // Decode DC coefficients - F.2.2.1
    const uint8_t *q_table = decoder->q_table[c->Tq];
    huffman_table *dc = decoder->huffman_tables[0] + c->Td;
    int t = huffman_decode(decoder, dc);
    if (t < 0) {
        return false;
    }
    if (t > 0) {
        int diff = load_next_bits(decoder, t);
        if (diff < 0) {
            return false;
        }
        diff = extend(diff, t);
        c->pred += diff;
    }

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
        } else {
            k += r;
            if (k > 63) {
                ok_jpg_error(decoder->jpg, "Invalid block index");
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

    c->idct(block, out);
    return true;
}

static void decode_restart(jpg_decoder *decoder) {
    decoder->restart_intervals_remaining = decoder->restart_intervals;
    for (int i = 0; i < decoder->num_components; i++) {
        decoder->components[i].pred = 0;
    }
}

static bool decode_scan(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;
    int next_restart = 0;
    decode_restart(decoder);
    for (int data_unit_y = 0; data_unit_y < decoder->data_units_y; data_unit_y++) {
        for (int data_unit_x = 0; data_unit_x < decoder->data_units_x; data_unit_x++) {
            for (int i = 0; i < decoder->num_components; i++) {
                component *c = decoder->components + i;
                int offset_y = 0;
                for (int y = 0; y < c->V; y++) {
                    int offset_x = 0;
                    for (int x = 0; x < c->H; x++) {
                        if (!decode_data_unit(decoder, c, c->output + offset_x + offset_y)) {
                            return false;
                        }
                        offset_x += 8;
                    }
                    offset_y += C_WIDTH * 8;
                }
            }

            convert_data_unit(decoder, data_unit_x, data_unit_y);

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
                            } else {
                                ok_jpg_error(jpg, "Invalid restart marker (1)");
                                return false;
                            }
                        } else {
                            uint8_t buffer[2];
                            if (!ok_read(decoder, buffer, 2)) {
                                return false;
                            }
                            if (!(buffer[0] == 0xff && buffer[1] == 0xD0 + next_restart)) {
                                ok_jpg_error(jpg, "Invalid restart marker (2)");
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

// MARK: Segment reading

#define intDivCeil(x, y) (((x) + (y)-1) / (y))

static bool read_sof(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;
    uint8_t buffer[3 * 3];
    if (!ok_read(decoder, buffer, 8)) {
        return false;
    }
    int length = readBE16(buffer) - 8;
    int P = buffer[2];
    if (P != 8) {
        ok_jpg_error(jpg, "Invalid JPEG (component size=%i)", P);
        return false;
    }
    decoder->in_height = readBE16(buffer + 3);
    decoder->in_width = readBE16(buffer + 5);
    if (decoder->in_width == 0 || decoder->in_height == 0) {
        ok_jpg_error(jpg, "Invalid JPEG dimensions %ix%i", decoder->in_width, decoder->in_height);
        return false;
    }
    jpg->width = decoder->rotate ? decoder->in_height : decoder->in_width;
    jpg->height = decoder->rotate ? decoder->in_width : decoder->in_height;
    decoder->num_components = buffer[7];
    if (decoder->num_components != 1 && decoder->num_components != 3) {
        ok_jpg_error(jpg, "Invalid JPEG (num_components=%i)", decoder->num_components);
        return false;
    }

    if (length < 3 * decoder->num_components) {
        ok_jpg_error(jpg, "Invalid JPEG (SOF segment too short)");
        return false;
    }
    if (!ok_read(decoder, buffer, 3 * decoder->num_components)) {
        return false;
    }

    int maxH = 1;
    int maxV = 1;
    for (int i = 0; i < decoder->num_components; i++) {
        component *c = decoder->components + i;
        c->id = buffer[i * 3 + 0];
        c->H = buffer[i * 3 + 1] >> 4;
        c->V = buffer[i * 3 + 1] & 0x0F;
        c->Tq = buffer[i * 3 + 2];

        if (c->H == 0 || c->V == 0 || c->H > 4 || c->V > 4 || c->Tq > 3) {
            ok_jpg_error(jpg, "Invalid JPEG (Bad component)");
            return false;
        }

        if (c->H > MAX_SAMPLING_FACTOR || c->V > MAX_SAMPLING_FACTOR) {
            ok_jpg_error(jpg, "Unsupported sampling factor: %ix%i", c->H, c->V);
            return false;
        }

        maxH = max(maxH, c->H);
        maxV = max(maxV, c->V);
        length -= 3;
    }
    decoder->data_units_x = intDivCeil(decoder->in_width, maxH * 8);
    decoder->data_units_y = intDivCeil(decoder->in_height, maxV * 8);

    // Skip remaining length, if any
    if (length > 0) {
        if (!ok_seek(decoder, length)) {
            return false;
        }
    }

    // Setup idct
    for (int i = 0; i < decoder->num_components; i++) {
        component *c = decoder->components + i;
        if (c->H == maxH && c->V == maxV) {
            c->idct = idct_8x8;
        } else if (c->H * 2 == maxH && c->V * 2 == maxV) {
            c->idct = idct_16x16;
        } else if (c->H == maxH && c->V * 2 == maxV) {
            c->idct = idct_8x16;
        } else if (c->H * 2 == maxH && c->V == maxV) {
            c->idct = idct_16x8;
        } else {
            ok_jpg_error(jpg, "Unsupported sampling factor: %ix%i in max %ix%i", c->H, c->V, maxH,
                         maxV);
            return false;
        }
    }

    // Allocate data
    if (!decoder->info_only) {
        if (jpg->data) {
            ok_jpg_error(jpg, "Invalid JPEG (Multiple SOF markers)");
            return false;
        }

        uint64_t size = (uint64_t)jpg->width * jpg->height * 4;
        size_t platform_size = (size_t)size;
        if (platform_size == size) {
            jpg->data = malloc(platform_size);
        }
        if (!jpg->data) {
            ok_jpg_error(jpg, "Couldn't allocate memory for %u x %u JPEG image",
                         jpg->width, jpg->height);
            return false;
        }
    }
    return true;
}

static bool read_exif(jpg_decoder *decoder) {
    static const char exif_magic[] = {'E', 'x', 'i', 'f', 0, 0};
    static const char tiff_magic_little_endian[] = {0x49, 0x49, 0x2a, 0x00};
    static const char tiff_magic_big_endian[] = {0x4d, 0x4d, 0x00, 0x2a};

    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;

    // Check for Exif header
    const int exif_header_length = 6;
    if (length < exif_header_length) {
        return ok_seek(decoder, length);
    }
    uint8_t exif_header[exif_header_length];

    if (!ok_read(decoder, exif_header, exif_header_length)) {
        return false;
    }
    length -= exif_header_length;
    if (memcmp(exif_header, exif_magic, exif_header_length) != 0) {
        return ok_seek(decoder, length);
    }

    // Check for TIFF header
    bool little_endian;
    const int tiff_header_length = 4;
    if (length < tiff_header_length) {
        return ok_seek(decoder, length);
    }
    uint8_t tiff_header[tiff_header_length];
    if (!ok_read(decoder, tiff_header, tiff_header_length)) {
        return false;
    }
    length -= tiff_header_length;
    if (memcmp(tiff_header, tiff_magic_little_endian, tiff_header_length) == 0) {
        little_endian = true;
    } else if (memcmp(tiff_header, tiff_magic_big_endian, tiff_header_length) == 0) {
        little_endian = false;
    } else {
        return ok_seek(decoder, length);
    }

    // Get start offset
    if (length < 4) {
        return ok_seek(decoder, length);
    }
    int32_t offset;
    uint8_t offset_buffer[4];
    if (!ok_read(decoder, offset_buffer, sizeof(offset_buffer))) {
        return false;
    }
    offset = (int32_t)(little_endian ? readLE32(offset_buffer) : readBE32(offset_buffer));
    length -= 4;
    offset -= 8; // Ignore tiff header, offset
    if (offset < 0 || offset > length) {
        return ok_seek(decoder, length);
    }
    if (!ok_seek(decoder, offset)) {
        return false;
    }
    length -= offset;

    // Get number of tags
    if (length < 2) {
        return ok_seek(decoder, length);
    }
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    length -= 2;
    int num_tags = little_endian ? readLE16(buffer) : readBE16(buffer);

    // Read tags, searching for orientation (0x112)
    const int tag_length = 12;
    uint8_t tag_buffer[tag_length];
    for (int i = 0; i < num_tags; i++) {
        if (length < tag_length) {
            return ok_seek(decoder, length);
        }
        if (!ok_read(decoder, tag_buffer, tag_length)) {
            return false;
        }
        length -= tag_length;

        int tag = little_endian ? readLE16(tag_buffer) : readBE16(tag_buffer);
        if (tag == 0x112) {
            int orientation = little_endian ? readLE16(tag_buffer + 8) : readBE16(tag_buffer + 8);
            switch (orientation) {
                case 1:
                default: // top-left
                    break;
                case 2: // top-right
                    decoder->flip_x = true;
                    break;
                case 3: // bottom-right
                    decoder->flip_x = true;
                    decoder->flip_y = !decoder->flip_y;
                    break;
                case 4: // bottom-left
                    decoder->flip_y = !decoder->flip_y;
                    break;
                case 5: // left-top
                    decoder->rotate = true;
                    break;
                case 6: // right-top
                    decoder->rotate = true;
                    decoder->flip_x = true;
                    break;
                case 7: // right-bottom
                    decoder->rotate = true;
                    decoder->flip_x = true;
                    decoder->flip_y = !decoder->flip_y;
                    break;
                case 8: // left-bottom
                    decoder->rotate = true;
                    decoder->flip_y = !decoder->flip_y;
                    break;
            }
            break;
        }
    }
    return ok_seek(decoder, length);
}

static bool read_dqt(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;
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
            ok_jpg_error(jpg, "Unsupported JPEG (extended)");
            return false;
        }
        if (Tq > 3) {
            ok_jpg_error(jpg, "Invalid JPEG (Tq)");
            return false;
        }
        if (!ok_read(decoder, decoder->q_table[Tq], 64)) {
            return false;
        }
        length -= 65;
    }
    if (length != 0) {
        ok_jpg_error(jpg, "Invalid DQT segment length");
        return false;
    } else {
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
        ok_jpg_error(decoder->jpg, "Invalid DRI segment length");
        return false;
    } else {
        decoder->restart_intervals = readBE16(buffer + 2);
        return true;
    }
}

static bool read_dht(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;
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
            ok_jpg_error(jpg, "Invalid JPEG (Bad DHT Tc/Th)");
            return false;
        }
        huffman_table *table = decoder->huffman_tables[Tc] + Th;
        generate_huffman_table(table, buffer);
        if (table->count > 0) {
            if (table->count > 256 || table->count > length) {
                ok_jpg_error(jpg, "Invalid DHT segment length");
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
        ok_jpg_error(jpg, "Invalid DHT segment length");
        return false;
    } else {
        return true;
    }
}

static bool read_sos(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;
    int expected_size = 6 + decoder->num_components * 2;
    uint8_t buffer[expected_size];
    if (!ok_read(decoder, buffer, expected_size)) {
        return false;
    }
    int length = readBE16(buffer);
    if (length != expected_size) {
        ok_jpg_error(jpg, "Invalid SOS segment (L)");
        return false;
    }
    if (buffer[2] != decoder->num_components) {
        ok_jpg_error(jpg, "Invalid SOS segment (Ns)");
        return false;
    }

    uint8_t *src = buffer + 3;
    for (int i = 0; i < decoder->num_components; i++, src += 2) {
        component *comp = decoder->components + i;
        int C = src[0];
        if (C != comp->id) {
            ok_jpg_error(jpg, "Invalid SOS segment (C)");
            return false;
        }

        comp->Td = src[1] >> 4;
        comp->Ta = src[1] & 0x0f;
        if (comp->Td > 3 || comp->Ta > 3) {
            ok_jpg_error(jpg, "Invalid SOS segment (Td/Ta)");
            return false;
        }
    }

    if (src[0] != 0 || src[1] != 63 || src[2] != 0) {
        ok_jpg_error(jpg, "Invalid SOS segment (Ss/Se/Ah/Al)");
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
    return ok_seek(decoder, length);
}

// MARK: JPEG decoding entry point

static void decode_jpg2(jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;

    // Read header
    uint8_t jpg_header[2];
    if (!ok_read(decoder, jpg_header, 2)) {
        return;
    }
    if (jpg_header[0] != 0xFF || jpg_header[1] != 0xD8) {
        ok_jpg_error(jpg, "Invalid signature (not a JPG file)");
        return;
    }

    while (!decoder->eoi_found) {
        // Read marker
        uint8_t buffer[2];
        int marker;
        if (decoder->next_marker != 0) {
            marker = decoder->next_marker;
            decoder->next_marker = 0;
        } else {
            if (!ok_read(decoder, buffer, 2)) {
                return;
            }
            if (buffer[0] != 0xFF) {
                ok_jpg_error(jpg, "Invalid JPG marker 0x%02X%02X", buffer[0], buffer[1]);
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
        } else if (marker == 0xC4) {
            // DHT
            success = decoder->info_only ? skip_segment(decoder) : read_dht(decoder);
        } else if (marker == 0xD9) {
            // EOI
            decoder->eoi_found = true;
        } else if (marker == 0xDA) {
            // SOS
            if (!decoder->info_only) {
                success = read_sos(decoder);
            } else {
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
        } else if (marker == 0xDB) {
            // DQT
            success = decoder->info_only ? skip_segment(decoder) : read_dqt(decoder);
        } else if (marker == 0xDD) {
            // DRI
            success = read_dri(decoder);
        } else if (marker == 0xE1) {
            // APP1 - EXIF metadata
            success = read_exif(decoder);
        } else if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
            // APP or Comment
            success = skip_segment(decoder);
        } else if ((marker & 0xF0) == 0xC0) {
            ok_jpg_error(jpg,
                         "Unsupported JPEG (marker 0xFF%02X) - progressive, extended, or lossless",
                         marker);
            success = false;
        } else {
            ok_jpg_error(jpg, "Unsupported or corrupt JPEG (marker 0xFF%02X)", marker);
            success = false;
        }

        if (!success) {
            return;
        }
    }

    if (!decoder->complete) {
        ok_jpg_error(jpg, "Incomplete JPEG image data");
    }
}

static ok_jpg *decode_jpg(void *user_data, ok_jpg_input_func input_func,
                          const ok_jpg_color_format color_format, const bool flip_y,
                          const bool info_only) {
    ok_jpg *jpg = calloc(1, sizeof(ok_jpg));
    if (!jpg) {
        return NULL;
    }
    if (!input_func) {
        ok_jpg_error(jpg, "Invalid argument: input_func is NULL");
        return jpg;
    }

    jpg_decoder *decoder = calloc(1, sizeof(jpg_decoder));
    if (!decoder) {
        ok_jpg_error(jpg, "Couldn't allocate decoder.");
        return jpg;
    }

    decoder->jpg = jpg;
    decoder->input_data = user_data;
    decoder->input_func = input_func;
    decoder->color_format = color_format;
    decoder->flip_y = flip_y;
    decoder->info_only = info_only;

    decode_jpg2(decoder);

    free(decoder);
    return jpg;
}
