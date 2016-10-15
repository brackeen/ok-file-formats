/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2016 David Brackeen

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
 * Functions to read WAV and CAF files. PCM format only.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_wav.h"
 *
 *     static int file_input_func(void *user_data, uint8_t *buffer, const int count) {
 *         FILE *fp = (FILE *)user_data;
 *         if (buffer && count > 0) {
 *             return (int)fread(buffer, 1, (size_t)count, fp);
 *         } else if (fseek(fp, count, SEEK_CUR) == 0) {
 *             return count;
 *         } else {
 *             return 0;
 *         }
 *     }
 *
 *     int main() {
 *         FILE *fp = fopen("my_audio.caf", "rb");
 *         ok_wav *audio = ok_wav_read(fp, file_input_func, true);
 *         fclose(fp);
 *         if (audio->data) {
 *             printf("Got audio! Length: %f seconds\n", (audio->num_frames / audio->sample_rate));
 *         }
 *         ok_wav_free(audio);
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>

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

/**
 * Input function provided to the #ok_wav_read() function.
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_wav_read() function.
 * @param buffer The data buffer to copy bytes to. If `NULL`, this function should perform a
 * relative seek.
 * @param count The number of bytes to read. If negative, this function should perform a
 * relative seek.
 * @return The number of bytes read or skipped. Should return 0 on error.
 */
typedef int (*ok_wav_input_func)(void *user_data, uint8_t *buffer, int count);

/**
 * Reads a WAV (or CAF) audio file.
 * On success, #ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 *
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_message is set.
 *
 * @param user_data The parameter to be passed to the `input_func`.
 * @param input_func The input function to read a WAV file from.
 * @param convert_to_system_endian If true, the data is converted to the endianness of the system 
 * (required for OpenAL). Otherwise, the data is left as is.
 * @return a new #ok_wav object. Never returns `NULL`. The object should be freed with
 * #ok_wav_free().
 */
ok_wav *ok_wav_read(void *user_data, ok_wav_input_func input_func, bool convert_to_system_endian);

/**
 * Frees the audio. This function should always be called when done with the audio, even if reading 
 * failed.
 */
void ok_wav_free(ok_wav *wav);

#ifdef __cplusplus
}
#endif

#endif
