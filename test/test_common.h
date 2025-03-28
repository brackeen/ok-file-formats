#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void *custom_alloc(void *user_data, size_t size);

void custom_free(void *user_data, void *memory);

void custom_image_alloc(void *user_data, uint32_t width, uint32_t height, uint8_t bpp,
                        uint8_t **dst_buffer, uint32_t *dst_stride);

uint8_t *custom_audio_alloc(void *user_data, uint64_t num_frames, uint8_t num_channels,
                            uint8_t bit_depth);

char *append_path(const char *path, const char *name);

// Converts a path to the parent path. Returns true on success.
bool parent_path(char *path);

char *get_full_path(const char *path, const char *name, const char *ext);

uint8_t *read_file(const char *filename, unsigned long *length);

// Tests if two images are the same.
// If fuzziness > 0, then the diff of the pixel values have to be within (fuzziness).
bool compare(const char *name, const char *ext,
             const uint8_t *image_data, uint32_t image_data_stride,
             uint32_t image_width, uint32_t image_height,
             const uint8_t *rgba_data, unsigned long rgba_data_length,
             bool info_only, uint8_t fuzziness, bool verbose);
