#include "ok__internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

void ok_image_error(ok_image *image, const char *format, ... ) {
    if (image != NULL) {
        image->width = 0;
        image->height = 0;
        if (image->data != NULL) {
            free(image->data);
            image->data = NULL;
        }
        if (format != NULL) {
            va_list args;
            va_start(args, format);
            vsnprintf(image->error_message, sizeof(image->error_message), format, args);
            va_end(args);
        }
    }
}

void ok_image_free(ok_image *image) {
    if (image != NULL) {
        if (image->data != NULL) {
            free(image->data);
            image->data = NULL;
        }
        free(image);
    }
}

size_t ok_memory_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    const size_t len = min(count, memory->remaining_bytes);
    if (len > 0) {
        memcpy(buffer, memory->buffer, len);
        memory->buffer += len;
        memory->remaining_bytes -= len;
        return len;
    }
    else {
        return 0;
    }
}

int ok_memory_seek_func(void *user_data, const int count) {
    ok_memory_source *memory = (ok_memory_source*)user_data;
    if ((size_t)count <= memory->remaining_bytes) {
        memory->buffer += count;
        memory->remaining_bytes -= count;
        return 0;
    }
    else {
        return -1;
    }
}

size_t ok_file_read_func(void *user_data, uint8_t *buffer, const size_t count) {
    if (count > 0) {
        FILE *fp = (FILE *)user_data;
        return fread(buffer, 1, count, fp);
    }
    else {
        return 0;
    }
}

int ok_file_seek_func(void *user_data, const int count) {
    if (count != 0) {
        FILE *fp = (FILE *)user_data;
        return fseek(fp, count, SEEK_CUR);
    }
    else {
        return 0;
    }
}