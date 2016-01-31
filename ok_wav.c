#include "ok_wav.h"
#include <memory.h>
#include <stdarg.h>
#include <stdio.h> // For vsnprintf
#include <stdlib.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef struct {
    ok_wav *wav;

    // Decode options
    bool convert_to_system_endian;

    // Input
    void *input_data;
    ok_wav_input_func input_func;

} pcm_decoder;

static void ok_wav_error(ok_wav *wav, const char *format, ...)
    __attribute__((__format__(__printf__, 2, 3)));

static void ok_wav_error(ok_wav *wav, const char *format, ...) {
    if (wav) {
        if (wav->data) {
            free(wav->data);
            wav->data = NULL;
        }
        if (format) {
            va_list args;
            va_start(args, format);
            vsnprintf(wav->error_message, sizeof(wav->error_message), format, args);
            va_end(args);
        }
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

static void decode_pcm(ok_wav *wav, void *input_data, ok_wav_input_func input_func,
                       const bool convert_to_system_endian);

// Public API

ok_wav *ok_wav_read(void *user_data, ok_wav_input_func input_func,
                    const bool convert_to_system_endian) {
    ok_wav *wav = calloc(1, sizeof(ok_wav));
    if (input_func) {
        decode_pcm(wav, user_data, input_func, convert_to_system_endian);
    } else {
        ok_wav_error(wav, "Invalid argument: input_func is NULL");
    }
    return wav;
}

void ok_wav_free(ok_wav *wav) {
    if (wav) {
        if (wav->data) {
            free(wav->data);
            wav->data = NULL;
        }
        free(wav);
    }
}

// Decoding

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

static bool valid_bit_depth(const ok_wav *wav) {
    if (wav->is_float) {
        return (wav->bit_depth == 32 || wav->bit_depth == 64);
    } else {
        return (wav->bit_depth == 8 || wav->bit_depth == 16 ||
                wav->bit_depth == 24 || wav->bit_depth == 32 ||
                wav->bit_depth == 48 || wav->bit_depth == 64);
    }
}
static void decode_pcm_data(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    uint64_t data_length = wav->num_frames * wav->num_channels * (wav->bit_depth / 8);
    int platform_data_length = (int)data_length;
    if (platform_data_length > 0 && (unsigned int)platform_data_length == data_length) {
        wav->data = malloc(platform_data_length);
    }
    if (!wav->data) {
        ok_wav_error(wav, "Couldn't allocate memory for audio with %llu frames",
                     (unsigned long long)wav->num_frames);
        return;
    }

    if (!ok_read(decoder, wav->data, platform_data_length)) {
        return;
    }

    const int n = 1;
    const bool system_is_little_endian = *(char *)&n == 1;
    if (decoder->convert_to_system_endian && wav->little_endian != system_is_little_endian &&
        wav->bit_depth > 8) {
        // Swap data
        uint8_t *data = wav->data;
        const uint8_t *data_end = wav->data + platform_data_length;
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

static void decode_wav(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
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

        uint32_t chunk_length = readLE32(chunk_header + 4);

        if (memcmp("fmt ", chunk_header, 4) == 0) {
            if (chunk_length != 16) {
                ok_wav_error(wav, "Invalid WAV file (not PCM)");
                return;
            }
            uint8_t chunk_data[16];
            if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                return;
            }
            uint16_t format = readLE16(chunk_data);
            wav->num_channels = (uint8_t)readLE16(chunk_data + 2);
            wav->sample_rate = readLE32(chunk_data + 4);
            wav->bit_depth = (uint8_t)readLE16(chunk_data + 14);
            wav->is_float = format == 3;

            bool validFormat = ((format == 1 || format == 3) && valid_bit_depth(wav) &&
                                wav->num_channels > 0);
            if (!validFormat) {
                ok_wav_error(wav, "Invalid WAV format. Must be PCM, and a bit depth of "
                                  "8, 16, 32, 48, or 64-bit.");
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            if (wav->sample_rate <= 0 || wav->num_channels <= 0) {
                ok_wav_error(wav, "Invalid WAV file (fmt not found)");
                return;
            }
            wav->num_frames = chunk_length / ((wav->bit_depth / 8) * wav->num_channels);
            decode_pcm_data(decoder);
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

static void decode_caf(pcm_decoder *decoder) {
    ok_wav *wav = decoder->wav;
    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    uint16_t file_version = readBE16(header);

    if (file_version != 1) {
        ok_wav_error(wav, "Not a CAF file (version %i)", file_version);
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

            bool valid_format = (memcmp("lpcm", format_id, 4) == 0 &&
                                 (sample_rate.value > 0) &&
                                 (channels_per_frame > 0) &&
                                 (bytes_per_packet == bytes_per_channel * channels_per_frame) &&
                                 (frames_per_packet == 1) &&
                                 (valid_bit_depth(wav)));
            if (!valid_format) {
                ok_wav_error(wav, "Invalid CAF format. Must be PCM, mono or stereo, and "
                                  "8-, 16-, 24- or 32-bit.)");
                return;
            }
        } else if (memcmp("data", chunk_header, 4) == 0) {
            // Read data chunk
            if (wav->sample_rate <= 0 || wav->num_channels <= 0) {
                ok_wav_error(wav, "Invalid CAF file (desc not found)");
                return;
            }
            // Skip the edit count
            if (!ok_seek(decoder, 4)) {
                return;
            }
            // Read the data and return (skip any remaining chunks)
            uint64_t data_length = chunk_length - 4;
            wav->num_frames = data_length / ((wav->bit_depth / 8) * wav->num_channels);
            decode_pcm_data(decoder);
            return;
        } else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, (int)chunk_length)) {
                return;
            }
        }
    }
}

static void decode_pcm(ok_wav *wav, void *input_data, ok_wav_input_func input_func,
                       const bool convert_to_system_endian) {
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
            wav->little_endian = true;
            decode_wav(decoder);
        } else if (memcmp("RIFX", header, 4) == 0) {
            wav->little_endian = false;
            decode_wav(decoder);
        } else if (memcmp("caff", header, 4) == 0) {
            decode_caf(decoder);
        } else {
            ok_wav_error(wav, "Not a PCM WAV or CAF file.");
        }
    }
    free(decoder);
}
