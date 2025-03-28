/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2020 David Brackeen

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

#include "ok_wav.h"
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

static const int OK_WAV_DECODE_FLAGS_ENDIAN_MASK = 3;

enum ok_wav_encoding {
    OK_WAV_ENCODING_UNKNOWN,
    OK_WAV_ENCODING_PCM,
    OK_WAV_ENCODING_ULAW,
    OK_WAV_ENCODING_ALAW,
    OK_WAV_ENCODING_APPLE_IMA_ADPCM,
    OK_WAV_ENCODING_MS_IMA_ADPCM,
    OK_WAV_ENCODING_MS_ADPCM,
};

typedef struct {
    ok_wav *wav;

    enum ok_wav_encoding encoding;

    // For ADPCM formats
    uint32_t block_size;
    uint32_t frames_per_block;

    // Decode options
    ok_wav_decode_flags decode_flags;

    // Allocator
    ok_wav_allocator allocator;
    void *allocator_user_data;

    // Input
    ok_wav_input input;
    void *input_user_data;
} ok_wav_decoder;

#define ok_wav_error(wav, error_code, message) ok_wav_set_error((wav), (error_code))

static void ok_wav_set_error(ok_wav *wav, ok_wav_error error_code) {
    if (wav) {
        wav->error_code = error_code;
    }
}

static bool ok_read(ok_wav_decoder *decoder, uint8_t *buffer, size_t length) {
    if (decoder->input.read(decoder->input_user_data, buffer, length) == length) {
        return true;
    } else {
        ok_wav_error(decoder->wav, OK_WAV_ERROR_IO, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(ok_wav_decoder *decoder, long length) {
    if (decoder->input.seek(decoder->input_user_data, length)) {
        return true;
    } else {
        ok_wav_error(decoder->wav, OK_WAV_ERROR_IO, "Seek error: error calling input function.");
        return false;
    }
}

#ifndef OK_NO_DEFAULT_ALLOCATOR

static void *ok_stdlib_alloc(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void ok_stdlib_free(void *user_data, void *memory) {
    (void)user_data;
    free(memory);
}

const ok_wav_allocator OK_WAV_DEFAULT_ALLOCATOR = {
    .alloc = ok_stdlib_alloc,
    .free = ok_stdlib_free,
    .audio_alloc = NULL
};

#endif

#define ok_malloc(size) decoder->allocator.alloc(decoder->allocator_user_data, (size))
#define ok_free(ptr) decoder->allocator.free(decoder->allocator_user_data, (ptr))

static size_t ok_malloc_wav_data(ok_wav_decoder *decoder,
                                 uint64_t output_frames_max,
                                 uint8_t output_channels,
                                 uint8_t output_bit_depth) {
    uint64_t size = output_frames_max * output_channels * (output_bit_depth / 8);
    size_t platform_size = (size_t)size;
    if (platform_size == 0 || platform_size != size) {
        return 0;
    }
    if (decoder->allocator.audio_alloc) {
        decoder->wav->data = decoder->allocator.audio_alloc(decoder->allocator_user_data,
                                                            output_frames_max,
                                                            output_channels,
                                                            output_bit_depth);
    } else {
        decoder->wav->data =  ok_malloc(platform_size);
    }
    return platform_size;
}

#ifndef OK_NO_STDIO

static size_t ok_file_read(void *user_data, uint8_t *buffer, size_t length) {
    return fread(buffer, 1, length, (FILE *)user_data);
}

static bool ok_file_seek(void *user_data, long count) {
    return fseek((FILE *)user_data, count, SEEK_CUR) == 0;
}

static const ok_wav_input OK_WAV_FILE_INPUT = {
    .read = ok_file_read,
    .seek = ok_file_seek,
};

#endif

static void ok_wav_decode(ok_wav *wav, ok_wav_decode_flags decode_flags,
                          ok_wav_input input, void *input_user_data,
                          ok_wav_allocator allocator, void *allocator_user_data);

// MARK: Public API

#if !defined(OK_NO_STDIO) && !defined(OK_NO_DEFAULT_ALLOCATOR)

ok_wav ok_wav_read(FILE *file, ok_wav_decode_flags decode_flags) {
    return ok_wav_read_with_allocator(file, decode_flags, OK_WAV_DEFAULT_ALLOCATOR, NULL);
}

#endif

#if !defined(OK_NO_STDIO)

ok_wav ok_wav_read_with_allocator(FILE *file, ok_wav_decode_flags decode_flags,
                                  ok_wav_allocator allocator, void *allocator_user_data) {
    ok_wav wav = { 0 };
    if (file) {
        ok_wav_decode(&wav, decode_flags, OK_WAV_FILE_INPUT, file, allocator, allocator_user_data);
    } else {
        ok_wav_error(&wav, OK_WAV_ERROR_API, "File not found");
    }
    return wav;
}

#endif

ok_wav ok_wav_read_from_input(ok_wav_decode_flags decode_flags,
                              ok_wav_input input_callbacks, void *input_callbacks_user_data,
                              ok_wav_allocator allocator, void *allocator_user_data) {
    ok_wav wav = { 0 };
    ok_wav_decode(&wav, decode_flags, input_callbacks, input_callbacks_user_data,
                  allocator, allocator_user_data);
    return wav;
}

// MARK: Input helpers

static inline uint16_t readBE16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

static inline uint32_t readBE32(const uint8_t *data) {
    return (((uint32_t)data[0] << 24) |
            ((uint32_t)data[1] << 16) |
            ((uint32_t)data[2] << 8) |
            ((uint32_t)data[3] << 0));
}

static inline uint64_t readBE64(const uint8_t *data) {
    return ((((uint64_t)data[0]) << 56) |
            (((uint64_t)data[1]) << 48) |
            (((uint64_t)data[2]) << 40) |
            (((uint64_t)data[3]) << 32) |
            (((uint64_t)data[4]) << 24) |
            (((uint64_t)data[5]) << 16) |
            (((uint64_t)data[6]) << 8) |
            (((uint64_t)data[7]) << 0));
}

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (((uint32_t)data[3] << 24) |
            ((uint32_t)data[2] << 16) |
            ((uint32_t)data[1] << 8) |
            ((uint32_t)data[0] << 0));
}

// MARK: Conversion

static void ok_wav_convert_endian(ok_wav_decoder *decoder) {
    ok_wav *wav = decoder->wav;

    uint64_t input_data_length = wav->num_frames * wav->num_channels;
    uint64_t output_data_length = input_data_length * sizeof(int16_t);
    size_t platform_data_length = (size_t)output_data_length;

    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    bool should_convert;
    switch (decoder->decode_flags & OK_WAV_DECODE_FLAGS_ENDIAN_MASK) {
        case OK_WAV_ENDIAN_NO_CONVERSION: default:
            should_convert = false;
            break;
        case OK_WAV_ENDIAN_NATIVE:
            should_convert = wav->little_endian != system_is_little_endian;
            break;
        case OK_WAV_ENDIAN_BIG:
            should_convert = wav->little_endian;
            break;
        case OK_WAV_ENDIAN_LITTLE:
            should_convert = !wav->little_endian;
            break;
    }

    if (should_convert && wav->bit_depth > 8) {
        // Swap data
        uint8_t *data = wav->data;
        const uint8_t *data_end = (const uint8_t *)wav->data + platform_data_length;
        if (wav->bit_depth == 16) {
            while (data < data_end) {
                const uint8_t t = data[0];
                data[0] = data[1];
                data[1] = t;
                data += 2;
            }
        } else if (wav->bit_depth == 24) {
            while (data < data_end) {
                const uint8_t t = data[0];
                data[0] = data[2];
                data[2] = t;
                data += 3;
            }
        } else if (wav->bit_depth == 32) {
            while (data < data_end) {
                const uint8_t t0 = data[0];
                data[0] = data[3];
                data[3] = t0;
                const uint8_t t1 = data[1];
                data[1] = data[2];
                data[2] = t1;
                data += 4;
            }
        } else if (wav->bit_depth == 48) {
            while (data < data_end) {
                const uint8_t t0 = data[0];
                data[0] = data[5];
                data[5] = t0;
                const uint8_t t1 = data[1];
                data[1] = data[4];
                data[4] = t1;
                const uint8_t t2 = data[2];
                data[2] = data[3];
                data[3] = t2;
                data += 6;
            }
        } else if (wav->bit_depth == 64) {
            while (data < data_end) {
                const uint8_t t0 = data[0];
                data[0] = data[7];
                data[7] = t0;
                const uint8_t t1 = data[1];
                data[1] = data[6];
                data[6] = t1;
                const uint8_t t2 = data[2];
                data[2] = data[5];
                data[5] = t2;
                const uint8_t t3 = data[3];
                data[3] = data[4];
                data[4] = t3;
                data += 8;
            }
        }
        wav->little_endian = !wav->little_endian;
    }
}

// MARK: Decoding

// See g711.c commonly available on the internet
// http://web.mit.edu/audio/src/build/i386_linux2/sox-11gamma-cb/g711.c

static const int16_t ok_wav_ulaw_table[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
    -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
    -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
    -1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
    -876, -844, -812, -780, -748, -716, -684, -652,
    -620, -588, -556, -524, -492, -460, -428, -396,
    -372, -356, -340, -324, -308, -292, -276, -260,
    -244, -228, -212, -196, -180, -164, -148, -132,
    -120, -112, -104, -96, -88, -80, -72, -64,
    -56, -48, -40, -32, -24, -16, -8, 0,
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
    23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
    11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
    7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140,
    5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
    3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004,
    2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
    1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436,
    1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
    876, 844, 812, 780, 748, 716, 684, 652,
    620, 588, 556, 524, 492, 460, 428, 396,
    372, 356, 340, 324, 308, 292, 276, 260,
    244, 228, 212, 196, 180, 164, 148, 132,
    120, 112, 104, 96, 88, 80, 72, 64,
    56, 48, 40, 32, 24, 16, 8, 0,
};

static const int16_t ok_wav_alaw_table[256] = {
    -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
    -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
    -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
    -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
    -30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
    -11008, -10496, -12032, -11520, -8960, -8448, -9984, -9472,
    -15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
    -344, -328, -376, -360, -280, -264, -312, -296,
    -472, -456, -504, -488, -408, -392, -440, -424,
    -88, -72, -120, -104, -24, -8, -56, -40,
    -216, -200, -248, -232, -152, -136, -184, -168,
    -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
    -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
    -688, -656, -752, -720, -560, -528, -624, -592,
    -944, -912, -1008, -976, -816, -784, -880, -848,
    5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
    7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
    2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
    3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
    22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
    30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
    11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
    15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
    344, 328, 376, 360, 280, 264, 312, 296,
    472, 456, 504, 488, 408, 392, 440, 424,
    88, 72, 120, 104, 24, 8, 56, 40,
    216, 200, 248, 232, 152, 136, 184, 168,
    1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
    1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
    688, 656, 752, 720, 560, 528, 624, 592,
    944, 912, 1008, 976, 816, 784, 880, 848,
};

static void ok_wav_decode_logarithmic_pcm_data(ok_wav_decoder *decoder, const int16_t table[256]) {
    static const size_t buffer_size = 1024;

    ok_wav *wav = decoder->wav;

    // Allocate buffers
    const uint8_t output_bit_depth = 16;
    uint64_t input_data_length = wav->num_frames * wav->num_channels;
    uint8_t *buffer = ok_malloc(buffer_size);
    if (!buffer) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate buffer");
        goto done;
    }
    ok_malloc_wav_data(decoder, wav->num_frames, wav->num_channels, output_bit_depth);
    if (!wav->data) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    int16_t *output = wav->data;
    while (input_data_length > 0) {
        size_t bytes_to_read = (size_t)min(input_data_length, buffer_size);
        if (!ok_read(decoder, buffer, bytes_to_read)) {
            goto done;
        }
        input_data_length -= bytes_to_read;
        for (uint64_t i = 0; i < bytes_to_read; i++) {
            *output++ = table[buffer[i]];
        }
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = output_bit_depth;

done:
    ok_free(buffer);
}

struct ok_wav_ima_state {
    int32_t predictor;
    int8_t step_index;
};

static int16_t ok_wav_decode_ima_adpcm_nibble(struct ok_wav_ima_state *channel_state,
                                              uint8_t nibble) {
    static const int ima_index_table[16] = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    static const uint16_t ima_step_table[89] = {
        7, 8, 9, 10, 11, 12, 13, 14,
        16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66,
        73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411,
        1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
        3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
        32767
    };

    if (channel_state->step_index < 0) {
        channel_state->step_index = 0;
    } else if (channel_state->step_index > 88) {
        channel_state->step_index = 88;
    }
    uint16_t step = ima_step_table[channel_state->step_index];
    int32_t diff = step >> 3;
    if (nibble & 1) {
        diff += (step >> 2);
    }
    if (nibble & 2) {
        diff += (step >> 1);
    }
    if (nibble & 4) {
        diff += step;
    }
    if (nibble & 8) {
        channel_state->predictor -= diff;
    } else {
        channel_state->predictor += diff;
    }
    if (channel_state->predictor > 32767) {
        channel_state->predictor = 32767;
    } else if (channel_state->predictor < -32768) {
        channel_state->predictor = -32768;
    }

    channel_state->step_index += ima_index_table[nibble];

    return (int16_t)channel_state->predictor;
}

// See https://wiki.multimedia.cx/index.php/Apple_QuickTime_IMA_ADPCM
// and https://wiki.multimedia.cx/index.php?title=IMA_ADPCM
// and http://www.drdobbs.com/database/algorithm-alley/184410326
static void ok_wav_decode_apple_ima_adpcm_data(ok_wav_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    struct ok_wav_ima_state *channel_states = NULL;
    uint8_t *block = NULL;
    uint8_t num_channels = wav->num_channels;

    // Allocate buffers
    const uint64_t output_frames_max = (wav->num_frames + 1) & ~1u;
    const uint8_t output_bit_depth = 16;
    channel_states = ok_malloc(num_channels * sizeof(struct ok_wav_ima_state));
    if (!channel_states) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate channel_state buffer");
        goto done;
    }
    memset(channel_states, 0, num_channels * sizeof(struct ok_wav_ima_state));
    block = ok_malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate packet");
        goto done;
    }
    ok_malloc_wav_data(decoder, output_frames_max, wav->num_channels, output_bit_depth);
    if (!wav->data) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        uint64_t frames = min(remaining_frames, decoder->frames_per_block);
        if (!ok_read(decoder, block, decoder->block_size)) {
            goto done;
        }

        // Each input block contains one channel. Convert to signed 16-bit and interleave.
        uint8_t *packet = block;
        for (int channel = 0; channel < num_channels; channel++) {
            struct ok_wav_ima_state *channel_state = channel_states + channel;

            // Each block starts with a 2-byte preamble
            uint16_t preamble = readBE16(packet);
            int32_t predictor = (int16_t)(preamble & ~0x7f);
            channel_state->step_index = preamble & 0x7f;
            packet += 2;

            if ((channel_state->predictor & ~0x7f) != predictor) {
                channel_state->predictor = predictor;
            }

            uint8_t *input = packet;
            int16_t *channel_output = output + channel;
            int16_t *channel_output_end = channel_output + num_channels * frames;
            while (channel_output < channel_output_end) {
                *channel_output = ok_wav_decode_ima_adpcm_nibble(channel_state, (*input) & 0x0f);
                channel_output += num_channels;
                *channel_output = ok_wav_decode_ima_adpcm_nibble(channel_state, (*input) >> 4);
                channel_output += num_channels;
                input++;
            }

            packet += (decoder->frames_per_block + 1) / 2;
        }

        output += frames * num_channels;
        remaining_frames -= frames;
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = output_bit_depth;

done:
    ok_free(block);
    ok_free(channel_states);
}

// Similar to Apple's IMA ADPCM.
// See https://wiki.multimedia.cx/index.php?title=Microsoft_IMA_ADPCM
static void ok_wav_decode_ms_ima_adpcm_data(ok_wav_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    struct ok_wav_ima_state *channel_states = NULL;
    uint8_t *block = NULL;
    uint8_t num_channels = wav->num_channels;

    // Allocate buffers
    const uint64_t output_frames_max = wav->num_frames + 7; // 1 frame, then 8 frames at once
    const uint8_t output_bit_depth = 16;
    channel_states = ok_malloc(num_channels * sizeof(struct ok_wav_ima_state));
    if (!channel_states) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate channel_state buffer");
        goto done;
    }
    memset(channel_states, 0, num_channels * sizeof(struct ok_wav_ima_state));
    block = ok_malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate block");
        goto done;
    }
    ok_malloc_wav_data(decoder, output_frames_max, wav->num_channels, output_bit_depth);
    if (!wav->data) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        const uint64_t block_frames = min(remaining_frames, decoder->frames_per_block);
        int64_t frames = (int64_t)block_frames;
        if (!ok_read(decoder, block, decoder->block_size)) {
            goto done;
        }

        // Preamble - 2 bytes for predictor, 1 bytes for index, 1 empty byte
        uint8_t *input = block;
        int16_t *block_output = output;
        for (int channel = 0; channel < num_channels; channel++) {
            int16_t sample = (int16_t)(wav->little_endian ? readLE16(input) : readBE16(input));
            channel_states[channel].predictor = sample;
            channel_states[channel].step_index = (int8_t)input[2];
            input += 4;

            *block_output++ = sample;
        }
        frames--;

        // Frames - 8 frames (4 bytes) for each channel
        while (frames > 0) {
            for (int channel = 0; channel < num_channels; channel++) {
                struct ok_wav_ima_state *channel_state = channel_states + channel;
                int16_t *channel_output = block_output + channel;
                for (int i = 0; i < 4; i++) {
                    *channel_output = ok_wav_decode_ima_adpcm_nibble(channel_state,
                                                                     (*input) & 0x0f);
                    channel_output += num_channels;
                    *channel_output = ok_wav_decode_ima_adpcm_nibble(channel_state, (*input) >> 4);
                    channel_output += num_channels;
                    input++;
                }
            }
            frames -= 8;
            block_output += 8 * num_channels;
        }

        output += block_frames * num_channels;
        remaining_frames -= block_frames;
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = output_bit_depth;
    
done:
    ok_free(block);
    ok_free(channel_states);
}

struct ok_wav_ms_adpcm_state {
    int32_t coeff1;
    int32_t coeff2;
    uint16_t delta;
    int16_t sample1;
    int16_t sample2;
};

static int16_t ok_wav_decode_ms_adpcm_nibble(struct ok_wav_ms_adpcm_state *channel_state,
                                             uint8_t nibble) {
    static const uint16_t adaptation_table[16] = {
        230, 230, 230, 230, 307, 409, 512, 614,
        768, 614, 512, 409, 307, 230, 230, 230
    };

    if (channel_state->delta < 16) {
        channel_state->delta = 16;
    }

    int8_t signed_nibble = (int8_t)nibble;
    if (nibble & 8) {
        signed_nibble |= 0xf0;
    }

    // According to https://wiki.multimedia.cx/?title=Microsoft_ADPCM this should be divided by 256,
    // not shifted right by 8. Shifting a signed number by N can give different results than
    // dividing by (2^N), but the right-shift matches what the SoX decoder is doing, and was (is?)
    // a common optimization at the time the codec was written.
    int32_t predictor = (channel_state->sample1 * channel_state->coeff1 +
                         channel_state->sample2 * channel_state->coeff2) >> 8;
    predictor += signed_nibble * channel_state->delta;
    int16_t sample;
    if (predictor > 32767) {
        sample = 32767;
    } else if (predictor < -32768) {
        sample = -32768;
    } else {
        sample = (int16_t)predictor;
    }

    channel_state->sample2 = channel_state->sample1;
    channel_state->sample1 = sample;
    channel_state->delta = (adaptation_table[nibble] * channel_state->delta) >> 8;
    return sample;
}

// See https://wiki.multimedia.cx/?title=Microsoft_ADPCM
static void ok_wav_decode_ms_adpcm_data(ok_wav_decoder *decoder) {
    static const int adaptation_coeff1[7] = {
        256, 512, 0, 192, 240, 460, 392
    };

    static const int adaptation_coeff2[7] = {
        0, -256, 0, 64, 0, -208, -232
    };

    ok_wav *wav = decoder->wav;
    struct ok_wav_ms_adpcm_state *channel_states = NULL;
    uint8_t *block = NULL;
    uint8_t num_channels = wav->num_channels;
    const bool is_le = wav->little_endian;

    // Allocate buffers
    const uint64_t output_frames_max = wav->num_frames + 1;
    const uint8_t output_bit_depth = 16;
    channel_states = ok_malloc(num_channels * sizeof(struct ok_wav_ms_adpcm_state));
    if (!channel_states) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate channel_state buffer");
        goto done;
    }
    memset(channel_states, 0, num_channels * sizeof(struct ok_wav_ms_adpcm_state));
    block = ok_malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate block");
        goto done;
    }
    ok_malloc_wav_data(decoder, output_frames_max, wav->num_channels, output_bit_depth);
    if (!wav->data) {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        uint64_t block_frames = min(remaining_frames, decoder->frames_per_block);
        int64_t frames = (int64_t)block_frames;
        if (!ok_read(decoder, block, decoder->block_size)) {
            goto done;
        }

        // Preamble (interleaved)
        uint8_t *input = block;
        for (int channel = 0; channel < num_channels; channel++) {
            const uint8_t coeff_index = min(*input, 6);
            channel_states[channel].coeff1 = adaptation_coeff1[coeff_index];
            channel_states[channel].coeff2 = adaptation_coeff2[coeff_index];
            input++;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            channel_states[channel].delta = (is_le ? readLE16(input) : readBE16(input));
            input += 2;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            channel_states[channel].sample1 = (int16_t)(is_le ? readLE16(input) : readBE16(input));
            input += 2;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            channel_states[channel].sample2 = (int16_t)(is_le ? readLE16(input) : readBE16(input));
            input += 2;
        }

        // Initial output (sample2 first)
        int16_t *block_output = output;
        for (int channel = 0; channel < num_channels; channel++) {
            *block_output++ = channel_states[channel].sample2;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            *block_output++ = channel_states[channel].sample1;
        }
        frames -= 2;

        // Frames (interleaved)
        int64_t samples = frames * num_channels;
        if (num_channels <= 2) {
            struct ok_wav_ms_adpcm_state *channel_state1 = channel_states;
            struct ok_wav_ms_adpcm_state *channel_state2 = channel_states + (num_channels - 1);
            while (samples > 0) {
                *block_output++ = ok_wav_decode_ms_adpcm_nibble(channel_state1, (*input) >> 4);
                *block_output++ = ok_wav_decode_ms_adpcm_nibble(channel_state2, (*input) & 0x0f);
                input++;
                samples -= 2;
            }
        } else {
            int channel = 0;
            while (samples > 0) {
                *block_output++ = ok_wav_decode_ms_adpcm_nibble(channel_states + channel,
                                                                (*input) >> 4);
                channel = (channel + 1) % num_channels;
                *block_output++ = ok_wav_decode_ms_adpcm_nibble(channel_states + channel,
                                                                (*input) & 0x0f);
                channel = (channel + 1) % num_channels;
                input++;
                samples -= 2;
            }
        }

        output += block_frames * num_channels;
        remaining_frames -= block_frames;
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = output_bit_depth;

done:
    ok_free(block);
    ok_free(channel_states);
}

static void ok_wav_decode_pcm_data(ok_wav_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    size_t size = ok_malloc_wav_data(decoder, wav->num_frames, wav->num_channels, wav->bit_depth);
    if (wav->data) {
        ok_read(decoder, wav->data, size);
    } else {
        ok_wav_error(wav, OK_WAV_ERROR_ALLOCATION, "Couldn't allocate memory for audio");
    }
}

// MARK: Container file formats (WAV, CAF)

static bool ok_wav_valid_bit_depth(const ok_wav *wav, enum ok_wav_encoding encoding) {
    if (encoding == OK_WAV_ENCODING_ULAW || encoding == OK_WAV_ENCODING_ALAW) {
        return (wav->bit_depth == 8 && wav->is_float == false);
    } else if (encoding == OK_WAV_ENCODING_APPLE_IMA_ADPCM) {
        return wav->is_float == false;
    } else if (encoding == OK_WAV_ENCODING_MS_IMA_ADPCM || encoding == OK_WAV_ENCODING_MS_ADPCM) {
        return (wav->bit_depth == 4 && wav->is_float == false);
    } else {
        if (wav->is_float) {
            return (wav->bit_depth == 32 || wav->bit_depth == 64);
        } else {
            return (wav->bit_depth == 8 || wav->bit_depth == 16 ||
                    wav->bit_depth == 24 || wav->bit_depth == 32 ||
                    wav->bit_depth == 48 || wav->bit_depth == 64);
        }
    }
}

static void ok_wav_decode_data(ok_wav_decoder *decoder, uint64_t data_length) {
    ok_wav *wav = decoder->wav;
    if (wav->sample_rate <= 0 || wav->num_channels <= 0) {
        ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Invalid file (header not found)");
        return;
    }

    if (decoder->encoding == OK_WAV_ENCODING_APPLE_IMA_ADPCM ||
        decoder->encoding == OK_WAV_ENCODING_MS_IMA_ADPCM ||
        decoder->encoding == OK_WAV_ENCODING_MS_ADPCM) {
        if (decoder->block_size == 0 || decoder->frames_per_block == 0) {
            ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Invalid block size");
            return;
        }
        const uint64_t blocks_needed = ((wav->num_frames + (decoder->frames_per_block - 1)) /
                                        decoder->frames_per_block);
        const uint64_t blocks_available = (data_length / decoder->block_size);
        if (blocks_available != blocks_needed) {
            // Audacity encoder is giving an invalid FACT chunk in some cases.
            // Use the calculated number of frames instead.
            wav->num_frames = blocks_available * decoder->frames_per_block;
        }
    } else if (wav->num_frames == 0) {
        if (wav->bit_depth >= 8) {
            wav->num_frames = data_length / ((wav->bit_depth / 8) * wav->num_channels);
        } else {
            ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Frame length not defined");
            return;
        }
    }
    switch (decoder->encoding) {
        case OK_WAV_ENCODING_UNKNOWN:
            // Do nothing
            break;
        case OK_WAV_ENCODING_PCM:
            ok_wav_decode_pcm_data(decoder);
            break;
        case OK_WAV_ENCODING_ALAW:
            ok_wav_decode_logarithmic_pcm_data(decoder, ok_wav_alaw_table);
            break;
        case OK_WAV_ENCODING_ULAW:
            ok_wav_decode_logarithmic_pcm_data(decoder, ok_wav_ulaw_table);
            break;
        case OK_WAV_ENCODING_APPLE_IMA_ADPCM:
            ok_wav_decode_apple_ima_adpcm_data(decoder);
            break;
        case OK_WAV_ENCODING_MS_IMA_ADPCM:
            ok_wav_decode_ms_ima_adpcm_data(decoder);
            break;
        case OK_WAV_ENCODING_MS_ADPCM:
            ok_wav_decode_ms_adpcm_data(decoder);
            break;
    }

    if (decoder->encoding != OK_WAV_ENCODING_UNKNOWN && wav->data) {
        ok_wav_convert_endian(decoder);
    }
}

static void ok_wav_decode_wav_file(ok_wav_decoder *decoder, bool is_little_endian) {
    ok_wav *wav = decoder->wav;
    wav->little_endian = is_little_endian;

    uint8_t header[8];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }

    bool valid = memcmp("WAVE", header + 4, 4) == 0;
    if (!valid) {
        ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Not a valid WAV file");
        return;
    }

    // Read chunks
    while (true) {
        uint8_t chunk_header[8];
        if (!ok_read(decoder, chunk_header, sizeof(chunk_header))) {
            return;
        }

        uint32_t chunk_length = (is_little_endian ? readLE32(chunk_header + 4) :
                                 readBE32(chunk_header + 4));

        if (memcmp("fmt ", chunk_header, 4) == 0) {
            uint8_t chunk_data[50];
            if (!(chunk_length >= 16 && chunk_length <= sizeof(chunk_data))) {
                ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Invalid WAV file (not PCM)");
                return;
            }
            if (!ok_read(decoder, chunk_data, chunk_length)) {
                return;
            }
            uint16_t format;
            if (is_little_endian) {
                format = readLE16(chunk_data);
                wav->num_channels = (uint8_t)readLE16(chunk_data + 2);
                wav->sample_rate = readLE32(chunk_data + 4);
                decoder->block_size = readLE16(chunk_data + 12);
                wav->bit_depth = (uint8_t)readLE16(chunk_data + 14);
            } else {
                format = readBE16(chunk_data);
                wav->num_channels = (uint8_t)readBE16(chunk_data + 2);
                wav->sample_rate = readBE32(chunk_data + 4);
                decoder->block_size = readBE16(chunk_data + 12);
                wav->bit_depth = (uint8_t)readBE16(chunk_data + 14);
            }

            if (format == 65534 && chunk_length == 40) {
                format = is_little_endian ? readLE16(chunk_data + 24) : readBE16(chunk_data + 24);
            }

            if (format == 1) {
                decoder->encoding = OK_WAV_ENCODING_PCM;
            } else if (format == 2) {
                decoder->encoding = OK_WAV_ENCODING_MS_ADPCM;
            } else if (format == 3) {
                decoder->encoding = OK_WAV_ENCODING_PCM;
                wav->is_float = true;
            } else if (format == 6) {
                decoder->encoding = OK_WAV_ENCODING_ALAW;
            } else if (format == 7) {
                decoder->encoding = OK_WAV_ENCODING_ULAW;
            } else if (format == 0x11) {
                decoder->encoding = OK_WAV_ENCODING_MS_IMA_ADPCM;
            } else {
                decoder->encoding = OK_WAV_ENCODING_UNKNOWN;
            }

            if (chunk_length >= 20 && (decoder->encoding == OK_WAV_ENCODING_MS_ADPCM ||
                                       decoder->encoding == OK_WAV_ENCODING_MS_IMA_ADPCM)) {
                decoder->frames_per_block = (is_little_endian ? readLE16(chunk_data + 18) :
                                             readBE16(chunk_data + 18));
                bool valid_frames_per_block = false;
                if (decoder->frames_per_block > 0) {
                    if (decoder->encoding == OK_WAV_ENCODING_MS_ADPCM) {
                        valid_frames_per_block = (((decoder->frames_per_block - 1) / 2 + 7) *
                                                  wav->num_channels == decoder->block_size);
                    } else if (decoder->encoding == OK_WAV_ENCODING_MS_IMA_ADPCM) {
                        valid_frames_per_block = (((decoder->frames_per_block - 1) / 2 + 4) *
                                                  wav->num_channels == decoder->block_size);
                    }
                }
                if (!valid_frames_per_block) {
                    ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Invalid frames per block");
                    return;
                }
            }

            bool valid_format = (decoder->encoding != OK_WAV_ENCODING_UNKNOWN &&
                                 ok_wav_valid_bit_depth(wav, decoder->encoding) &&
                                 wav->num_channels > 0);
            if (!valid_format) {
                ok_wav_error(wav, OK_WAV_ERROR_UNSUPPORTED,
                             "Unsupported WAV format. Must be PCM, and a bit depth of "
                             "8, 16, 32, 48, or 64-bit.");
                return;
            }
        } else if (memcmp("fact", chunk_header, 4) == 0) {
            uint8_t chunk_data[4];
            if (chunk_length >= sizeof(chunk_data)) {
                if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                    return;
                }
                wav->num_frames = is_little_endian ? readLE32(chunk_data) : readBE32(chunk_data);
                chunk_length -= sizeof(chunk_data);
            }
            if (!ok_seek(decoder, (long)chunk_length)) {
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            ok_wav_decode_data(decoder, chunk_length);
            return;
        } else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, (long)chunk_length)) {
                return;
            }
        }
    }
}

static void ok_wav_decode_caf_file(ok_wav_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }

    uint16_t file_version = readBE16(header);
    if (file_version != 1) {
        ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Not a CAF file");
        return;
    }

    while (true) {
        // Read chunk type and length
        uint8_t chunk_header[12];
        if (!ok_read(decoder, chunk_header, sizeof(chunk_header))) {
            return;
        }
        const int64_t chunk_length = (int64_t)readBE64(chunk_header + 4);

        if (memcmp("desc", chunk_header, 4) == 0) {
            // Read desc chunk
            if (chunk_length != 32) {
                ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Corrupt CAF file (bad desc)");
                return;
            }
            uint8_t chunk_data[32];
            if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                return;
            }

            union {
                double value;
                uint64_t bits;
            } sample_rate;
            char format_id[4];

            sample_rate.bits = readBE64(chunk_data);
            memcpy(format_id, chunk_data + 8, 4);
            uint32_t format_flags = readBE32(chunk_data + 12);
            uint32_t bytes_per_packet = readBE32(chunk_data + 16);
            uint32_t frames_per_packet = readBE32(chunk_data + 20);
            uint32_t channels_per_frame = readBE32(chunk_data + 24);
            uint32_t bits_per_channel = readBE32(chunk_data + 28);
            uint32_t bytes_per_channel = bits_per_channel / 8;

            wav->sample_rate = sample_rate.value;
            wav->num_channels = (uint8_t)channels_per_frame;
            wav->is_float = format_flags & 1;
            wav->little_endian = (format_flags & 2) != 0;
            wav->bit_depth = (uint8_t)bits_per_channel;

            if (memcmp("lpcm", format_id, 4) == 0) {
                decoder->encoding = OK_WAV_ENCODING_PCM;
            } else if (memcmp("ulaw", format_id, 4) == 0) {
                decoder->encoding = OK_WAV_ENCODING_ULAW;
            } else if (memcmp("alaw", format_id, 4) == 0) {
                decoder->encoding = OK_WAV_ENCODING_ALAW;
            } else if (memcmp("ima4", format_id, 4) == 0) {
                decoder->encoding = OK_WAV_ENCODING_APPLE_IMA_ADPCM;
            } else {
                decoder->encoding = OK_WAV_ENCODING_UNKNOWN;
            }

            bool valid_bytes_per_packet;
            if (decoder->encoding == OK_WAV_ENCODING_APPLE_IMA_ADPCM) {
                if (wav->num_channels > 0) {
                    decoder->frames_per_block = frames_per_packet;
                    decoder->block_size = bytes_per_packet;
                    valid_bytes_per_packet = (((frames_per_packet + 1) / 2 + 2) *
                                              wav->num_channels == decoder->block_size);
                } else {
                    valid_bytes_per_packet = false;
                }
            } else {
                const uint32_t bpp = bytes_per_channel * channels_per_frame;
                valid_bytes_per_packet = (frames_per_packet == 1 && bytes_per_packet == bpp);
            }

            bool valid_format = (decoder->encoding != OK_WAV_ENCODING_UNKNOWN &&
                                 (wav->sample_rate > 0) &&
                                 (wav->num_channels > 0) &&
                                 valid_bytes_per_packet &&
                                 (ok_wav_valid_bit_depth(wav, decoder->encoding)));
            if (!valid_format) {
                ok_wav_error(wav, OK_WAV_ERROR_UNSUPPORTED,
                             "Unsupported CAF format. Must be PCM, mono or stereo, and "
                             "8-, 16-, 24- or 32-bit.)");
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            if (chunk_length < 4) {
                ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Corrupt CAF file (bad data)");
                return;
            }
            // Skip the edit count
            if (!ok_seek(decoder, 4)) {
                return;
            }
            // Read the data and return (skip any remaining chunks)
            ok_wav_decode_data(decoder, (uint64_t)(chunk_length - 4));
            return;
        } else if (memcmp("pakt", chunk_header, 4) == 0) {
            // Read pakt chunk
            if (chunk_length != 24) {
                ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Corrupt CAF file (bad pakt)");
                return;
            }
            uint8_t chunk_data[24];
            if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                return;
            }

            decoder->wav->num_frames = readBE64(chunk_data + 8);
        } else if (chunk_length < 0) {
            ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Invalid chunk length");
            return;
        } else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, (long)chunk_length)) {
                return;
            }
        }
    }
}

static void ok_wav_decode(ok_wav *wav, ok_wav_decode_flags decode_flags,
                          ok_wav_input input, void *input_user_data,
                          ok_wav_allocator allocator, void *allocator_user_data) {
    if (!input.read || !input.seek) {
        ok_wav_error(wav, OK_WAV_ERROR_API,
                     "Invalid argument: read_func and seek_func must not be NULL");
        return;
    }
    
    if (!allocator.alloc || !allocator.free) {
        ok_wav_error(wav, OK_WAV_ERROR_API,
                     "Invalid argument: allocator alloc and free functions must not be NULL");
        return;
    }
    ok_wav_decoder decoder = { 0 };

    decoder.wav = wav;
    decoder.decode_flags = decode_flags;
    decoder.allocator = allocator;
    decoder.allocator_user_data = allocator_user_data;
    decoder.input = input;
    decoder.input_user_data = input_user_data;

    uint8_t header[4];
    if (ok_read(&decoder, header, sizeof(header))) {
        //printf("File '%.4s'\n", header);
        if (memcmp("RIFF", header, 4) == 0) {
            ok_wav_decode_wav_file(&decoder, true);
        } else if (memcmp("RIFX", header, 4) == 0) {
            ok_wav_decode_wav_file(&decoder, false);
        } else if (memcmp("caff", header, 4) == 0) {
            ok_wav_decode_caf_file(&decoder);
        } else {
            ok_wav_error(wav, OK_WAV_ERROR_INVALID, "Not a PCM WAV or CAF file.");
        }
    }
}
