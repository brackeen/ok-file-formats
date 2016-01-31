/*
 ok-file-formats
 Copyright (c) 2014 David Brackeen
 
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

#ifndef _OK_WAV_H_
#define _OK_WAV_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** PCM audio data. The length of the data is (num_channels * num_frames * (bit_depth/8)) */
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
 * Input function provided to the ok_wav_read function.
 * Reads 'count' bytes into buffer. Returns number of bytes actually read.
 * If buffer is NULL or 'count' is negative, this function should perform a relative seek.
 */
typedef int (*ok_wav_input_func)(void *user_data, unsigned char *buffer, const int count);

/**
 * Reads a WAV (or CAF) audio file (PCM format only). If convert_to_system_endian is true, the
 * data is converted to the endianness of the system (needed for OpenAL), otherwise, the data is
 * left as is.
 *
 * If an error occurs, data is NULL.
 */
ok_wav *ok_wav_read(void *user_data, ok_wav_input_func input_func,
                    const bool convert_to_system_endian);

/**
 * Frees the audio. This function should always be called when done with the audio, even if reading 
 * failed.
 */
void ok_wav_free(ok_wav *wav);

#ifdef __cplusplus
}
#endif

#endif
