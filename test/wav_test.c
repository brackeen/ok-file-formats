#include "wav_test.h"
#include "ok_wav.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

enum wav_test_type {
    test_normal,
    test_allocator,
};

static void print_diff(const uint8_t *data1, const uint8_t *data2, const unsigned long length) {
    printf("Expected:                                         Actual:\n");
    if (data1 && data2) {
        unsigned long i = 0;
        while (i < length) {
            printf("\x1B[39m"); // Default color
            unsigned long count = min(length - i, 16);
            for (unsigned long j = 0; j < count; j++) {
                uint8_t b = data1[i + j];
                printf("%02x ", b);
            }
            for (unsigned long j = count; j < 16; j++) {
                printf("   ");
            }
            printf("| ");
            for (unsigned long j = 0; j < count; j++) {
                uint8_t b = data2[i + j];
                if (b == data1[i + j]) {
                    printf("\x1B[39m"); // Default color
                } else {
                    printf("\x1B[31m"); // Red
                }
                printf("%02x ", b);
            }
            printf("\n");
            i += count;
        }
        printf("\x1B[39m"); // Default color
    }
}

static bool test_wav(const char *path, const char *container_type, const char *format,
                     int channels, enum wav_test_type test_type, bool verbose) {
    char src_filename[256];
    bool success = false;

    // Load ok_wav
    snprintf(src_filename, sizeof(src_filename), "sound-%s-%dch", format, channels);
    char *src_path = get_full_path(path, src_filename, container_type);
    FILE *file = fopen(src_path, "rb");
    if (!file) {
        printf("Warning: %24.24s.%s not found.\n", src_filename, container_type);
        return true;
    }
    const ok_wav_allocator allocator = {
        .alloc = custom_alloc,
        .free = custom_free,
        .audio_alloc = custom_audio_alloc
    };
    
    ok_wav wav = { 0 };
    switch (test_type) {
        case test_normal:
            wav = ok_wav_read(file, OK_WAV_ENDIAN_NO_CONVERSION);
            break;
        case test_allocator:
            wav = ok_wav_read_with_allocator(file, OK_WAV_ENDIAN_NO_CONVERSION, allocator, NULL);
            break;
    }
    fclose(file);
    free(src_path);

    if (!wav.data) {
        printf("File:    %24.24s.%s (Couldn't load data. Error %i).\n", src_filename,
               container_type, wav.error_code);
    } else {
        // Load raw
        char *raw_path = get_full_path(path, src_filename, "raw");
        unsigned long expected_length = 0;
        void *raw_data = read_file(raw_path, &expected_length);
        free(raw_path);

        // Compare
        unsigned long length = (unsigned long)(wav.num_frames * wav.num_channels *
                                               (wav.bit_depth / 8));

        if (expected_length != length) {
            printf("File:    %24.24s.%s (Invalid data length: Expected %lu, got %lu).\n",
                   src_filename, container_type, expected_length, length);
        } else {
            success = memcmp(wav.data, raw_data, length) == 0;
            if (!success) {
                printf("File:    %24.24s.%s (Data mismatch).\n", src_filename, container_type);
                if (verbose) {
                    print_diff(raw_data, wav.data, length);
                }
            }
        }
        free(raw_data);
        free(wav.data);
    }

    return success;
}

int wav_test(const char *path, bool verbose) {
    const int channels[] = { 1, 2 };
    const char *caf_data_formats[] = {
        "I8", "ulaw", "alaw", "ima4",
        "BEI16", "BEI24", "BEI32", "BEF32", "BEF64",
        "LEI16", "LEI24", "LEI32", "LEF32", "LEF64" };
    const char *wav_data_formats[] = {
        "UI8", "ulaw-wav", "alaw-wav", "ima-adpcm", "ms-adpcm",
        "BEI16", "BEI24", "BEI32", "BEF32", "BEF64",
        "LEI16", "LEI24", "LEI32", "LEF32", "LEF64" };

    const int num_channels = sizeof(channels) / sizeof(channels[0]);
    const int num_caf_types = sizeof(caf_data_formats) / sizeof(caf_data_formats[0]);
    const int num_wav_types = sizeof(wav_data_formats) / sizeof(wav_data_formats[0]);

    const int num_files = (num_caf_types + num_wav_types) * num_channels;
    if (verbose) {
        printf("Testing %i files in path \"%s\".\n", num_files, path);
    }

    double startTime = clock() / (double)CLOCKS_PER_SEC;
    int num_failures = 0;
    for (int i = 0; i < num_channels; i++) {
        bool success;
        for (int j = 0; j < num_caf_types; j++) {
            success = test_wav(path, "caf", caf_data_formats[j], channels[i], test_normal, verbose);
            if (!success) {
                num_failures++;
            }
            success = test_wav(path, "caf", caf_data_formats[j], channels[i], test_allocator, verbose);
            if (!success) {
                num_failures++;
            }
        }

        for (int j = 0; j < num_wav_types; j++) {
            success = test_wav(path, "wav", wav_data_formats[j], channels[i], test_normal, verbose);
            if (!success) {
                num_failures++;
            }
            success = test_wav(path, "wav", wav_data_formats[j], channels[i], test_allocator, verbose);
            if (!success) {
                num_failures++;
            }
        }
    }

    double endTime = clock() / (double)CLOCKS_PER_SEC;
    double elapsedTime = endTime - startTime;
    printf("Success: WAV %i of %i\n", (num_files - num_failures), num_files);
    if (verbose) {
        printf("Duration: %f seconds\n", elapsedTime);
    }

#if 0
    char *test_path = append_path(path, "../../wav/issue-21.wav");
    FILE *file = fopen(test_path, "rb");
    if (!file) {
        printf("Not found: %s\n", test_path);
    } else {
        printf("Testing %s\n", test_path);
        ok_wav wav = ok_wav_read(file, OK_WAV_ENDIAN_NO_CONVERSION);
        fclose(file);
        free(wav.data);
    }
    free(test_path);

#endif

    return num_failures;
}
