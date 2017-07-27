/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen

 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
 associated documentation files (the "Software"), to deal in the Software without restriction,
 including without limitation the rights to use, copy, modify, merge, publish, distribute,
 sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all copies or
 substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
 NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
 OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 + JPEG spec
   http://www.w3.org/Graphics/JPEG/itu-t81.pdf
   http://www.w3.org/Graphics/JPEG/jfif3.pdf
   http://www.fifi.org/doc/jhead/exif-e.html
 + Another easy-to-read JPEG decoder (written in python)
   https://github.com/enmasse/jpeg_read/blob/master/jpeg_read.py
 */

#include "ok_jpg.h"
#include <stdlib.h>
#include <string.h>

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
#define MAX_COMPONENTS 3
#define HUFFMAN_LOOKUP_SIZE_BITS 10
#define HUFFMAN_LOOKUP_SIZE (1 << HUFFMAN_LOOKUP_SIZE_BITS)

typedef void (*ok_jpg_idct_func)(const int16_t *const input, uint8_t *output);

typedef struct {
    uint8_t id;
    uint8_t H;
    uint8_t V;
    uint8_t Tq;
    uint8_t Td;
    uint8_t Ta;
    uint8_t output[C_WIDTH * C_WIDTH];
    int16_t pred;
    int16_t *blocks;
    size_t next_block;
    int blocks_v;
    int blocks_h;
    int eob_run;
    ok_jpg_idct_func idct;
    bool complete;
} ok_jpg_component;

typedef struct {
    uint16_t code[256];
    uint8_t val[256];
    uint8_t size[257];
    uint8_t lookup_num_bits[HUFFMAN_LOOKUP_SIZE];
    uint8_t lookup_val[HUFFMAN_LOOKUP_SIZE];
    uint8_t lookup_ac_num_bits[HUFFMAN_LOOKUP_SIZE];
    int16_t lookup_ac_val[HUFFMAN_LOOKUP_SIZE];
    int maxcode[16];
    int mincode[16];
    int valptr[16];
    int count; // "lastk" in spec
} ok_jpg_huffman_table;

typedef struct {
    // Output image
    ok_jpg *jpg;

    // Decode options
    bool color_rgba;
    bool flip_x;
    bool flip_y;
    bool rotate;
    bool info_only;

    // Input
    void *input_data;
    ok_jpg_read_func input_read_func;
    ok_jpg_seek_func input_seek_func;
    uint8_t input_buffer[256];
    uint8_t *input_buffer_start;
    uint8_t *input_buffer_end;
    uint32_t input_buffer_bits;
    int input_buffer_bit_count;

    // Output
    uint8_t *dst_buffer;
    uint32_t dst_stride;

    // State
    bool progressive;
    bool eoi_found;
    bool sof_found;
    int next_marker;
    int restart_intervals;
    int restart_intervals_remaining;
    int next_restart;

    // JPEG data
    uint16_t in_width;
    uint16_t in_height;
    int data_units_x;
    int data_units_y;
    int num_components;
    ok_jpg_component components[MAX_COMPONENTS];
    uint8_t q_table[4][8 * 8];

    // Scan
    int num_scan_components;
    int scan_components[MAX_COMPONENTS];
    int scan_start;      // "Ss" in spec
    int scan_end;        // "Se"
    int scan_prev_scale; // "Ah"
    int scan_scale;      // "Al"

    // 0 = DC table, 1 = AC table
    ok_jpg_huffman_table huffman_tables[2][4];
} ok_jpg_decoder;

#ifdef NDEBUG
#define ok_jpg_error(jpg, message) ok_jpg_set_error((jpg), "ok_jpg_error")
#else
#define ok_jpg_error(jpg, message) ok_jpg_set_error((jpg), (message))
#endif

static void ok_jpg_set_error(ok_jpg *jpg, const char *message) {
    if (jpg) {
        free(jpg->data);
        jpg->data = NULL;
        jpg->width = 0;
        jpg->height = 0;
        jpg->error_message = message;
    }
}

static inline uint8_t ok_read_uint8(ok_jpg_decoder *decoder) {
    if (decoder->input_buffer_start == decoder->input_buffer_end) {
        size_t len = decoder->input_read_func(decoder->input_data, decoder->input_buffer,
                                              sizeof(decoder->input_buffer));
        decoder->input_buffer_start = decoder->input_buffer;
        decoder->input_buffer_end = decoder->input_buffer + len;

        if (len == 0) {
            return 0;
        }
    }
    return *decoder->input_buffer_start++;
}

static bool ok_read(ok_jpg_decoder *decoder, uint8_t *buffer, size_t length) {
    size_t available = (size_t)(decoder->input_buffer_end - decoder->input_buffer_start);
    if (available) {
        size_t len = min(length, available);
        memcpy(buffer, decoder->input_buffer_start, len);
        decoder->input_buffer_start += len;
        length -= len;
        if (length == 0) {
            return true;
        }
        buffer += len;
    }
    if (decoder->input_read_func(decoder->input_data, buffer, length) == length) {
        return true;
    } else {
        ok_jpg_error(decoder->jpg, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(ok_jpg_decoder *decoder, long length) {
    if (length == 0) {
        return true;
    } else if (length < 0) {
        ok_jpg_error(decoder->jpg, "Seek error: negative seek unsupported.");
        return false;
    }
    size_t available = (size_t)(decoder->input_buffer_end - decoder->input_buffer_start);

    size_t len = min((size_t)length, available);
    decoder->input_buffer_start += len;
    length -= len;
    if (length > 0) {
        if (decoder->input_seek_func(decoder->input_data, length)) {
            return true;
        } else {
            ok_jpg_error(decoder->jpg, "Seek error: error calling input function.");
            return false;
        }
    } else {
        return true;
    }
}

#ifndef OK_NO_STDIO

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, size_t length) {
    return fread(buffer, 1, length, (FILE *)user_data);
}

static bool ok_file_seek_func(void *user_data, long count) {
    return fseek((FILE *)user_data, count, SEEK_CUR) == 0;
}

#endif

static ok_jpg *ok_jpg_decode(void *user_data, ok_jpg_read_func input_read_func,
                             ok_jpg_seek_func input_seek_func,
                             uint8_t *dst_buffer, uint32_t dst_stride,
                             ok_jpg_decode_flags decode_flags, bool check_user_data);

// MARK: Public API

#ifndef OK_NO_STDIO

ok_jpg *ok_jpg_read(FILE *file, ok_jpg_decode_flags decode_flags) {
    return ok_jpg_decode(file, ok_file_read_func, ok_file_seek_func, NULL, 0, decode_flags, true);
}

ok_jpg *ok_jpg_read_to_buffer(FILE *file, uint8_t *dst_buffer, uint32_t dst_stride,
                              ok_jpg_decode_flags decode_flags) {
    return ok_jpg_decode(file, ok_file_read_func, ok_file_seek_func, dst_buffer, dst_stride,
                         decode_flags, true);
}

#endif

ok_jpg *ok_jpg_read_from_callbacks(void *user_data, ok_jpg_read_func read_func,
                                   ok_jpg_seek_func seek_func,
                                   ok_jpg_decode_flags decode_flags) {
    return ok_jpg_decode(user_data, read_func, seek_func, NULL, 0, decode_flags, false);
}

ok_jpg *ok_jpg_read_from_callbacks_to_buffer(void *user_data, ok_jpg_read_func read_func,
                                             ok_jpg_seek_func seek_func,
                                             uint8_t *dst_buffer, uint32_t dst_stride,
                                             ok_jpg_decode_flags decode_flags) {
    return ok_jpg_decode(user_data, read_func, seek_func, dst_buffer, dst_stride,
                         decode_flags, false);
}

void ok_jpg_free(ok_jpg *jpg) {
    if (jpg) {
        free(jpg->data);
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
static inline void ok_jpg_load_bits(ok_jpg_decoder *decoder, int num_bits) {
    while (decoder->input_buffer_bit_count < num_bits) {
        if (decoder->next_marker != 0) {
            decoder->input_buffer_bits <<= 8;
            decoder->input_buffer_bit_count += 8;
        } else {
            uint8_t b = ok_read_uint8(decoder);
            if (b == 0xff) {
                uint8_t marker = ok_read_uint8(decoder);
                if (marker != 0) {
                    decoder->next_marker = marker;
                    b = 0;
                }
            }
            decoder->input_buffer_bits = (decoder->input_buffer_bits << 8) | b;
            decoder->input_buffer_bit_count += 8;
        }
    }
}

// Assumes at least num_bits of data was previously loaded in ok_jpg_load_bits
static inline int ok_jpg_peek_bits(ok_jpg_decoder *decoder, int num_bits) {
    return (int)((decoder->input_buffer_bits >> (decoder->input_buffer_bit_count - num_bits)) &
                 ((1 << num_bits) - 1));
}

// Assumes at least num_bits of data was previously loaded in ok_jpg_load_bits
static inline void ok_jpg_consume_bits(ok_jpg_decoder *decoder, int num_bits) {
    decoder->input_buffer_bit_count -= num_bits;
}

static inline void ok_jpg_dump_bits(ok_jpg_decoder *decoder) {
    decoder->input_buffer_bits = 0;
    decoder->input_buffer_bit_count = 0;
}

static inline int ok_jpg_load_next_bits(ok_jpg_decoder *decoder, int num_bits) {
    ok_jpg_load_bits(decoder, num_bits);
    int mask = (1 << num_bits) - 1;
    decoder->input_buffer_bit_count -= num_bits;
    return (int)(decoder->input_buffer_bits >> decoder->input_buffer_bit_count) & mask;
}

static inline int ok_jpg_extend(const int v, const int t) {
    // Figure F.12
    if (v < (1 << (t - 1))) {
        return v + ((-1) << t) + 1;
    } else {
        return v;
    }
}

// MARK: Huffman decoding

static void ok_jpg_generate_huffman_table(ok_jpg_huffman_table *huff, const uint8_t *bits) {
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
            if (i >= HUFFMAN_LOOKUP_SIZE_BITS) {
                huff->maxcode[i] = (huff->maxcode[i] << (15 - i)) | ((1 << (15 - i)) - 1);
            }
        }
    }
}

static void ok_jpg_generate_huffman_table_lookups(ok_jpg_huffman_table *huff, bool is_ac_table) {
    // Look up table for codes that use N bits or less (most of them)
    for (int q = 0; q < HUFFMAN_LOOKUP_SIZE; q++) {
        huff->lookup_num_bits[q] = 0;
        for (uint8_t i = 0; i < HUFFMAN_LOOKUP_SIZE_BITS; i++) {
            uint8_t num_bits = i + 1;
            int code = q >> (HUFFMAN_LOOKUP_SIZE_BITS - num_bits);
            if (code <= huff->maxcode[i]) {
                huff->lookup_num_bits[q] = num_bits;

                int j = huff->valptr[i];
                j += code - huff->mincode[i];
                huff->lookup_val[q] = huff->val[j];
                break;
            }
        }
    }

    if (is_ac_table) {
        // Additional lookup table to get both RS and extended value
        for (int q = 0; q < HUFFMAN_LOOKUP_SIZE; q++) {
            int num_bits = huff->lookup_num_bits[q];
            if (num_bits > 0) {
                int rs = huff->lookup_val[q];
                int s = rs & 0x0f;
                int total_bits = num_bits + s;
                if (total_bits <= HUFFMAN_LOOKUP_SIZE_BITS) {
                    huff->lookup_ac_num_bits[q] = (uint8_t)total_bits;
                    if (s > 0) {
                        int v = (q >> (HUFFMAN_LOOKUP_SIZE_BITS - total_bits)) & ((1 << s) - 1);
                        huff->lookup_ac_val[q] = (int16_t)ok_jpg_extend(v, s);
                    }
                }
            }
        }
    }
}

static inline uint8_t ok_jpg_huffman_decode(ok_jpg_decoder *decoder,
                                            const ok_jpg_huffman_table *table) {
    // JPEG spec: "Decode" (Figure F.16)

    // First, try lookup tables
    ok_jpg_load_bits(decoder, 16);
    int code = ok_jpg_peek_bits(decoder, HUFFMAN_LOOKUP_SIZE_BITS);
    int num_bits = table->lookup_num_bits[code];
    if (num_bits != 0) {
        ok_jpg_consume_bits(decoder, num_bits);
        return table->lookup_val[code];
    }

    // Next, try a code up to 16-bits
    code = ok_jpg_peek_bits(decoder, 16);
    for (int i = HUFFMAN_LOOKUP_SIZE_BITS; i < 16; i++) {
        if (code <= table->maxcode[i]) {
            ok_jpg_consume_bits(decoder, i + 1);
            code >>= (15 - i);
            int j = table->valptr[i];
            j += code - table->mincode[i];
            return table->val[j];
        }
    }
    // Shouldn't happen
    ok_jpg_error(decoder->jpg, "Invalid huffman code");
    return 0;
}

// MARK: JPEG color conversion

static inline uint8_t ok_jpg_clip_uint8(const int x) {
    return ((unsigned int)x) < 0xff ? (uint8_t)x : (x < 0 ? 0 : 0xff);
}

static inline uint8_t ok_jpg_clip_fp_uint8(const int fx) {
    return ((unsigned int)fx) < 0xff0000 ? (uint8_t)(fx >> 16) : (fx < 0 ? 0 : 0xff);
}

static inline void ok_jpg_convert_YCbCr_to_RGB(uint8_t Y, uint8_t Cb, uint8_t Cr,
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
    *r = ok_jpg_clip_fp_uint8(fr);
    *g = ok_jpg_clip_fp_uint8(fg);
    *b = ok_jpg_clip_fp_uint8(fb);
}

// Convert from grayscale to RGBA
static void ok_jpg_convert_data_unit_grayscale(const uint8_t *y,
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
static void ok_jpg_convert_data_unit_color(const uint8_t *y, const uint8_t *cb, const uint8_t *cr,
                                           uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a,
                                           const int x_inc, const int y_inc,
                                           const int max_width, const int max_height) {
    const int in_row_offfset = C_WIDTH - max_width;
    const int out_row_offset = y_inc - x_inc * max_width;

    for (int v = 0; v < max_height; v++) {
        const uint8_t *y_end = y + max_width;
        while (y < y_end) {
            ok_jpg_convert_YCbCr_to_RGB(*y, *cb, *cr, r, g, b);
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

static void ok_jpg_convert_data_unit(ok_jpg_decoder *decoder, int data_unit_x, int data_unit_y) {
    ok_jpg *jpg = decoder->jpg;
    ok_jpg_component *c = decoder->components;
    int x = data_unit_x * c->H * 8;
    int y = data_unit_y * c->V * 8;
    const int width = min(c->H * 8, decoder->in_width - x);
    const int height = min(c->V * 8, decoder->in_height - y);
    int x_inc = 4;
    int y_inc = (int)(decoder->dst_stride ? decoder->dst_stride : jpg->width * 4);
    uint8_t *data = decoder->dst_buffer;
    if (decoder->rotate) {
        int temp = x;
        x = y;
        y = temp;
    }
    if (decoder->flip_x) {
        data += ((int)jpg->width * 4) - (x + 1) * x_inc;
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
        ok_jpg_convert_data_unit_grayscale(c->output, data, data + 1, data + 2, data + 3,
                                           x_inc, y_inc, width, height);
    } else if (decoder->color_rgba) {
        ok_jpg_convert_data_unit_color(c->output, (c + 1)->output, (c + 2)->output,
                                       data, data + 1, data + 2, data + 3,
                                       x_inc, y_inc, width, height);
    } else {
        ok_jpg_convert_data_unit_color(c->output, (c + 1)->output, (c + 2)->output,
                                       data + 2, data + 1, data, data + 3,
                                       x_inc, y_inc, width, height);
    }
}

// MARK: IDCT

// Output is scaled by (1 << 12) * sqrt(2) / (1 << out_shift)
static inline void ok_jpg_idct_1d_8(int *out, const int out_shift,
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
static inline void ok_jpg_idct_1d_16(int *out, const int out_shift,
                                     const int v0, const int v1, const int v2, const int v3,
                                     const int v4, const int v5, const int v6, const int v7) {
    // Constants scaled by (1 << 12).
    static const int c1 = 5765;  // cos( 1*pi/32) * sqrt(2)
    static const int c2 = 5681;  // cos( 2*pi/32) * sqrt(2)
    static const int c3 = 5543;  // cos( 3*pi/32) * sqrt(2)
    static const int c4 = 5352;  // cos( 4*pi/32) * sqrt(2)
    static const int c5 = 5109;  // cos( 5*pi/32) * sqrt(2)
    static const int c6 = 4816;  // cos( 6*pi/32) * sqrt(2)
    static const int c7 = 4478;  // cos( 7*pi/32) * sqrt(2)
    static const int c9 = 3675;  // cos( 9*pi/32) * sqrt(2)
    static const int c10 = 3218; // cos(10*pi/32) * sqrt(2)
    static const int c11 = 2731; // cos(11*pi/32) * sqrt(2)
    static const int c12 = 2217; // cos(12*pi/32) * sqrt(2)
    static const int c13 = 1682; // cos(13*pi/32) * sqrt(2)
    static const int c14 = 1130; // cos(14*pi/32) * sqrt(2)
    static const int c15 = 568;  // cos(15*pi/32) * sqrt(2)

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
        out[0]  = (p0 + q0) >> out_shift;
        out[1]  = (p1 + q1) >> out_shift;
        out[2]  = (p2 + q2) >> out_shift;
        out[3]  = (p3 + q3) >> out_shift;
        out[4]  = (p4 + q4) >> out_shift;
        out[5]  = (p5 + q5) >> out_shift;
        out[6]  = (p6 + q6) >> out_shift;
        out[7]  = (p7 + q7) >> out_shift;
        out[8]  = (p7 - q7) >> out_shift;
        out[9]  = (p6 - q6) >> out_shift;
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

// ok_jpg_idct_1d_h scales output by ((1 << 12) * sqrt(2)).
// Shift-right by 8 so output is scaled ((1 << 4) * sqrt(2)).
// Input is zig-zagged
#define ok_jpg_idct_1d_h(idct_func, in, out, s) \
	do { \
		idct_func(out + 0 * s, 8, in[0],  in[2] , in[3],  in[9],  in[10], in[20], in[21], in[35]); \
		idct_func(out + 1 * s, 8, in[1],  in[4] , in[8],  in[11], in[19], in[22], in[34], in[36]); \
		idct_func(out + 2 * s, 8, in[5],  in[7] , in[12], in[18], in[23], in[33], in[37], in[48]); \
		idct_func(out + 3 * s, 8, in[6],  in[13], in[17], in[24], in[32], in[38], in[47], in[49]); \
		idct_func(out + 4 * s, 8, in[14], in[16], in[25], in[31], in[39], in[46], in[50], in[57]); \
		idct_func(out + 5 * s, 8, in[15], in[26], in[30], in[40], in[45], in[51], in[56], in[58]); \
		idct_func(out + 6 * s, 8, in[27], in[29], in[41], in[44], in[52], in[55], in[59], in[62]); \
		idct_func(out + 7 * s, 8, in[28], in[42], in[43], in[53], in[54], in[60], in[61], in[63]); \
	} while (0)

// Input is scaled by ((1 << 4) * sqrt(2)).
// idct_1d scales output by ((1 << 12) * sqrt(2)), for a total output scale of (1 << 17).
// Shift by 19 to get rid of the scale and to divide by 4 at the same time.
// (Divide by 4 per the IDCT formula, JPEG spec section A.3.3)
#define ok_jpg_idct_1d_w(idct_func, in, temp_row, output, s, w, h) \
	do { \
		const int *in2 = in; \
		for (int y = 0; y < h; y++) { \
			idct_func(temp_row, 19, \
                      in2[0 * s], in2[1 * s], in2[2 * s], in2[3 * s], \
                      in2[4 * s], in2[5 * s], in2[6 * s], in2[7 * s]); \
			for (int x = 0; x < w; x++) { \
				output[x] = ok_jpg_clip_uint8(temp_row[x] + 128); \
			} \
			in2++; \
			output += C_WIDTH; \
		} \
	} while (0)

// IDCT a 8x8 input block to 8x8 in an output of size (C_WIDTH x C_WIDTH)
static void ok_jpg_idct_8x8(const int16_t * const input, uint8_t *output) {
    int out[8 * 8];
    int temp_row[8];
    ok_jpg_idct_1d_h(ok_jpg_idct_1d_8, input, out, 8);
    ok_jpg_idct_1d_w(ok_jpg_idct_1d_8, out, temp_row, output, 8, 8, 8);
}

// IDCT a 8x8 block to 8x16 in an output of size (C_WIDTH x C_WIDTH)
static void ok_jpg_idct_8x16(const int16_t * const input, uint8_t *output) {
    int out[8 * 16];
    int temp_row[8];
    ok_jpg_idct_1d_h(ok_jpg_idct_1d_16, input, out, 16);
    ok_jpg_idct_1d_w(ok_jpg_idct_1d_8, out, temp_row, output, 16, 8, 16);
}

// IDCT a 8x8 block to 16x8 in an output of size (C_WIDTH x C_WIDTH)
static void ok_jpg_idct_16x8(const int16_t * const input, uint8_t *output) {
    int out[16 * 8];
    int temp_row[16];
    ok_jpg_idct_1d_h(ok_jpg_idct_1d_8, input, out, 16);
    ok_jpg_idct_1d_w(ok_jpg_idct_1d_16, out, temp_row, output, 16, 16, 8);
}

// IDCT a 8x8 block to 16x16 in an output of size (C_WIDTH x C_WIDTH)
static void ok_jpg_idct_16x16(const int16_t * const input, uint8_t *output) {
    int out[16 * 16];
    int temp_row[16];
    ok_jpg_idct_1d_h(ok_jpg_idct_1d_16, input, out, 16);
    ok_jpg_idct_1d_w(ok_jpg_idct_1d_16, out, temp_row, output, 16, 16, 16);
}

// MARK: Entropy decoding

#define OK_JPG_BLOCK_EXTRA_SPACE 15

static inline bool ok_jpg_decode_block(ok_jpg_decoder *decoder, ok_jpg_component *c,
                                       int16_t *block) {
    // Decode DC coefficients - F.2.2.1
    ok_jpg_huffman_table *dc = decoder->huffman_tables[0] + c->Td;
    uint8_t t = ok_jpg_huffman_decode(decoder, dc);
    if (t > 0) {
        int diff = ok_jpg_load_next_bits(decoder, t);
        c->pred += ok_jpg_extend(diff, t);
    }
    block[0] = c->pred;

    // Decode AC coefficients - Figures F.13 and F.14
    ok_jpg_huffman_table *ac = decoder->huffman_tables[1] + c->Ta;
    int k = 1;
    while (k <= 63) {
        ok_jpg_load_bits(decoder, 16);
        int code = ok_jpg_peek_bits(decoder, HUFFMAN_LOOKUP_SIZE_BITS);
        uint8_t num_bits = ac->lookup_ac_num_bits[code];
        if (num_bits > 0) {
            ok_jpg_consume_bits(decoder, num_bits);
            uint8_t rs = ac->lookup_val[code];
            int s = rs & 0x0f;
            if (s > 0) {
                int r = rs >> 4;
                k += r;
                block[k] = ac->lookup_ac_val[code];
                k++;
            } else {
                if (rs == 0) {
                    break;
                }
                k += 16;
            }
        } else {
            uint8_t rs = ok_jpg_huffman_decode(decoder, ac);
            int s = rs & 0x0f;
            if (s > 0) {
                int r = rs >> 4;
                k += r;
                int v = ok_jpg_load_next_bits(decoder, s);
                block[k] = (int16_t)ok_jpg_extend(v, s);
                k++;
            } else {
                if (rs == 0) {
                    break;
                }
                k += 16;
            }
        }
    }
    return true;
}

static bool ok_jpg_decode_block_progressive(ok_jpg_decoder *decoder, ok_jpg_component *c,
                                            int16_t *block) {
    int k = decoder->scan_start;
    const int k_end = decoder->scan_end;
    const int scale = decoder->scan_scale;

    // Decode DC coefficients - F.2.2.1
    if (k == 0) {
        ok_jpg_huffman_table *dc = decoder->huffman_tables[0] + c->Td;
        uint8_t t = ok_jpg_huffman_decode(decoder, dc);
        if (t > 0) {
            int diff = ok_jpg_load_next_bits(decoder, t);
            c->pred += ok_jpg_extend(diff, t) << scale;
        }
        block[0] = c->pred;
        k++;
    }

    // Decode AC coefficients - Figures F.13, F.14, and G.2
    if (c->eob_run > 0) {
        c->eob_run--;
        return true;
    }
    ok_jpg_huffman_table *ac = decoder->huffman_tables[1] + c->Ta;
    while (k <= k_end) {
        uint8_t rs = ok_jpg_huffman_decode(decoder, ac);
        int s = rs & 0x0f;
        int r = rs >> 4;
        if (s == 0) {
            if (r != 0x0f) {
                if (r > 0) {
                    int next_bits = ok_jpg_load_next_bits(decoder, r);
                    c->eob_run = (1 << r) + next_bits - 1;
                }
                break;
            }
            k += 16;
        } else {
            k += r;
            int v = ok_jpg_load_next_bits(decoder, s);
            block[k] = (int16_t)(ok_jpg_extend(v, s) << scale);
            k++;
        }
    }
    return true;
}

static bool ok_jpg_decode_block_subsequent_scan(ok_jpg_decoder *decoder, ok_jpg_component *c,
                                                int16_t *block) {
    int k = decoder->scan_start;
    const int k_end = decoder->scan_end;
    const int16_t lsb = (int16_t)(1 << decoder->scan_scale);

    // Decode DC coefficients - Section G.1.2.1
    if (k == 0) {
        int correction = ok_jpg_load_next_bits(decoder, 1);
        if (correction) {
            block[0] |= lsb;
        }
        k++;
    }

    // Decode AC coefficients - Section G.1.2.3
    ok_jpg_huffman_table *ac = decoder->huffman_tables[1] + c->Ta;
    int r = -1;
    int16_t v = 0;
    if (c->eob_run > 0) {
        c->eob_run--;
        r = 64;
    }
    while (k <= k_end) {
        if (r < 0) {
            uint8_t rs = ok_jpg_huffman_decode(decoder, ac);
            int s = rs & 0x0f;
            r = rs >> 4;
            if (s == 0) {
                if (r != 0x0f) {
                    if (r > 0) {
                        int next_bits = ok_jpg_load_next_bits(decoder, r);
                        c->eob_run = (1 << r) + next_bits - 1;
                    }
                    r = 64;
                }
            } else {
                int sign = ok_jpg_load_next_bits(decoder, 1);
                v = sign ? lsb : -lsb;
            }
        }

        int16_t *coeff = block + k;
        if (*coeff != 0) {
            int correction = ok_jpg_load_next_bits(decoder, 1);
            if (correction) {
                if (*coeff < 0) {
                    *coeff -= lsb;
                } else {
                    *coeff += lsb;
                }
            }
        } else {
            if (r == 0 && v != 0) {
                *coeff = v;
                v = 0;
            }
            r--;
        }
        k++;
    }
    return true;
}

static inline void ok_jpg_dequantize(ok_jpg_decoder *decoder, ok_jpg_component *c, int16_t *block) {
    const uint8_t *q_table = decoder->q_table[c->Tq];
    for (int k = 0; k < 64; k++) {
        block[k] *= q_table[k];
    }
}

static void ok_jpg_decode_restart(ok_jpg_decoder *decoder) {
    decoder->restart_intervals_remaining = decoder->restart_intervals;
    for (int i = 0; i < decoder->num_scan_components; i++) {
        ok_jpg_component *c = decoder->components + decoder->scan_components[i];
        c->pred = 0;
        c->eob_run = 0;
    }
}

static bool ok_jpg_decode_restart_if_needed(ok_jpg_decoder *decoder) {
    if (decoder->restart_intervals_remaining > 0) {
        decoder->restart_intervals_remaining--;

        if (decoder->restart_intervals_remaining == 0) {
            ok_jpg_dump_bits(decoder);
            if (decoder->next_marker != 0) {
                if (decoder->next_marker == 0xD0 + decoder->next_restart) {
                    decoder->next_marker = 0;
                } else {
                    ok_jpg_error(decoder->jpg, "Invalid restart marker (1)");
                    return false;
                }
            } else {
                uint8_t buffer[2];
                if (!ok_read(decoder, buffer, 2)) {
                    return false;
                }
                if (!(buffer[0] == 0xff && buffer[1] == 0xD0 + decoder->next_restart)) {
                    ok_jpg_error(decoder->jpg, "Invalid restart marker (2)");
                    return false;
                }
            }
            decoder->next_restart = (decoder->next_restart + 1) & 7;

            ok_jpg_decode_restart(decoder);
        }
    }
    return true;
}

static bool ok_jpg_decode_scan(ok_jpg_decoder *decoder) {
    decoder->next_restart = 0;
    ok_jpg_decode_restart(decoder);
    if (decoder->restart_intervals_remaining > 0) {
        // Increment because the restart is checked before each data unit instead of after.
        decoder->restart_intervals_remaining++;
    }
    if (decoder->progressive) {
        bool (*decode_function)(ok_jpg_decoder *decoder, ok_jpg_component *c, int16_t *block);
        if (decoder->scan_prev_scale > 0) {
            decode_function = ok_jpg_decode_block_subsequent_scan;
        } else {
            decode_function = ok_jpg_decode_block_progressive;
        }
        if (decoder->num_scan_components == 1) {
            ok_jpg_component *c = decoder->components + decoder->scan_components[0];
            c->next_block = 0;
            for (int data_unit_y = 0; data_unit_y < c->blocks_v; data_unit_y++) {
                size_t block_index = c->next_block;
                for (int data_unit_x = 0; data_unit_x < c->blocks_h; data_unit_x++) {
                    ok_jpg_decode_restart_if_needed(decoder);
                    if (!decode_function(decoder, c, c->blocks + (block_index * 64))) {
                        return false;
                    }
                    block_index++;
                }
                c->next_block += (size_t)(c->H * decoder->data_units_x);
            }
        } else {
            for (int i = 0; i < decoder->num_scan_components; i++) {
                ok_jpg_component *c = decoder->components + decoder->scan_components[i];
                c->next_block = 0;
            }
            for (int data_unit_y = 0; data_unit_y < decoder->data_units_y; data_unit_y++) {
                for (int data_unit_x = 0; data_unit_x < decoder->data_units_x; data_unit_x++) {
                    ok_jpg_decode_restart_if_needed(decoder);
                    for (int i = 0; i < decoder->num_scan_components; i++) {
                        ok_jpg_component *c = decoder->components + decoder->scan_components[i];
                        size_t block_index = c->next_block;
                        for (int y = 0; y < c->V; y++) {
                            for (int x = 0; x < c->H; x++) {
                                if (!decode_function(decoder, c, c->blocks + (block_index * 64))) {
                                    return false;
                                }
                                block_index++;
                            }
                            block_index += (size_t)(c->H * (decoder->data_units_x - 1));
                        }
                        c->next_block += c->H;
                    }
                }
                for (int i = 0; i < decoder->num_scan_components; i++) {
                    ok_jpg_component *c = decoder->components + decoder->scan_components[i];
                    c->next_block += (size_t)((c->V - 1) * c->H * decoder->data_units_x);
                }
            }
        }
    } else {
        int16_t block[64 + OK_JPG_BLOCK_EXTRA_SPACE];
        for (int data_unit_y = 0; data_unit_y < decoder->data_units_y; data_unit_y++) {
            for (int data_unit_x = 0; data_unit_x < decoder->data_units_x; data_unit_x++) {
                ok_jpg_decode_restart_if_needed(decoder);
                for (int i = 0; i < decoder->num_scan_components; i++) {
                    ok_jpg_component *c = decoder->components + decoder->scan_components[i];
                    int offset_y = 0;
                    for (int y = 0; y < c->V; y++) {
                        int offset_x = 0;
                        for (int x = 0; x < c->H; x++) {
                            memset(block, 0, 8 * 8 * sizeof(*block));
                            if (!ok_jpg_decode_block(decoder, c, block)) {
                                return false;
                            }
                            ok_jpg_dequantize(decoder, c, block);
                            c->idct(block, c->output + offset_x + offset_y);
                            offset_x += 8;
                        }
                        offset_y += C_WIDTH * 8;
                    }
                }
                ok_jpg_convert_data_unit(decoder, data_unit_x, data_unit_y);
            }
        }
    }

    ok_jpg_dump_bits(decoder);

    for (int i = 0; i < decoder->num_scan_components; i++) {
        ok_jpg_component *c = decoder->components + decoder->scan_components[i];
        if (!c->complete) {
            c->complete = decoder->scan_end == 63 && decoder->scan_scale == 0;
        }
    }

    return true;
}

static void ok_jpg_progressive_finish(ok_jpg_decoder *decoder) {
    for (int i = 0; i < decoder->num_components; i++) {
        ok_jpg_component *c = decoder->components + i;
        c->next_block = 0;
    }
    for (int data_unit_y = 0; data_unit_y < decoder->data_units_y; data_unit_y++) {
        for (int data_unit_x = 0; data_unit_x < decoder->data_units_x; data_unit_x++) {
            for (int i = 0; i < decoder->num_components; i++) {
                ok_jpg_component *c = decoder->components + i;
                size_t block_index = c->next_block;
                int offset_y = 0;
                for (int y = 0; y < c->V; y++) {
                    int offset_x = 0;
                    for (int x = 0; x < c->H; x++) {
                        int16_t *block = c->blocks + (block_index * 64);
                        ok_jpg_dequantize(decoder, c, block);
                        c->idct(block, c->output + offset_x + offset_y);
                        block_index++;
                        offset_x += 8;
                    }
                    offset_y += C_WIDTH * 8;
                    block_index += (size_t)(c->H * (decoder->data_units_x - 1));
                }
                c->next_block += c->H;
            }

            ok_jpg_convert_data_unit(decoder, data_unit_x, data_unit_y);
        }
        for (int i = 0; i < decoder->num_components; i++) {
            ok_jpg_component *c = decoder->components + i;
            c->next_block += (size_t)((c->V - 1) * c->H * decoder->data_units_x);
        }
    }
}

// MARK: EXIF

static bool ok_jpg_read_exif(ok_jpg_decoder *decoder) {
    static const char exif_magic[] = {'E', 'x', 'i', 'f', 0, 0};
    static const char tiff_magic_little_endian[] = {0x49, 0x49, 0x2a, 0x00};
    static const char tiff_magic_big_endian[] = {0x4d, 0x4d, 0x00, 0x2a};

    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;

    // Check for Exif header
    uint8_t exif_header[6];
    const int exif_header_length = sizeof(exif_header);
    if (length < exif_header_length) {
        return ok_seek(decoder, length);
    }

    if (!ok_read(decoder, exif_header, exif_header_length)) {
        return false;
    }
    length -= exif_header_length;
    if (memcmp(exif_header, exif_magic, exif_header_length) != 0) {
        return ok_seek(decoder, length);
    }

    // Check for TIFF header
    bool little_endian;
    uint8_t tiff_header[4];
    const int tiff_header_length = sizeof(tiff_header);
    if (length < tiff_header_length) {
        return ok_seek(decoder, length);
    }
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
    uint8_t tag_buffer[12];
    const int tag_length = sizeof(tag_buffer);
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

// MARK: Segment reading

#define intDivCeil(x, y) (((x) + (y)-1) / (y))

static bool ok_jpg_read_sof(ok_jpg_decoder *decoder) {
    // JPEG spec: Table B.2
    ok_jpg *jpg = decoder->jpg;
    uint8_t buffer[3 * 3];
    if (!ok_read(decoder, buffer, 8)) {
        return false;
    }
    int length = readBE16(buffer) - 8;
    int P = buffer[2];
    if (P != 8) {
        ok_jpg_error(jpg, "Invalid component size");
        return false;
    }
    decoder->in_height = readBE16(buffer + 3);
    decoder->in_width = readBE16(buffer + 5);
    if (decoder->in_width == 0 || decoder->in_height == 0) {
        ok_jpg_error(jpg, "Invalid image dimensions");
        return false;
    }
    jpg->width = decoder->rotate ? decoder->in_height : decoder->in_width;
    jpg->height = decoder->rotate ? decoder->in_width : decoder->in_height;
    decoder->num_components = buffer[7];
    if (decoder->num_components == 4) {
        ok_jpg_error(jpg, "Unsupported format (CMYK)");
        return false;
    }
    if (decoder->num_components != 1 && decoder->num_components != 3) {
        ok_jpg_error(jpg, "Invalid component count");
        return false;
    }

    if (length < 3 * decoder->num_components) {
        ok_jpg_error(jpg, "SOF segment too short");
        return false;
    }
    if (!ok_read(decoder, buffer, 3 * (size_t)decoder->num_components)) {
        return false;
    }

    int maxH = 1;
    int maxV = 1;
    int minH = 4;
    int minV = 4;
    for (int i = 0; i < decoder->num_components; i++) {
        ok_jpg_component *c = decoder->components + i;
        c->id = buffer[i * 3 + 0];
        c->H = buffer[i * 3 + 1] >> 4;
        c->V = buffer[i * 3 + 1] & 0x0F;
        c->Tq = buffer[i * 3 + 2];

        if (c->H == 0 || c->V == 0 || c->H > 4 || c->V > 4 || c->Tq > 3) {
            ok_jpg_error(jpg, "Bad component");
            return false;
        }

        if (c->H > MAX_SAMPLING_FACTOR || c->V > MAX_SAMPLING_FACTOR) {
            ok_jpg_error(jpg, "Unsupported sampling factor");
            return false;
        }

        maxH = max(maxH, c->H);
        maxV = max(maxV, c->V);
        minH = min(minH, c->H);
        minV = min(minV, c->V);
        length -= 3;
    }
    if (minH > 1 || minV > 1) {
        maxH = 1;
        maxV = 1;
        for (int i = 0; i < decoder->num_components; i++) {
            ok_jpg_component *c = decoder->components + i;
            c->H /= minH;
            c->V /= minV;
            maxH = max(maxH, c->H);
            maxV = max(maxV, c->V);
        }
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
        ok_jpg_component *c = decoder->components + i;
        c->blocks_h = intDivCeil(decoder->in_width, (maxH / c->H) * 8);
        c->blocks_v = intDivCeil(decoder->in_height, (maxV / c->V) * 8);
        if (c->H == maxH && c->V == maxV) {
            c->idct = ok_jpg_idct_8x8;
        } else if (c->H * 2 == maxH && c->V * 2 == maxV) {
            c->idct = ok_jpg_idct_16x16;
        } else if (c->H == maxH && c->V * 2 == maxV) {
            c->idct = ok_jpg_idct_8x16;
        } else if (c->H * 2 == maxH && c->V == maxV) {
            c->idct = ok_jpg_idct_16x8;
        } else {
            ok_jpg_error(jpg, "Unsupported IDCT sampling factor");
            return false;
        }
    }

    // Allocate data
    if (!decoder->info_only) {
        if (decoder->sof_found) {
            ok_jpg_error(jpg, "Invalid JPEG (Multiple SOF markers)");
            return false;
        }
        decoder->sof_found = true;

        if (decoder->progressive) {
            for (int i = 0; i < decoder->num_components; i++) {
                ok_jpg_component *c = decoder->components + i;
                size_t num_blocks = (size_t)(decoder->data_units_x * c->H *
                                             decoder->data_units_y * c->V);
                c->blocks = calloc(num_blocks * 64, sizeof(*c->blocks) + OK_JPG_BLOCK_EXTRA_SPACE);
                if (!c->blocks) {
                    ok_jpg_error(jpg, "Couldn't allocate internal block memory for image");
                    return false;
                }
            }
        }

        if (!decoder->dst_buffer) {
            uint64_t dst_stride = decoder->dst_stride ? decoder->dst_stride : jpg->width * 4;
            uint64_t size = dst_stride * jpg->height;
            size_t platform_size = (size_t)size;
            if (platform_size == size) {
                decoder->dst_buffer = malloc(platform_size);
            }
            if (!decoder->dst_buffer) {
                ok_jpg_error(jpg, "Couldn't allocate memory for image");
                return false;
            }
            jpg->data = decoder->dst_buffer;
        }
    }

#if 0
    printf("SOF: Components %i (", decoder->num_components);
    for (int i = 0; i < decoder->num_components; i++) {
        ok_jpg_component *c = decoder->components + i;
        if (i != 0) {
            printf(",");
        }
        printf("%ix%i", maxH/c->H, maxV/c->V);
    }
    printf(")\n");
#endif
    return true;
}

static bool ok_jpg_read_sos(ok_jpg_decoder *decoder) {
    // JPEG spec: Table B.3
    ok_jpg *jpg = decoder->jpg;
    uint8_t buffer[16];
    const size_t header_size = 3;
    if (!ok_read(decoder, buffer, header_size)) {
        return false;
    }
    uint16_t length = readBE16(buffer);
    decoder->num_scan_components = buffer[2];
    if (decoder->num_scan_components > decoder->num_components) {
        ok_jpg_error(jpg, "Invalid SOS segment (Ns)");
        return false;
    }
    const size_t expected_size = 3 + (size_t)decoder->num_scan_components * 2;
    if (length != expected_size + header_size) {
        ok_jpg_error(jpg, "Invalid SOS segment (L)");
        return false;
    }
    if (sizeof(buffer) < expected_size) {
        ok_jpg_error(jpg, "Too many components for buffer");
        return false;
    }
    if (!ok_read(decoder, buffer, expected_size)) {
        return false;
    }

    uint8_t *src = buffer;
    for (int i = 0; i < decoder->num_scan_components; i++, src += 2) {
        int C = src[0];
        bool component_found = false;
        for (int j = 0; j < decoder->num_components; j++) {
            ok_jpg_component *c = decoder->components + j;
            if (c->id == C) {
                decoder->scan_components[i] = j;
                component_found = true;
            }
        }
        if (!component_found) {
            ok_jpg_error(jpg, "Invalid SOS segment (C)");
            return false;
        }

        ok_jpg_component *comp = decoder->components + decoder->scan_components[i];
        comp->Td = src[1] >> 4;
        comp->Ta = src[1] & 0x0f;
        if (comp->Td > 3 || comp->Ta > 3) {
            ok_jpg_error(jpg, "Invalid SOS segment (Td/Ta)");
            return false;
        }
    }

    decoder->scan_start = src[0];
    decoder->scan_end = src[1];
    decoder->scan_prev_scale = src[2] >> 4;
    decoder->scan_scale = src[2] & 0x0f;

    if (decoder->progressive) {
        if (decoder->scan_start < 0 || decoder->scan_start > 63 ||
            decoder->scan_end < decoder->scan_start || decoder->scan_end > 63 ||
            decoder->scan_prev_scale < 0 || decoder->scan_prev_scale > 13 ||
            decoder->scan_scale < 0 || decoder->scan_scale > 13) {
            ok_jpg_error(jpg, "Invalid progressive SOS segment (Ss/Se/Ah/Al)");
            return false;
        }
    } else {
        // Sequential
        if (decoder->scan_start != 0 || decoder->scan_end != 63 ||
            decoder->scan_prev_scale != 0 || decoder->scan_scale != 0) {
            ok_jpg_error(jpg, "Invalid SOS segment (Ss/Se/Ah/Al)");
            return false;
        }
    }

#if 0
    printf("SOS: Scan components: (");
    for (int i = 0; i < decoder->num_scan_components; i++) {
        ok_jpg_component *comp = decoder->components + decoder->scan_components[i];
        if (i != 0) {
            printf(",");
        }
        printf("%i", comp->id);

    }
    printf(") ");
    for (int i = decoder->num_scan_components; i < decoder->num_components; i++) {
        printf("  ");
    }
    printf("\trange: %i...%i \tah: %i al: %i\n",
           decoder->scan_start, decoder->scan_end,
           decoder->scan_prev_scale, decoder->scan_scale);
#endif

    return ok_jpg_decode_scan(decoder);
}

static bool ok_jpg_read_dqt(ok_jpg_decoder *decoder) {
    // JPEG spec: Table B.4
    ok_jpg *jpg = decoder->jpg;
    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    while (length >= 65) {
        uint8_t pt = ok_read_uint8(decoder);
        int Pq = pt >> 4;
        int Tq = pt & 0x0f;

        if (Pq == 1) {
            ok_jpg_error(jpg, "Unsupported JPEG (16-bit q_table)");
            return false;
        }
        if (Pq != 0 || Tq > 3) {
            ok_jpg_error(jpg, "Invalid JPEG (Pq/Tq)");
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

static bool ok_jpg_read_dht(ok_jpg_decoder *decoder) {
    // JPEG spec: Table B.5
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
        ok_jpg_huffman_table *table = decoder->huffman_tables[Tc] + Th;
        ok_jpg_generate_huffman_table(table, buffer);
        if (table->count > 0) {
            if (table->count > 256 || table->count > length) {
                ok_jpg_error(jpg, "Invalid DHT segment length");
                return false;
            }
            if (!ok_read(decoder, table->val, (size_t)table->count)) {
                return false;
            }
            length -= table->count;
        }
        bool is_ac_table = Tc == 1;
        ok_jpg_generate_huffman_table_lookups(table, is_ac_table);
    }
    if (length != 0) {
        ok_jpg_error(jpg, "Invalid DHT segment length");
        return false;
    } else {
        return true;
    }
}

static bool ok_jpg_read_dri(ok_jpg_decoder *decoder) {
    // JPEG spec: Table B.7
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

static bool ok_jpg_skip_segment(ok_jpg_decoder *decoder) {
    uint8_t buffer[2];
    if (!ok_read(decoder, buffer, sizeof(buffer))) {
        return false;
    }
    int length = readBE16(buffer) - 2;
    return ok_seek(decoder, length);
}

// MARK: JPEG decoding entry point

static void ok_jpg_decode2(ok_jpg_decoder *decoder) {
    ok_jpg *jpg = decoder->jpg;

    // Read header
    uint8_t jpg_header[2];
    if (!ok_read(decoder, jpg_header, 2)) {
        return;
    }
    if (jpg_header[0] != 0xFF || jpg_header[1] != 0xD8) {
        ok_jpg_error(jpg, "Invalid signature (not a JPEG file)");
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
            if (buffer[0] == 0xFF) {
                marker = buffer[1];
            } else if (buffer[0] == 0x00 && buffer[1] == 0xFF) {
                if (!ok_read(decoder, buffer, 1)) {
                    return;
                }
                marker = buffer[0];
            } else {
                ok_jpg_error(jpg, "Invalid JPEG marker");
                return;
            }
        }

        bool success = true;
        if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            // SOF
            decoder->progressive = (marker == 0xC2);
            success = ok_jpg_read_sof(decoder);
            if (success && decoder->info_only) {
                return;
            }
        } else if (marker == 0xC4) {
            // DHT
            success = decoder->info_only ? ok_jpg_skip_segment(decoder) : ok_jpg_read_dht(decoder);
        } else if (marker == 0xD9) {
            // EOI
            decoder->eoi_found = true;
            if (!decoder->info_only && decoder->progressive) {
                ok_jpg_progressive_finish(decoder);
            }
        } else if (marker == 0xDA) {
            // SOS
            if (!decoder->info_only) {
                success = ok_jpg_read_sos(decoder);
            } else {
                success = ok_jpg_skip_segment(decoder);
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
                            if (buffer[0] != 0 && !(buffer[0] >= 0xD0 && buffer[0] <= 0xD7)) {
                                decoder->next_marker = buffer[0];
                                break;
                            }
                        }
                    }
                }
            }
        } else if (marker == 0xDB) {
            // DQT
            success = decoder->info_only ? ok_jpg_skip_segment(decoder) : ok_jpg_read_dqt(decoder);
        } else if (marker == 0xDD) {
            // DRI
            success = ok_jpg_read_dri(decoder);
        } else if (marker == 0xE1) {
            // APP1 - EXIF metadata
            success = ok_jpg_read_exif(decoder);
        } else if ((marker >= 0xE0 && marker <= 0xEF) || marker == 0xFE) {
            // APP or Comment
            success = ok_jpg_skip_segment(decoder);
        } else if (marker == 0xFF) {
            // Ignore
        } else {
            ok_jpg_error(jpg, "Unsupported or corrupt JPEG");
            success = false;
        }

        if (!success) {
            return;
        }
    }

    if (decoder->num_components == 0) {
        ok_jpg_error(jpg, "SOF not found");
    } else {
        for (int i = 0; i < decoder->num_components; i++) {
            if (!decoder->components[i].complete) {
                ok_jpg_error(jpg, "Missing JPEG image data");
                break;
            }
        }
    }
}

static ok_jpg *ok_jpg_decode(void *user_data, ok_jpg_read_func input_read_func,
                             ok_jpg_seek_func input_seek_func,
                             uint8_t *dst_buffer, uint32_t dst_stride,
                             ok_jpg_decode_flags decode_flags, bool check_user_data) {
    ok_jpg *jpg = calloc(1, sizeof(ok_jpg));
    if (!jpg) {
        return NULL;
    }
    if (check_user_data && !user_data) {
        ok_jpg_error(jpg, "File not found");
        return jpg;
    }
    if (!input_read_func || !input_seek_func) {
        ok_jpg_error(jpg, "Invalid argument: read_func and seek_func must not be NULL");
        return jpg;
    }

    ok_jpg_decoder *decoder = calloc(1, sizeof(ok_jpg_decoder));
    if (!decoder) {
        ok_jpg_error(jpg, "Couldn't allocate decoder.");
        return jpg;
    }

    decoder->jpg = jpg;
    decoder->input_data = user_data;
    decoder->input_read_func = input_read_func;
    decoder->input_seek_func = input_seek_func;
    decoder->dst_buffer = dst_buffer;
    decoder->dst_stride = dst_stride;
    decoder->color_rgba = (decode_flags & OK_JPG_COLOR_FORMAT_BGRA) == 0;
    decoder->flip_y = (decode_flags & OK_JPG_FLIP_Y) != 0;
    decoder->info_only = (decode_flags & OK_JPG_INFO_ONLY) != 0;

    ok_jpg_decode2(decoder);

    for (int i = 0; i < MAX_COMPONENTS; i++) {
        free(decoder->components[i].blocks);
    }
    free(decoder);

    return jpg;
}
