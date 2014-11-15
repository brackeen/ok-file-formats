#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_common.h"
#include "ok_png.h"
#include "ok_jpg.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

static void print_image(const uint8_t *data, const uint32_t width, const uint32_t height) {
    if (data != NULL) {
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width * 4; x++) {
                if ((x & 3) == 0) {
                    printf("|");
                }
                uint8_t b = data[y * (width*4) + x];
                printf("%02x", b);
            }
            printf("\n");
        }
    }
}

char *get_full_path(const char *path, const char *name, const char *ext) {
    size_t path_len = strlen(path);
    char *file_name = malloc(path_len + 1 + strlen(name) + 1 + strlen(ext) + 1);
    strcpy(file_name, path);
    if (path_len > 0 && path[path_len - 1] != '/') {
        strcat(file_name, "/");
    }
    strcat(file_name, name);
    strcat(file_name, ".");
    strcat(file_name, ext);
    return file_name;
}

uint8_t *read_file(const char *filename, size_t *length) {
    uint8_t *buffer;
    FILE *fp = fopen(filename, "rb");
    
    if (fp != NULL) {
        fseek(fp, 0, SEEK_END);
        *length = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        buffer = malloc(*length);
        if (buffer != NULL) {
            fread(buffer, 1, *length, fp);
        }
        fclose(fp);
    }
    else {
        buffer = NULL;
        *length = 0;
    }
    
    return buffer;
}

size_t file_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    if (count > 0) {
        FILE *fp = (FILE *)user_data;
        return fread(buffer, 1, count, fp);
    }
    else {
        return 0;
    }
}

int file_seek_func(void *user_data, const int count) {
    if (count != 0) {
        FILE *fp = (FILE *)user_data;
        return fseek(fp, count, SEEK_CUR);
    }
    else {
        return 0;
    }
}

typedef struct {
    uint8_t *buffer;
    size_t remaining_bytes;
} buffer_source;

static size_t buffer_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    buffer_source *source = (buffer_source *)user_data;
    const size_t len = min(count, source->remaining_bytes);
    if (len > 0) {
        memcpy(buffer, source->buffer, len);
        source->buffer += len;
        source->remaining_bytes -= len;
        return len;
    }
    else {
        return 0;
    }
}

static int buffer_seek_func(void *user_data, const int count) {
    buffer_source *source = (buffer_source *)user_data;
    if ((size_t)count <= source->remaining_bytes) {
        source->buffer += count;
        source->remaining_bytes -= count;
        return 0;
    }
    else {
        return -1;
    }
}

ok_image *read_image(const char *path, const char *name, const char *ext, const read_type type, const bool info_only,
                     const ok_color_format color_format, const bool flip_y) {
    ok_image *image = NULL;
    char *in_filename = get_full_path(path, name, ext);
    bool is_png = strcmp("PNG", ext) == 0 || strcmp("png", ext) == 0;
    
    FILE *fp = NULL;
    uint8_t *png_data = NULL;
    buffer_source source;
    void *user_data;
    ok_read_func read_func;
    ok_seek_func seek_func;

    // Open
    switch (type) {
        case READ_TYPE_FILE:
            fp = fopen(in_filename, "rb");
            user_data = fp;
            read_func = file_read_func;
            seek_func = file_seek_func;
            break;
            
        case READ_TYPE_BUFFER:
            png_data = read_file(in_filename, &source.remaining_bytes);
            source.buffer = png_data;
            user_data = &source;
            read_func = buffer_read_func;
            seek_func = buffer_seek_func;
            break;
            
        default:
            free(in_filename);
            return NULL;
    }
    
    // Read
    if (is_png) {
        if (info_only) {
            image = ok_png_read_info(user_data, read_func, seek_func);
        }
        else {
            image = ok_png_read(user_data, read_func, seek_func, color_format, flip_y);
        }
    }
    else {
        if (info_only) {
            image = ok_jpg_read_info(user_data, read_func, seek_func);
        }
        else {
            image = ok_jpg_read(user_data, read_func, seek_func, color_format, flip_y);
        }
    }
    
    // Close
    free(in_filename);
    if (fp) {
        fclose(fp);
    }
    if (png_data) {
        free(png_data);
    }
    return image;
}

static bool fuzzy_memcmp(const uint8_t *data1, const uint8_t *data2, const size_t length,
                         const uint8_t fuzziness, float *p_identical, int *peak_diff) {
    if (fuzziness == 0) {
        return memcmp(data1, data2, length) == 0;
    }
    else {
        *peak_diff = 0;
        bool success = true;
        int identical = 0;
        const uint8_t *data1_end = data1 + length;
        while (data1 < data1_end) {
            int diff = abs(*data1 - *data2);
            if (diff <= 1) {
                identical++;
            }
            if (diff > *peak_diff) {
                *peak_diff = diff;
            }
            if (diff > fuzziness) {
                success = false;
            }
            data1++;
            data2++;
        }
        *p_identical = (float)identical / length;
        return success;
    }
}

bool compare(const char *name, const char *ext, const ok_image *image,
             const uint8_t *rgba_data, const size_t rgba_data_length,
             const bool info_only, const uint8_t fuzziness, const bool print_image_on_error) {
    float p_identical;
    int peak_diff;
    bool success = false;
    if (info_only) {
        if (image->width * image->height * 4 != rgba_data_length) {
            printf("Failure: Incorrect dimensions for %s.%s (%u x %u - data length should be %u but is %zu)\n",
                   name, ext, image->width, image->height, (image->width * image->height * 4), rgba_data_length);
        }
        else {
            printf("Success: %14.14s.%s (Info only: %u x %u)\n", name, ext, image->width, image->height);
            success = true;
        }
    }
    else if (image->data == NULL && rgba_data == NULL) {
        printf("Success (invalid file correctly detected): %s.%s. Error: %s\n", name, ext, image->error_message);
        success = true;
    }
    else if (image->data == NULL) {
        printf("Failure: Couldn't load %s.%s. %s\n", name, ext, image->error_message);
    }
    else if (rgba_data == NULL) {
        printf("Failure: Couldn't load %s (raw rgba data)\n", name);
    }
    else if (image->width * image->height * 4 != rgba_data_length) {
        printf("Failure: Incorrect dimensions for %s.%s (%u x %u - data length should be %u but is %zu)\n", name, ext,
               image->width, image->height, (image->width * image->height * 4), rgba_data_length);
    }
    else if (!fuzzy_memcmp(image->data, rgba_data, rgba_data_length, fuzziness, &p_identical, &peak_diff)) {
        if (fuzziness > 0) {
            printf("Failure: Data is different for image %s.%s (%f%% diff<=1, peak diff=%i)\n",
                   name, ext, (p_identical * 100), peak_diff);
        }
        else {
            printf("Failure: Data is different for image %s.%s\n", name, ext);
        }
        if (print_image_on_error) {
            printf("raw:\n");
            print_image(rgba_data, image->width, image->height);
            printf("as decoded:\n");
            print_image(image->data, image->width, image->height);
        }
    }
    else if (fuzziness > 0) {
        printf("Success: %14.14s.%s (%9.5f%% diff<=1, peak diff=%i)\n", name, ext, (p_identical * 100), peak_diff);
        success = true;
    }
    else {
        printf("Success: %14.14s.%s\n", name, ext);
        success = true;
    }
    return success;
}