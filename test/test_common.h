#include <stdint.h>
#include "ok_png.h"

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, size_t *length);

typedef enum {
    READ_TYPE_FILE = 0,
    READ_TYPE_MEMORY,
    READ_TYPE_CALLBACKS,
    READ_TYPE_COUNT
} read_type;

ok_image *read_image(const char *path, const char *name, const char *ext, const read_type type,
                      const ok_color_format color_format, const bool flip_y);

// Tests if two images are the same. If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext, const ok_image *image,
             const uint8_t *rgba_data, const size_t rgba_data_length,
             const uint8_t fuzziness, const bool print_image_on_error);