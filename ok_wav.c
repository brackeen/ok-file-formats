/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen

 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#include "ok_wav.h"
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

enum encoding {
    ENCODING_UNKNOWN,
    ENCODING_PCM,
    ENCODING_ULAW,
    ENCODING_ALAW,
    ENCODING_APPLE_IMA_ADPCM,
    ENCODING_MS_IMA_ADPCM,
    ENCODING_MS_ADPCM,
};

typedef struct {
    ok_wav *wav;

    enum encoding encoding;

    // For ADPCM formats
    uint32_t block_size;
    uint32_t frames_per_block;

    // Decode options
    bool convert_to_system_endian;

    // Input
    void *input_data;
    ok_wav_input_func input_func;

} pcm_decoder;

static void ok_wav_error(ok_wav *wav, const char *message) {
    if (wav) {
        free(wav->data);
        wav->data = NULL;

        const size_t len = sizeof(wav->error_message) - 1;
        strncpy(wav->error_message, message, len);
        wav->error_message[len] = 0;
    }
}

static bool ok_read(pcm_decoder *decoder, uint8_t *data, const int length) {
    if (decoder->input_func(decoder->input_data, data, length) == length) {
        return true;
    } else {
        ok_wav_error(decoder->wav, "Read error: error calling input function.");
        return false;
    }
}

static bool ok_seek(pcm_decoder *decoder, const int length) {
    return ok_read(decoder, NULL, length);
}

static void decode_file(ok_wav *wav, void *input_data, ok_wav_input_func input_func,
                        bool convert_to_system_endian);

// MARK: Public API

ok_wav *ok_wav_read(void *user_data, ok_wav_input_func input_func, bool convert_to_system_endian) {
    ok_wav *wav = calloc(1, sizeof(ok_wav));
    if (input_func) {
        decode_file(wav, user_data, input_func, convert_to_system_endian);
    } else {
        ok_wav_error(wav, "Invalid argument: input_func is NULL");
    }
    return wav;
}

void ok_wav_free(ok_wav *wav) {
    if (wav) {
        free(wav->data);
        free(wav);
    }
}

// MARK: Input helpers

static inline uint16_t readBE16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

static inline uint32_t readBE32(const uint8_t *data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static inline uint64_t readBE64(const uint8_t *data) {
    return (
        (((uint64_t)data[0]) << 56) |
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
    return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

// MARK: Decoding

// See g711.c commonly available on the internet
// http://web.mit.edu/audio/src/build/i386_linux2/sox-11gamma-cb/g711.c

static const int16_t ulaw_table[256] = {
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

static const int16_t alaw_table[256] = {
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

static void decode_logarithmic_pcm_data(pcm_decoder *decoder, const int16_t table[256]) {
    static const unsigned int buffer_size = 1024;

    ok_wav *wav = decoder->wav;

    // Allocate buffers
    uint64_t input_data_length = wav->num_frames * wav->num_channels;
    uint64_t output_data_length = input_data_length * sizeof(int16_t);
    size_t platform_data_length = (size_t)output_data_length;
    uint8_t *buffer = malloc(buffer_size);
    if (!buffer) {
        ok_wav_error(wav, "Couldn't allocate buffer");
        goto done;
    }
    if (platform_data_length > 0 && platform_data_length == output_data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    int16_t *output = wav->data;
    while (input_data_length > 0) {
        int bytes_to_read = (int)min(input_data_length, buffer_size);
        if (!ok_read(decoder, buffer, bytes_to_read)) {
            goto done;
        }
        input_data_length -= bytes_to_read;
        for (int i = 0; i < bytes_to_read; i++) {
            *output++ = table[buffer[i]];
        }
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = 16;

done:
    free(buffer);
}
struct ima_state {
    int32_t predictor;
    int8_t step_index;
};

static int16_t decode_ima_adpcm_nibble(struct ima_state *channel_state, uint8_t nibble) {
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
static void decode_apple_ima_adpcm_data(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    struct ima_state *channel_states = NULL;
    uint8_t *block = NULL;
    const int num_channels = wav->num_channels;

    // Allocate buffers
    const uint64_t max_output_frames = (wav->num_frames + 1) & ~1;
    const uint64_t output_data_length = max_output_frames * sizeof(int16_t) * num_channels;
    const size_t platform_data_length = (size_t)output_data_length;
    channel_states = calloc(wav->num_channels, sizeof(struct ima_state));
    if (!channel_states) {
        ok_wav_error(wav, "Couldn't allocate channel_state buffer");
        goto done;
    }
    block = malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, "Couldn't allocate packet");
        goto done;
    }
    if (platform_data_length > 0 && platform_data_length == output_data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        int frames = (int)min(remaining_frames, decoder->frames_per_block);
        if (!ok_read(decoder, block, decoder->block_size)) {
            goto done;
        }

        // Each input block contains one channel. Convert to signed 16-bit and interleave.
        uint8_t *packet = block;
        for (int channel = 0; channel < num_channels; channel++) {
            struct ima_state *channel_state = channel_states + channel;

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
                *channel_output = decode_ima_adpcm_nibble(channel_state, (*input) & 0x0f);
                channel_output += num_channels;
                *channel_output = decode_ima_adpcm_nibble(channel_state, (*input) >> 4);
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
    wav->bit_depth = 16;

done:
    free(block);
    free(channel_states);
}

// Similar to Apple's IMA ADPCM.
// See https://wiki.multimedia.cx/index.php?title=Microsoft_IMA_ADPCM
static void decode_ms_ima_adpcm_data(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    struct ima_state *channel_states = NULL;
    uint8_t *block = NULL;
    const int num_channels = wav->num_channels;

    // Allocate buffers
    const uint64_t max_output_frames = wav->num_frames + 7; // 1 frame, then 8 frames at once
    const uint64_t output_data_length = max_output_frames * sizeof(int16_t) * num_channels;
    const size_t platform_data_length = (size_t)output_data_length;
    channel_states = calloc(wav->num_channels, sizeof(struct ima_state));
    if (!channel_states) {
        ok_wav_error(wav, "Couldn't allocate channel_state buffer");
        goto done;
    }
    block = malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, "Couldn't allocate block");
        goto done;
    }
    if (platform_data_length > 0 && platform_data_length == output_data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        const int block_frames = (int)min(remaining_frames, decoder->frames_per_block);
        int frames = block_frames;
        if (!ok_read(decoder, block, decoder->block_size)) {
            goto done;
        }

        // Preamble - 2 bytes for predictor, 1 bytes for index, 1 empty byte
        uint8_t *input = block;
        for (int channel = 0; channel < num_channels; channel++) {
            int16_t sample = (wav->little_endian ? readLE16(input) : readBE16(input));
            channel_states[channel].predictor = sample;
            channel_states[channel].step_index = input[2];
            input += 4;

            *output++ = sample;
        }
        frames--;

        // Frames - 8 frames (4 bytes) for each channel
        while (frames > 0) {
            for (int channel = 0; channel < num_channels; channel++) {
                struct ima_state *channel_state = channel_states + channel;
                int16_t *channel_output = output + channel; 
                for (int i = 0; i < 4; i++) {
                    *channel_output = decode_ima_adpcm_nibble(channel_state, (*input) & 0x0f);
                    channel_output += num_channels;
                    *channel_output = decode_ima_adpcm_nibble(channel_state, (*input) >> 4);
                    channel_output += num_channels;
                    input++;
                }
            }
            frames -= 8;
            output += 8 * num_channels;
        }

        remaining_frames -= block_frames;
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = 16;
    
done:
    free(block);
    free(channel_states);
}

struct ms_adpcm_state {
    int32_t coeff1;
    int32_t coeff2;
    uint16_t delta;
    int16_t sample1;
    int16_t sample2;
};

static int16_t decode_ms_adpcm_nibble(struct ms_adpcm_state *channel_state, uint8_t nibble) {
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
static void decode_ms_adpcm_data(pcm_decoder *decoder) {
    static const int adaptation_coeff1[7] = {
        256, 512, 0, 192, 240, 460, 392
    };

    static const int adaptation_coeff2[7] = {
        0, -256, 0, 64, 0, -208, -232
    };

    ok_wav *wav = decoder->wav;
    struct ms_adpcm_state *channel_states = NULL;
    uint8_t *block = NULL;
    const int num_channels = wav->num_channels;
    const bool is_le = wav->little_endian;

    // Allocate buffers
    const uint64_t max_output_frames = (wav->num_frames + 1) & ~1;
    const uint64_t output_data_length = max_output_frames * sizeof(int16_t) * num_channels;
    const size_t platform_data_length = (size_t)output_data_length;
    channel_states = calloc(wav->num_channels, sizeof(struct ms_adpcm_state));
    if (!channel_states) {
        ok_wav_error(wav, "Couldn't allocate channel_state buffer");
        goto done;
    }
    block = malloc(decoder->block_size);
    if (!block) {
        ok_wav_error(wav, "Couldn't allocate block");
        goto done;
    }
    if (platform_data_length > 0 && platform_data_length == output_data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio");
        goto done;
    }

    // Decode
    uint64_t remaining_frames = wav->num_frames;
    int16_t *output = wav->data;
    while (remaining_frames > 0) {
        const int block_frames = (int)min(remaining_frames, decoder->frames_per_block);
        int frames = block_frames;
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
            channel_states[channel].sample1 = (is_le ? readLE16(input) : readBE16(input));
            input += 2;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            channel_states[channel].sample2 = (is_le ? readLE16(input) : readBE16(input));
            input += 2;
        }

        // Initial output (sample2 first)
        for (int channel = 0; channel < num_channels; channel++) {
            *output++ = channel_states[channel].sample2;
        }
        for (int channel = 0; channel < num_channels; channel++) {
            *output++ = channel_states[channel].sample1;
        }
        frames -= 2;

        // Frames (interleaved)
        int samples = frames * num_channels;
        if (num_channels <= 2) {
            struct ms_adpcm_state *channel_state1 = channel_states;
            struct ms_adpcm_state *channel_state2 = channel_states + (num_channels - 1);
            while (samples > 0) {
                *output++ = decode_ms_adpcm_nibble(channel_state1, (*input) >> 4);
                *output++ = decode_ms_adpcm_nibble(channel_state2, (*input) & 0x0f);
                input++;
                samples -= 2;
            }
        } else {
            int channel = 0;
            while (samples > 0) {
                *output++ = decode_ms_adpcm_nibble(channel_states + channel, (*input) >> 4);
                channel = (channel + 1) % num_channels;
                *output++ = decode_ms_adpcm_nibble(channel_states + channel, (*input) & 0x0f);
                channel = (channel + 1) % num_channels;
                input++;
                samples -= 2;
            }
        }

        remaining_frames -= block_frames;
    }

    // Set endian
    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    wav->little_endian = system_is_little_endian;
    wav->bit_depth = 16;

done:
    free(block);
    free(channel_states);
}

static void decode_pcm_data(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    uint64_t data_length = wav->num_frames * wav->num_channels * (wav->bit_depth / 8);
    int platform_data_length = (int)data_length;
    if (platform_data_length > 0 && (size_t)platform_data_length == data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio");
        return;
    }

    if (!ok_read(decoder, wav->data, platform_data_length)) {
        return;
    }

    const int n = 1;
    const bool system_is_little_endian = *(const char *)&n == 1;
    if (decoder->convert_to_system_endian && wav->little_endian != system_is_little_endian &&
        wav->bit_depth > 8) {
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
        wav->little_endian = system_is_little_endian;
    }
}

// MARK: Container file formats (WAV, CAF)

static bool valid_bit_depth(const ok_wav *wav, enum encoding encoding) {
    if (encoding == ENCODING_ULAW || encoding == ENCODING_ALAW) {
        return (wav->bit_depth == 8 && wav->is_float == false);
    } else if (encoding == ENCODING_APPLE_IMA_ADPCM) {
        return wav->is_float == false;
    } else if (encoding == ENCODING_MS_IMA_ADPCM || encoding == ENCODING_MS_ADPCM) {
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

static void decode_data(pcm_decoder *decoder, uint64_t data_length) {
    ok_wav *wav = decoder->wav;
    if (wav->sample_rate <= 0 || wav->num_channels <= 0) {
        ok_wav_error(wav, "Invalid file (header not found)");
        return;
    }

    if (decoder->encoding == ENCODING_APPLE_IMA_ADPCM ||
        decoder->encoding == ENCODING_MS_IMA_ADPCM ||
        decoder->encoding == ENCODING_MS_ADPCM) {
        const uint64_t blocks_needed = ((wav->num_frames + (decoder->frames_per_block - 1)) /
                                        decoder->frames_per_block);
        const uint64_t bytes_needed = blocks_needed * decoder->block_size;
        if (data_length < bytes_needed) {

            ok_wav_error(wav, "Not enough bytes for requested number of frames");
            return;
        }
    } else if (wav->num_frames == 0) {
        if (wav->bit_depth >= 8) {
            wav->num_frames = data_length / ((wav->bit_depth / 8) * wav->num_channels);
        } else {
            ok_wav_error(wav, "Frame length not defined");
            return;
        }
    }
    switch (decoder->encoding) {
        case ENCODING_UNKNOWN:
            // Do nothing
            break;
        case ENCODING_PCM:
            decode_pcm_data(decoder);
            break;
        case ENCODING_ALAW:
            decode_logarithmic_pcm_data(decoder, alaw_table);
            break;
        case ENCODING_ULAW:
            decode_logarithmic_pcm_data(decoder, ulaw_table);
            break;
        case ENCODING_APPLE_IMA_ADPCM:
            decode_apple_ima_adpcm_data(decoder);
            break;
        case ENCODING_MS_IMA_ADPCM:
            decode_ms_ima_adpcm_data(decoder);
            break;
        case ENCODING_MS_ADPCM:
            decode_ms_adpcm_data(decoder);
            break;
    }
}

static void decode_wav_file(pcm_decoder *decoder, bool is_little_endian) {
    ok_wav *wav = decoder->wav;
    wav->little_endian = is_little_endian;

    uint8_t header[8];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }

    bool valid = memcmp("WAVE", header + 4, 4) == 0;
    if (!valid) {
        ok_wav_error(wav, "Not a valid WAV file");
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
                ok_wav_error(wav, "Invalid WAV file (not PCM)");
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
                decoder->encoding = ENCODING_PCM;
            } else if (format == 2) {
                decoder->encoding = ENCODING_MS_ADPCM;
            } else if (format == 3) {
                decoder->encoding = ENCODING_PCM;
                wav->is_float = true;
            } else if (format == 6) {
                decoder->encoding = ENCODING_ALAW;
            } else if (format == 7) {
                decoder->encoding = ENCODING_ULAW;
            } else if (format == 0x11) {
                decoder->encoding = ENCODING_MS_IMA_ADPCM;
            } else {
                decoder->encoding = ENCODING_UNKNOWN;
            }

            if (chunk_length >= 20 && (decoder->encoding == ENCODING_MS_ADPCM ||
                                       decoder->encoding == ENCODING_MS_IMA_ADPCM)) {
                decoder->frames_per_block = (is_little_endian ? readLE16(chunk_data + 18) :
                                             readBE16(chunk_data + 18));
            }

            bool validFormat = (decoder->encoding != ENCODING_UNKNOWN &&
                                valid_bit_depth(wav, decoder->encoding) && wav->num_channels > 0);
            if (!validFormat) {
                ok_wav_error(wav, "Invalid WAV format. Must be PCM, and a bit depth of "
                                  "8, 16, 32, 48, or 64-bit.");
                return;
            }
        } else if (memcmp("fact", chunk_header, 4) == 0) {
            if (chunk_length >= 4) {
                uint8_t chunk_data[4];
                if (!ok_read(decoder, chunk_data, chunk_length)) {
                    return;
                }
                wav->num_frames = is_little_endian ? readLE32(chunk_data) : readBE32(chunk_data);
                chunk_length -= 4;
            }
            if (!ok_seek(decoder, chunk_length)) {
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            decode_data(decoder, chunk_length);
            return;
        } else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, chunk_length)) {
                return;
            }
        }
    }
}

static void decode_caf_file(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }

    uint16_t file_version = readBE16(header);
    if (file_version != 1) {
        ok_wav_error(wav, "Not a CAF file");
        return;
    }

    while (true) {
        // Read chunk type and length
        uint8_t chunk_header[12];
        if (!ok_read(decoder, chunk_header, sizeof(chunk_header))) {
            return;
        }
        const int64_t chunk_length = readBE64(chunk_header + 4);

        if (memcmp("desc", chunk_header, 4) == 0) {
            // Read desc chunk
            if (chunk_length != 32) {
                ok_wav_error(wav, "Corrupt CAF file (bad desc)");
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
                decoder->encoding = ENCODING_PCM;
            } else if (memcmp("ulaw", format_id, 4) == 0) {
                decoder->encoding = ENCODING_ULAW;
            } else if (memcmp("alaw", format_id, 4) == 0) {
                decoder->encoding = ENCODING_ALAW;
            } else if (memcmp("ima4", format_id, 4) == 0) {
                decoder->encoding = ENCODING_APPLE_IMA_ADPCM;
            } else {
                decoder->encoding = ENCODING_UNKNOWN;
            }

            bool valid_bytes_per_packet;
            if (decoder->encoding == ENCODING_APPLE_IMA_ADPCM) {
                if (wav->num_channels > 0) {
                    decoder->frames_per_block = frames_per_packet;
                    decoder->block_size = bytes_per_packet;
                    valid_bytes_per_packet = ((frames_per_packet / 2 + 2) * wav->num_channels ==
                                              decoder->block_size);
                } else {
                    valid_bytes_per_packet = false;
                }
            } else {
                const uint32_t bpp = bytes_per_channel * channels_per_frame;
                valid_bytes_per_packet = (frames_per_packet == 1 && bytes_per_packet == bpp);
            }

            bool valid_format = (decoder->encoding != ENCODING_UNKNOWN &&
                                 (wav->sample_rate > 0) &&
                                 (wav->num_channels > 0) &&
                                 valid_bytes_per_packet &&
                                 (valid_bit_depth(wav, decoder->encoding)));
            if (!valid_format) {
                ok_wav_error(wav, "Invalid CAF format. Must be PCM, mono or stereo, and "
                                  "8-, 16-, 24- or 32-bit.)");
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            if (chunk_length < 4) {
                ok_wav_error(wav, "Corrupt CAF file (bad data)");
                return;
            }
            // Skip the edit count
            if (!ok_seek(decoder, 4)) {
                return;
            }
            // Read the data and return (skip any remaining chunks)
            decode_data(decoder, chunk_length - 4);
            return;
        } else if (memcmp("pakt", chunk_header, 4) == 0) {
            // Read pakt chunk
            if (chunk_length != 24) {
                ok_wav_error(wav, "Corrupt CAF file (bad pakt)");
                return;
            }
            uint8_t chunk_data[24];
            if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                return;
            }

            decoder->wav->num_frames = readBE64(chunk_data + 8);
        } else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, (int)chunk_length)) {
                return;
            }
        }
    }
}

static void decode_file(ok_wav *wav, void *input_data, ok_wav_input_func input_func,
                        bool convert_to_system_endian) {
    if (!wav) {
        return;
    }
    pcm_decoder *decoder = calloc(1, sizeof(pcm_decoder));
    if (!decoder) {
        ok_wav_error(wav, "Couldn't allocate decoder.");
        return;
    }

    decoder->wav = wav;
    decoder->input_data = input_data;
    decoder->input_func = input_func;
    decoder->convert_to_system_endian = convert_to_system_endian;

    uint8_t header[4];
    if (ok_read(decoder, header, sizeof(header))) {
        //printf("File '%.4s'\n", header);
        if (memcmp("RIFF", header, 4) == 0) {
            decode_wav_file(decoder, true);
        } else if (memcmp("RIFX", header, 4) == 0) {
            decode_wav_file(decoder, false);
        } else if (memcmp("caff", header, 4) == 0) {
            decode_caf_file(decoder);
        } else {
            ok_wav_error(wav, "Not a PCM WAV or CAF file.");
        }
    }
    free(decoder);
}
