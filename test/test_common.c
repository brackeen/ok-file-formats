#include <stdlib.h>
#include <string.h>
#include "test_common.h"
#include "ok_png.h"
#include "ok_jpg.h"

static void print_image(const uint8_t *data, const uint32_t width, const uint32_t height) {
    if (data != NULL) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width * 4; x++) {
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

static size_t file_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    if (count > 0) {
        FILE *fp = (FILE *)user_data;
        return fread(buffer, 1, count, fp);
    }
    return 0;
}

static int file_seek_func(void *user_data, const int count) {
    if (count != 0) {
        FILE *fp = (FILE *)user_data;
        return fseek(fp, count, SEEK_CUR);
    }
    return 0;
}

ok_image *read_image(const char *path, const char *name, const char *ext, const read_type type,
                     const ok_color_format color_format, const bool flip_y) {
    char *in_filename = get_full_path(path, name, ext);
    bool is_png = strcmp("PNG", ext) == 0 || strcmp("png", ext) == 0;
    
    ok_image *image;
    switch(type) {
        case READ_TYPE_FILE: {
            if (is_png) {
                image = ok_png_read(in_filename, color_format, flip_y);
            }
            else {
                image = ok_jpg_read(in_filename, color_format, flip_y);
            }
            break;
        }
        case READ_TYPE_MEMORY: {
            size_t length;
            uint8_t *png_data = read_file(in_filename, &length);
            if (is_png) {
                image = ok_png_read_from_memory(png_data, length, color_format, flip_y);
            }
            else {
                image = ok_jpg_read_from_memory(png_data, length, color_format, flip_y);
            }
            free(png_data);
            break;
        }
        case READ_TYPE_CALLBACKS: {
            FILE *fp = fopen(in_filename, "rb");
            if (is_png) {
                image = ok_png_read_from_callbacks(fp, file_read_func, file_seek_func, color_format, flip_y);
            }
            else {
                image = ok_jpg_read_from_callbacks(fp, file_read_func, file_seek_func, color_format, flip_y);
            }
            fclose(fp);
            break;
        }
        default:
            image = NULL;
            break;
    }
    
    free(in_filename);
    
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
             const uint8_t fuzziness, const bool print_image_on_error) {
    float p_identical;
    int peak_diff;
    bool success = false;
    if (image->data == NULL && rgba_data == NULL) {
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
        printf("Success: %16.16s.%s (%9.5f%% diff<=1, peak diff=%i)\n", name, ext, (p_identical * 100), peak_diff);
        success = true;
    }
    else {
        printf("Success: %s.%s\n", name, ext);
        success = true;
    }
    return success;
}