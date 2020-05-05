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
 *         ok_wav audio = ok_wav_read(file, OK_WAV_DEFAULT_DECODE_FLAGS);
 *         fclose(file);
 *         if (audio.data) {
 *             printf("Got audio! Length: %f seconds\n", (audio.num_frames / audio.sample_rate));
 *             free(audio.data);
 *         }
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
    uint64_t num_frames;
    uint8_t num_channels;
    uint8_t bit_depth;
    bool is_float;
    bool little_endian;
    ok_wav_error error_code;
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

// MARK: Reading from a FILE

#if !defined(OK_NO_STDIO) && !defined(OK_NO_DEFAULT_ALLOCATOR)

/**
 * Reads a WAV (or CAF) audio file using the default "stdlib" allocator.
 * On success, #ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_code is nonzero.
 *
 * If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit
 * signed integer PCM data.
 *
 * The returned `data` must be freed by the caller (using stdlib's `free()`).
 *
 * @param file The file to read.
 * @param decode_flags The deocde flags. Use #OK_WAV_DEFAULT_DECODE_FLAGS in most cases.
 * @return a #ok_wav object.
 */
ok_wav ok_wav_read(FILE *file, ok_wav_decode_flags decode_flags);

#endif

// MARK: Reading from a FILE, using a custom allocator

typedef struct {
    /**
     * Allocates uninitilized memory.
     *
     * @param user_data The pointer passed to #ok_wav_read_with_allocator.
     * @param size The size of the memory to allocate.
     * @return the pointer to the newly allocated memory, or `NULL` if the memory could not be allocated.
     */
    void *(*alloc)(void *user_data, size_t size);
    
    /**
     * Frees memory previously allocated with `alloc`.
     *
     * @param user_data The pointer passed to #ok_wav_read_with_allocator.
     * @param memory The memory to free.
     */
    void (*free)(void *user_data, void *memory);
    
    /**
     * Allocates memory for the decoded audio.
     * This function may be `NULL`, in which case `alloc` is used instead.
     *
     * @param user_data The pointer passed to #ok_wav_read_with_allocator.
     * @param num_frames The number of audio frames. This value may be slightly larger the the final audio's frame count.
     * @param num_channels The number of audio channels. Usually 1 or 2.
     * @param bit_depth The bit depth of each sample.
     * @return The buffer to output data. The buffer must have a minimum size of
     * (num_frames * num_channels * (bit_depth/8)) bytes. Return `NULL` if the memory could not be allocated.
     */
    uint8_t *(*audio_alloc)(void *user_data, uint64_t num_frames, uint8_t num_channels, uint8_t bit_depth);
} ok_wav_allocator;

#if !defined(OK_NO_DEFAULT_ALLOCATOR)

/// The default allocator using stdlib's `malloc` and `free`.
extern const ok_wav_allocator OK_WAV_DEFAULT_ALLOCATOR;

#endif

#if !defined(OK_NO_STDIO)

/**
 * Reads a WAV  (or CAF) audio file using a custom allocator.
 * On success, #ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_code is nonzero.
 *
 * If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit
 * signed integer PCM data.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param file The file to read.
 * @param decode_flags The WAV decode flags. Use #OK_WAV_DEFAULT_DECODE_FLAGS in most cases.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_WAV_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_wav object.
*/
ok_wav ok_wav_read_with_allocator(FILE *file, ok_wav_decode_flags decode_flags,
                                  ok_wav_allocator allocator, void *allocator_user_data);

#endif

// MARK: Reading from custom input

typedef struct {
    /**
     * Reads bytes from its source (typically `user_data`), copying the data to `buffer`.
     *
     * @param user_data The parameter that was passed to the #ok_wav_read_from_input()
     * @param buffer The data buffer to copy bytes to.
     * @param count The number of bytes to read.
     * @return The number of bytes read.
     */
    size_t (*read)(void *user_data, uint8_t *buffer, size_t count);

    /**
     * Skips bytes from its source (typically `user_data`).
     *
     * @param user_data The parameter that was passed to the #ok_wav_read_from_input().
     * @param count The number of bytes to skip.
     * @return `true` if success.
     */
    bool (*seek)(void *user_data, long count);
} ok_wav_input;

/**
 * Reads a WAV  (or CAF) audio file.
 * On success, #ok_wav.data has a length of `(num_channels * num_frames * (bit_depth/8))`.
 * On failure, #ok_wav.data is `NULL` and #ok_wav.error_code is nonzero.
 *
 * If the encoding of the file is u-law, a-law, or ADPCM, the data is converted to 16-bit
 * signed integer PCM data.
 *
 * The returned `data` must be freed by the caller.
 *
 * @param decode_flags The WAV decode flags. Use #OK_WAV_DEFAULT_DECODE_FLAGS in most cases.
 * @param input_callbacks The custom input functions.
 * @param input_callbacks_user_data The parameter to be passed to the input's `read` and `seek` functions.
 * @param allocator The allocator to use.
 * @param allocator_user_data The pointer to pass to the allocator functions.
 * If using `OK_WAV_DEFAULT_ALLOCATOR`, this value should be `NULL`.
 * @return a #ok_wav object.
 */
ok_wav ok_wav_read_from_input(ok_wav_decode_flags decode_flags,
                              ok_wav_input input_callbacks, void *input_callbacks_user_data,
                              ok_wav_allocator allocator, void *allocator_user_data);

#ifdef __cplusplus
}
#endif

#endif
