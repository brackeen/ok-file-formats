#include "ok_jpg.h"
#include "ok_png.h"
#include "test_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

static const int CUSTOM_MEMORY_STRIDE_ALIGNMENT = 512; // To test strides
static const size_t CUSTOM_MEMORY_MAX = 1024 * 1024 * 1024; // No more than 1GB of memory per image

void *custom_alloc(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

void custom_free(void *user_data, void *memory) {
    (void)user_data;
    free(memory);
}

void custom_image_alloc(void *user_data, uint32_t width, uint32_t height, uint8_t bpp,
                        uint8_t **dst_buffer, uint32_t *dst_stride) {
    (void)user_data;
    const int alignment = CUSTOM_MEMORY_STRIDE_ALIGNMENT;
    uint64_t stride = (uint64_t)width * bpp;
    uint64_t aligned_stride = (stride + alignment - 1) / alignment * alignment;
    uint64_t size = aligned_stride * height;
    size_t platform_size = (size_t)size;
    
    if (aligned_stride <= UINT32_MAX && platform_size == size && platform_size <= CUSTOM_MEMORY_MAX) {
        *dst_stride = (uint32_t)aligned_stride;
        *dst_buffer = malloc(platform_size);
    }
}

uint8_t *custom_audio_alloc(void *user_data, uint64_t num_frames, uint8_t num_channels, uint8_t bit_depth) {
    (void)user_data;
    uint64_t size = num_frames * num_channels * (bit_depth / 8);
    size_t platform_size = (size_t)size;
    if (platform_size == 0 || platform_size != size) {
        return NULL;
    }
    return malloc(platform_size);
}

static void print_image(const uint8_t *data, const uint32_t width, const uint32_t height) {
    if (data) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width * 4; x++) {
                if ((x & 3) == 0) {
                    printf("|");
                }
                uint8_t b = data[y * (width * 4) + x];
                printf("%02x", b);
            }
            printf("\n");
        }
    }
}

static void print_image_diff(const uint8_t *data, const uint32_t width, const uint32_t height) {
    if (data) {
        for (uint32_t y = 0; y < height; y++) {
            int max_diff = 0;
            for (uint32_t x = 0; x < width * 4; x++) {
                max_diff = max(max_diff, data[y * (width * 4) + x]);
                if (((x + 1) & 3) == 0) {
                    printf("%01x", min(15, max_diff));
                    max_diff = 0;
                }
            }
            printf("\n");
        }
    }
}

char *append_path(const char *path, const char *name) {
    return get_full_path(path, name, "");
}

bool parent_path(char *path) {
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif

    char *result = strrchr(path, sep);
    if (result) {
        *result = '\0';
        return true;
    } else {
        return false;
    }
}

char *get_full_path(const char *path, const char *name, const char *ext) {
#ifdef _WIN32
    const char *sep = "\\";
#else
    const char *sep = "/";
#endif
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    size_t name_len = strlen(name);
    char *file_name = malloc(path_len + 1 + name_len + 1 + ext_len + 1);
    memcpy(file_name, path, path_len + 1);
    if (path_len > 0 && path[path_len - 1] != sep[0]) {
        strncat(file_name, sep, 1);
    }
    strncat(file_name, name, name_len);
    if (ext_len > 0) {
        strncat(file_name, ".", 1);
        strncat(file_name, ext, ext_len);
    }
    return file_name;
}

uint8_t *read_file(const char *filename, unsigned long *length) {
    uint8_t *buffer;
    FILE *fp = fopen(filename, "rb");

    if (fp) {
        fseek(fp, 0, SEEK_END);
        long tell = ftell(fp);
        if (tell < 0) {
            buffer = NULL;
            *length = 0;
        } else {
            *length = (unsigned long)tell;
            fseek(fp, 0, SEEK_SET);
            buffer = malloc(*length);
            if (buffer) {
                fread(buffer, 1, *length, fp);
            }
        }
        fclose(fp);
    } else {
        buffer = NULL;
        *length = 0;
    }

    return buffer;
}

static bool fuzzy_memcmp(const uint8_t *data1, const uint8_t *data2,
                         size_t pitch1, size_t pitch2,
                         size_t width, size_t height,
                         uint8_t fuzziness, double *p_identical, int *peak_diff) {
    int identical = 0;
    bool success = true;

    *peak_diff = 0;

    for (size_t i = 0; i < height; i++) {
        const uint8_t *row1 = data1 + i * pitch1;
        const uint8_t *row2 = data2 + i * pitch2;
        if (fuzziness == 0) {
            if (memcmp(row1, row2, width) != 0) {
                return false;
            }
        } else {
            const uint8_t *row1_end = row1 + width;
            while (row1 < row1_end) {
                int diff = abs(*row1 - *row2);
                if (diff <= 1) {
                    identical++;
                }
                if (diff > *peak_diff) {
                    *peak_diff = diff;
                }
                if (diff > fuzziness) {
                    success = false;
                }
                row1++;
                row2++;
            }
        }
    }
    *p_identical = (double)identical / (width * height);
    return success;
}

bool compare(const char *name, const char *ext,
             const uint8_t *image_data, uint32_t image_data_stride,
             uint32_t image_width, uint32_t image_height,
             const uint8_t *rgba_data, unsigned long rgba_data_length,
             bool info_only, uint8_t fuzziness, bool verbose) {
    double p_identical;
    int peak_diff;
    bool success = false;
    if (info_only) {
        if (rgba_data_length > 0 && image_width * image_height * 4 != rgba_data_length) {
            printf("Failure: Incorrect dimensions for %s.%s "
                   "(%u x %u - data length should be %lu but is %u)\n",
                   name, ext, image_width, image_height, rgba_data_length,
                   (image_width * image_height * 4));
        } else if (image_data != NULL) {
            printf("Failure: Data incorrectly loaded for %s.%s \n", name, ext);
        } else {
            if (verbose) {
                printf("File:    %16.16s.%s (Info only: %u x %u)\n", name, ext,
                       image_width, image_height);
            }
            success = true;
        }
    } else if (!image_data && (!rgba_data || rgba_data_length == 0)) {
        if (verbose) {
            printf("File:    %16.16s.%s (invalid file correctly detected).\n", name, ext);
        }
        success = true;
    } else if (!image_data) {
        printf("Failure: Couldn't load %s.%s.\n", name, ext);
    } else if (!rgba_data) {
        printf("Warning: Couldn't load %s.rgba. Possibly invalid file.\n", name);
        success = true;
    } else if (image_width * image_height * 4 != rgba_data_length) {
        printf("Failure: Incorrect dimensions for %s.%s "
               "(%u x %u - data length should be %lu but is %u)\n", name, ext,
               image_width, image_height, rgba_data_length, (image_width * image_height * 4));
    } else if (!fuzzy_memcmp(image_data, rgba_data, image_data_stride, image_width * 4,
                             image_width * 4, image_height,
                             fuzziness, &p_identical, &peak_diff)) {
        if (fuzziness > 0) {
            printf("Failure: Data is different for image %s.%s (%f%% diff<=1, peak diff=%i)\n",
                   name, ext, (p_identical * 100.0), peak_diff);
        } else {
            printf("Failure: Data is different for image %s.%s\n", name, ext);
        }
        if (verbose) {
            printf("raw:\n");
            print_image(rgba_data, image_width, image_height);
            printf("as decoded:\n");
            print_image(image_data, image_width, image_height);
            uint8_t *diff_data = malloc(rgba_data_length);
            for (unsigned long i = 0; i < rgba_data_length; i++) {
                diff_data[i] = (uint8_t)abs(rgba_data[i] - image_data[i]);
            }
            printf("diff:\n");
            print_image_diff(diff_data, image_width, image_height);
            free(diff_data);
        }
    } else if (fuzziness > 0) {
        if (verbose) {
            printf("File:    %16.16s.%s (%9.5f%% diff<=1, peak diff=%i)\n", name, ext,
                   (p_identical * 100.0), peak_diff);
        }
        success = true;
    } else {
        if (verbose) {
            printf("File:    %16.16s.%s\n", name, ext);
        }
        success = true;
    }
    return success;
}
