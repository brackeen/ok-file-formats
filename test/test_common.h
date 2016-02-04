#include <stdbool.h>
#include <stdint.h>

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, unsigned long *length);

int file_input_func(void *user_data, uint8_t *buffer, int count);

// Tests if two images are the same.
// If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext,
             const uint8_t *image_data, const uint32_t image_width, const uint32_t image_height,
             const char *image_error_message,
             const uint8_t *rgba_data, const unsigned long rgba_data_length,
             const bool info_only, const uint8_t fuzziness, const bool print_image_on_error);
