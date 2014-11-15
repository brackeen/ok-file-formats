#include <stdint.h>
#include "ok_png.h"

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, long *length);

typedef enum {
    READ_TYPE_FILE = 0,
    READ_TYPE_BUFFER,
    READ_TYPE_COUNT
} read_type;

int file_input_func(void *user_data, unsigned char *buffer, const int count);

ok_image *read_image(const char *path, const char *name, const char *ext, const read_type type, const bool info_only,
                      const ok_color_format color_format, const bool flip_y);

// Tests if two images are the same. If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext, const ok_image *image,
             const uint8_t *rgba_data, const size_t rgba_data_length,
             const bool info_only, const uint8_t fuzziness, const bool print_image_on_error);