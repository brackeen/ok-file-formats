#include <stdbool.h>
#include <stdint.h>

void *custom_alloc(void *user_data, size_t size);

void custom_free(void *user_data, void *memory);

void custom_image_alloc(void *user_data, uint32_t width, uint32_t height, uint8_t bpp,
                        uint8_t **dst_buffer, uint32_t *dst_stride);

char *append_path(const char *path, const char *name);

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, unsigned long *length);

// Tests if two images are the same.
// If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext,
             const uint8_t *image_data, uint32_t image_data_stride,
             uint32_t image_width, uint32_t image_height,
             const uint8_t *rgba_data, unsigned long rgba_data_length,
             bool info_only, uint8_t fuzziness, bool verbose);
