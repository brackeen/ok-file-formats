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

#ifndef OK_WAV_H
#define OK_WAV_H

/**
 * @file
 * Functions to read WAV and CAF files.
 *
 * Supported encodings:
 *  * PCM (including floating-point).
 *  * Both u-law and a-law.
 *  * CAF: Apple's IMA ADPCM.
 *  * WAV: Microsoft's IMA ADPCM.
 *  * WAV: Microsoft's ADPCM.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_wav.h"
 *
 *     int main() {
 *         FILE *file = fopen("my_audio.wav", "rb");
 *         ok_wav *audio = ok_wav_read(file, true);
 *         fclose(file);
 *         if (audio->data) {
 *             printf("Got audio! Length: %f seconds\n", (audio->num_frames / audio->sample_rate));
 *         }
 *         ok_wav_free(audio);
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>
#ifndef OK_NO_STDIO
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OK_WAV_SUCCESS = 0,
    OK_WAV_ERROR_API, // Invalid argument sent to public API function
    OK_WAV_ERROR_INVALID, // Not a valid WAV file
    OK_WAV_ERROR_UNSUPPORTED, // Unsupported WAV file (Not PCM, u-law, a-law, or ADPCM)
    OK_WAV_ERROR_ALLOCATION, // Couldn't allocate memory
    OK_WAV_ERROR_IO, // Couldn't read or seek the file
} ok_wav_error;

/**
 * The data returned from #ok_wav_read().
 */
typedef struct {
    double sample_rate;
    uint8_t num_channels;
    uint8_t bit_depth;
    bool is_float;
    bool little_endian;
    ok_wav_error error_code;
    uint64_t num_frames;
    void *data;
} ok_wav;

/**
 * Decode flags.
 */
typedef enum {
    // Perform no endian conversion
    OK_WAV_ENDIAN_NO_CONVERSION = 0,
    /// Convert to native endian
    OK_WAV_ENDIAN_NATIVE = 1,
    /// Convert to little endian
    OK_WAV_ENDIAN_LITTLE = 2,
    /// Convert to big endian
    OK_WAV_ENDIAN_BIG = 3,
} ok_wav_decode_flags;

static const ok_wav_decode_flags OK_WAV_DEFAULT_DECODE_FLAGS = OK_WAV_ENDIAN_NATIVE;

#ifndef OK_NO_STDIO

/**
 * Reads a WAV (or CAF) audio file.
 * On success, ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 *
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_message is set.
 *
 * If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit
 * signed integer PCM data.
 *
 * @param file The file to read.
 * @param decode_flags The deocde flags. Use #OK_WAV_DEFAULT_DECODE_FLAGS in most cases.
 * @return a new #ok_wav object. Never returns `NULL`. The object should be freed with
 * #ok_wav_free().
 */
ok_wav *ok_wav_read(FILE *file, ok_wav_decode_flags decode_flags);

#endif

/**
 * Frees the audio. This function should always be called when done with the audio, even if reading
 * failed.
 */
void ok_wav_free(ok_wav *wav);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_wav_read_from_callbacks() function.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_wav_read_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_wav_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Seek function provided to the #ok_wav_read_from_callbacks() function.
 * This function must skip bytes from its source (typically `user_data`).
 *
 * @param user_data The parameter that was passed to the #ok_wav_read_from_callbacks() function.
 * @param count The number of bytes to skip.
 * @return `true` if success.
 */
typedef bool (*ok_wav_seek_func)(void *user_data, long count);

/**
 * Reads a WAV (or CAF) audio file from the provided callback functions.
 * On success, ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 *
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_message is set.
 *
 * If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit
 * signed integer PCM data.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @param decode_flags The deocde flags. Use #OK_WAV_DEFAULT_DECODE_FLAGS in most cases.
 * @return a new #ok_wav object. Never returns `NULL`. The object should be freed with
 * #ok_wav_free().
 */
ok_wav *ok_wav_read_from_callbacks(void *user_data, ok_wav_read_func read_func,
                                   ok_wav_seek_func seek_func, ok_wav_decode_flags decode_flags);

#ifdef __cplusplus
}
#endif

#endif
