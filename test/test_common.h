#include <stdbool.h>
#include <stdint.h>

char *append_path(const char *path, const char *name);

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, unsigned long *length);

int file_input_func(void *user_data, uint8_t *buffer, int count);

// Tests if two images are the same.
// If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext,
             const uint8_t *image_data, uint32_t image_width, uint32_t image_height,
             const char *image_error_message,
             const uint8_t *rgba_data, unsigned long rgba_data_length,
             bool info_only, uint8_t fuzziness, bool verbose);
