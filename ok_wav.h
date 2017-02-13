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

/**
 * The data returned from #ok_wav_read().
 */
typedef struct {
    double sample_rate;
    uint8_t num_channels;
    uint8_t bit_depth;
    bool is_float;
    bool little_endian;
    uint64_t num_frames;
    void *data;
    char error_message[80];
} ok_wav;

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
 * @param convert_to_system_endian If true, the data is converted to the endianness of the system
 * (required for playback on most systems). Otherwise, the data is left as is.
 * @return a new #ok_wav object. Never returns `NULL`. The object should be freed with
 * #ok_wav_free().
 */
ok_wav *ok_wav_read(FILE *file, bool convert_to_system_endian);

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
 * @param convert_to_system_endian If true, the data is converted to the endianness of the system
 * (required for playback on most systems). Otherwise, the data is left as is.
 * @return a new #ok_wav object. Never returns `NULL`. The object should be freed with
 * #ok_wav_free().
 */
ok_wav *ok_wav_read_from_callbacks(void *user_data, ok_wav_read_func read_func,
                                   ok_wav_seek_func seek_func, bool convert_to_system_endian);

#ifdef __cplusplus
}
#endif

#endif
