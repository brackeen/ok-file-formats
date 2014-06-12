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
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
    
    /** 16-bit PCM audio data. The length of the data is (num_channels * num_frames * 2) */
    typedef struct {
        float sample_rate;
        uint8_t num_channels;
        uint64_t num_frames;
        bool little_endian;
        uint8_t *data;
        char error_message[80];
    } ok_audio;

#ifndef _OK_READ_FUNC_
#define _OK_READ_FUNC_
    /// Reads 'count' bytes into buffer. Returns number of bytes read.
    typedef size_t (*ok_read_func)(void *user_data, uint8_t *buffer, const size_t count);
    
    /// Seek function. Should return 0 on success.
    typedef int (*ok_seek_func)(void *user_data, const int count);
#endif
    
    /**
     Reads a WAV (or CAF) audio file (PCM format only). If convert_to_system_endian is true, the data is converted
     to the endianness of the system (needed for OpenAL), otherwise, the data is left as is. 
     
     If an error occurs, data is NULL.
     */
    ok_audio *ok_wav_read(const char *file_name, const bool convert_to_system_endian);
    ok_audio *ok_wav_read_from_file(FILE *file, const bool convert_to_system_endian);
    ok_audio *ok_wav_read_from_memory(const void *buffer, const size_t buffer_length,
                                      const bool convert_to_system_endian);
    ok_audio *ok_wav_read_from_callbacks(void *user_data, ok_read_func read_func, ok_seek_func seek_func,
                                         const bool convert_to_system_endian);
    
    /**
     Frees the audio. This function should always be called when done with the audio, even if reading failed.
     */
    void ok_audio_free(ok_audio *audio);

#ifdef __cplusplus
}
#endif

#endif
