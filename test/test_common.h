#include <stdbool.h>
#include <stdint.h>

char *append_path(const char *path, const char *name);

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, unsigned long *length);

static inline uint32_t align_to(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Tests if two images are the same.
// If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext,
             const uint8_t *image_data, uint32_t image_data_stride,
             uint32_t image_width, uint32_t image_height,
             const uint8_t *rgba_data, unsigned long rgba_data_length,
             bool info_only, uint8_t fuzziness, bool verbose);
