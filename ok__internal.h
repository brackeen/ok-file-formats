/**
 Private functions - do not call
 */
#ifndef _OK_INTERNAL_H_
#define _OK_INTERNAL_H_

#include <memory.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifndef _OK_IMAGE_STRUCT_
#define _OK_IMAGE_STRUCT_

typedef enum {
    OK_COLOR_FORMAT_RGBA = 0,
    OK_COLOR_FORMAT_RGBA_PRE,
    OK_COLOR_FORMAT_BGRA,
    OK_COLOR_FORMAT_BGRA_PRE,
} ok_color_format;

typedef struct {
    uint32_t width;
    uint32_t height;
    bool has_alpha;
    uint8_t *data;
    char error_message[80];
} ok_image;

#endif

#ifndef _OK_READ_FUNC_
#define _OK_READ_FUNC_
/// Reads 'count' bytes into buffer. Returns number of bytes read.
typedef size_t (*ok_read_func)(void *user_data, uint8_t *buffer, const size_t count);

/// Seek function. Should return 0 on success.
typedef int (*ok_seek_func)(void *user_data, const int count);
#endif

void ok_image_error(ok_image *image, const char *format, ... );

void ok_image_free(ok_image *image);

typedef struct {
    uint8_t *buffer;
    size_t remaining_bytes;
} ok_memory_source;

size_t ok_memory_read_func(void *user_data, uint8_t *buffer, const size_t count);
int ok_memory_seek_func(void *user_data, const int count);
size_t ok_file_read_func(void *user_data, uint8_t *buffer, const size_t count);
int ok_file_seek_func(void *user_data, const int count);

// Integer reading

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"

static inline uint16_t readBE16(const uint8_t *data) {
    return (uint16_t)((data[0] << 8) | data[1]);
}

static inline uint32_t readBE32(const uint8_t *data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static inline uint64_t readBE64(const uint8_t *data) {
    return
    (((uint64_t)data[0]) << 56) |
    (((uint64_t)data[1]) << 48) |
    (((uint64_t)data[2]) << 40) |
    (((uint64_t)data[3]) << 32) |
    (((uint64_t)data[4]) << 24) |
    (((uint64_t)data[5]) << 16) |
    (((uint64_t)data[6]) << 8) |
    (((uint64_t)data[7]) << 0);
}

static inline uint16_t readLE16(const uint8_t *data) {
    return (uint16_t)((data[1] << 8) | data[0]);
}

static inline uint32_t readLE32(const uint8_t *data) {
    return (data[3] << 24) | (data[2] << 16) | (data[1] << 8) | data[0];
}

static inline uint64_t readLE64(const uint8_t *data) {
    return
    (((uint64_t)data[7]) << 56) |
    (((uint64_t)data[6]) << 48) |
    (((uint64_t)data[5]) << 40) |
    (((uint64_t)data[4]) << 32) |
    (((uint64_t)data[3]) << 24) |
    (((uint64_t)data[2]) << 16) |
    (((uint64_t)data[1]) << 8) |
    (((uint64_t)data[0]) << 0);
}

#pragma clang diagnostic pop

#endif
