#include "ok_wav.h"
#include <memory.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

typedef struct {
    ok_audio *audio;
    
    // Decode options
    bool convert_to_system_endian;
    
    // Input
    void *reader_data;
    ok_read_func read_func;
    ok_seek_func seek_func;
    
} pcm_decoder;

__attribute__((__format__ (__printf__, 2, 3)))
static void ok_audio_error(ok_audio *audio, const char *format, ... ) {
    if (audio != NULL) {
        if (audio->data != NULL) {
            free(audio->data);
            audio->data = NULL;
        }
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(audio->error_message, sizeof(audio->error_message), format, args);
            va_end(args);
        }
    }
}

typedef struct {
    uint8_t *buffer;
    size_t remaining_bytes;
} ok_memory_source;

static size_t ok_memory_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    const size_t len = min(count, memory->remaining_bytes);
    if (len > 0) {
        memcpy(buffer, memory->buffer, len);
        memory->buffer += len;
        memory->remaining_bytes -= len;
        return len;
    }
    else {
        return 0;
    }
}

static int ok_memory_seek_func(void *user_data, const int count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    if ((size_t)count <= memory->remaining_bytes) {
        memory->buffer += count;
        memory->remaining_bytes -= count;
        return 0;
    }
    else {
        return -1;
    }
}

static size_t ok_file_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    if (count > 0) {
        FILE *fp = (FILE *)user_data;
        return fread(buffer, 1, count, fp);
    }
    else {
        return 0;
    }
}

static int ok_file_seek_func(void *user_data, const int count) {
    if (count != 0) {
        FILE *fp = (FILE *)user_data;
        return fseek(fp, count, SEEK_CUR);
    }
    else {
        return 0;
    }
}

static bool ok_read(pcm_decoder *decoder, uint8_t *data, const size_t length) {
    if (decoder->read_func(decoder->reader_data, data, length) == length) {
        return true;
    }
    else {
        ok_audio_error(decoder->audio, "Read error: error calling read function.");
        return false;
    }
}

static bool ok_seek(pcm_decoder *decoder, const int length) {
    if (decoder->seek_func(decoder->reader_data, length) == 0) {
        return true;
    }
    else {
        ok_audio_error(decoder->audio, "Read error: error calling seek function.");
        return false;
    }
}

static void decode_pcm(ok_audio *audio, void* reader_data,
                       ok_read_func read_func, ok_seek_func seek_func,
                       const bool convert_to_system_endian);

// Public API

ok_audio *ok_wav_read(const char *file_name, const bool convert_to_system_endian) {
    ok_audio *audio = calloc(1, sizeof(ok_audio));
    if (file_name != NULL) {
        FILE *fp = fopen(file_name, "rb");
        if (fp != NULL) {
            decode_pcm(audio, fp, ok_file_read_func, ok_file_seek_func, convert_to_system_endian);
            fclose(fp);
        }
        else {
            ok_audio_error(audio, "%s", strerror(errno));
        }
    }
    else {
        ok_audio_error(audio, "Invalid argument: file_name is NULL");
    }
    return audio;
}

ok_audio *ok_wav_read_from_file(FILE *fp, const bool convert_to_system_endian) {
    ok_audio *audio = calloc(1, sizeof(ok_audio));
    if (fp != NULL) {
        decode_pcm(audio, fp, ok_file_read_func, ok_file_seek_func, convert_to_system_endian);
    }
    else {
        ok_audio_error(audio, "Invalid argument: file is NULL");
    }
    return audio;
}

ok_audio *ok_wav_read_from_memory(const void *buffer, const size_t buffer_length,
                                  const bool convert_to_system_endian) {
    ok_audio *audio = calloc(1, sizeof(ok_audio));
    if (buffer != NULL) {
        ok_memory_source memory;
        memory.buffer = (uint8_t *)buffer;
        memory.remaining_bytes = buffer_length;
        decode_pcm(audio, &memory, ok_memory_read_func, ok_memory_seek_func, convert_to_system_endian);
    }
    else {
        ok_audio_error(audio, "Invalid argument: buffer is NULL");
    }
    return audio;
}

ok_audio *ok_wav_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                                     const bool convert_to_system_endian) {
    ok_audio *audio = calloc(1, sizeof(ok_audio));
    if (user_data != NULL && read_func != NULL && seek_func != NULL) {
        decode_pcm(audio, user_data, read_func, seek_func, convert_to_system_endian);
    }
    else {
        ok_audio_error(audio, "Invalid argument: read_func or seek_func is NULL");
    }
    return audio;
}

void ok_audio_free(ok_audio *audio) {
    if (audio != NULL) {
        if (audio->data != NULL) {
            free(audio->data);
            audio->data = NULL;
        }
        free(audio);
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
    return
    (((uint64_t)data[0]) << 56) |
    (((uint64_t)data[1]) << 48) |
    (((uint64_t)data[2]) << 40) |
    (((uint64_t)data[3]) << 32) |
    (((uint64_t)data[4]) << 24) |
    (((uint64_t)data[5]) << 16) |
    (((uint64_t)data[6]) << 8) |
    (((uint64_t)data[7]) << 0);
}

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

static void decode_pcm_data(pcm_decoder *decoder) {
    ok_audio *audio = decoder->audio;
    uint64_t data_length = audio->num_frames * audio->num_channels * 2;
    size_t platform_data_length = (size_t)data_length;
    if (platform_data_length == data_length) {
        audio->data = malloc(platform_data_length);
    }
    if (audio->data == NULL) {
        ok_audio_error(audio, "Couldn't allocate memory for audio with %llu frames", audio->num_frames);
        return;
    }
    
    if (!ok_read(decoder, audio->data, platform_data_length)) {
        return;
    }
    
    const int n = 1;
    const bool system_is_little_endian = *(char *)&n == 1;
    if (decoder->convert_to_system_endian && audio->little_endian != system_is_little_endian) {
        // Swap data
        uint8_t *data = audio->data;
        const uint8_t *data_end = audio->data + platform_data_length;
        while (data < data_end) {
            const uint8_t t = data[0];
            data[0] = data[1];
            data[1] = t;
            data += 2;
        }
        audio->little_endian = system_is_little_endian;
    }
}

static void decode_wav(pcm_decoder *decoder) {
    ok_audio *audio = decoder->audio;
    uint8_t header[8];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    
    bool valid = memcmp("WAVE", header + 4, 4) == 0;
    if (!valid) {
        ok_audio_error(audio, "Not a valid WAV file");
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
                ok_audio_error(audio, "Corrupt WAV file (bad fmt)");
                return;
            }
            uint8_t chunk_data[16];
            if (!ok_read(decoder, chunk_data, sizeof(chunk_data))) {
                return;
            }
            uint16_t format = readLE16(chunk_data);
            audio->num_channels = readLE16(chunk_data + 2);
            audio->sample_rate = readLE32(chunk_data + 4);
            int bits_per_sample = readLE16(chunk_data + 14);
            
            bool validFormat = (format == 1 && bits_per_sample == 16 &&
                                (audio->num_channels == 1 || audio->num_channels == 2));
            if (!validFormat) {
                ok_audio_error(audio, "Invalid WAV format (only PCM, 16-bit, integer, mono or stereo supported)");
                return;
            }
        }
        else if (memcmp("data", chunk_header, 4) == 0) {
            if (audio->sample_rate <= 0 || audio->num_channels <= 0) {
                ok_audio_error(audio, "Invalid WAV file (fmt not found)");
                return;
            }
            audio->num_frames = chunk_length / (2 * audio->num_channels);
            decode_pcm_data(decoder);
            return;
        }
        else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, chunk_length)) {
                return;
            }
        }
    }
}

static void decode_caf(pcm_decoder *decoder) {
    ok_audio *audio = decoder->audio;
    uint8_t header[4];
    if (!ok_read(decoder, header, sizeof(header))) {
        return;
    }
    uint16_t file_version = readBE16(header);
    
    if (file_version != 1) {
        ok_audio_error(audio, "Not a CAF file (version %i)", file_version);
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
                ok_audio_error(audio, "Corrupt CAF file (bad desc)");
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
            
            bool validFormat = (memcmp("lpcm", format_id, 4) == 0 &&
                                (sample_rate.value > 0) &&
                                (format_flags == 0 || format_flags == 2) &&
                                (channels_per_frame == 1 || channels_per_frame == 2) &&
                                (bytes_per_packet == 2 * channels_per_frame) &&
                                (frames_per_packet == 1) &&
                                (bits_per_channel == 16));
            if (!validFormat) {
                ok_audio_error(audio, "Invalid CAF format (only PCM, 16-bit, integer, mono or stereo supported)");
                return;
            }

            audio->sample_rate = sample_rate.value;
            audio->num_channels = channels_per_frame;
            audio->little_endian = (format_flags & 2) != 0;
        }
        else if (memcmp("data", chunk_header, 4) == 0) {
            // Read data chunk
            if (audio->sample_rate <= 0 || audio->num_channels <= 0) {
                ok_audio_error(audio, "Invalid CAF file (desc not found)");
                return;
            }
            // Skip the edit count
            if (!ok_seek(decoder, 4)) {
                return;
            }
            // Read the data and return (skip any remaining chunks)
            uint64_t data_length = chunk_length - 4;
            audio->num_frames = data_length / (2 * audio->num_channels);
            decode_pcm_data(decoder);
            return;
        }
        else {
            // Skip ignored chunk
            //printf("Ignoring chunk '%.4s'\n", chunk_header);
            if (!ok_seek(decoder, (int)chunk_length)) {
                return;
            }
        }
    }
}

static void decode_pcm(ok_audio *audio, void* reader_data,
                       ok_read_func read_func, ok_seek_func seek_func,
                       const bool convert_to_system_endian) {
    if (audio == NULL) {
        return;
    }
    pcm_decoder *decoder = calloc(1, sizeof(pcm_decoder));
    if (decoder == NULL) {
        ok_audio_error(audio, "Couldn't allocate decoder.");
        return;
    }
    
    decoder->audio = audio;
    decoder->reader_data = reader_data;
    decoder->read_func = read_func;
    decoder->seek_func = seek_func;
    decoder->convert_to_system_endian = convert_to_system_endian;
    
    uint8_t header[4];
    if (ok_read(decoder, header, sizeof(header))) {
        //printf("File '%.4s'\n", header);
        if (memcmp("RIFF", header, 4) == 0) {
            audio->little_endian = true;
            decode_wav(decoder);
        }
        else if (memcmp("RIFX", header, 4) == 0) {
            audio->little_endian = false;
            decode_wav(decoder);
        }
        else if (memcmp("caff", header, 4) == 0) {
            decode_caf(decoder);
        }
        else {
            ok_audio_error(audio, "Not a PCM WAV or CAF file.");
        }
    }
    free(decoder);
}

