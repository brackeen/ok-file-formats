/*
 ok-file-formats
 https://github.com/brackeen/ok-file-formats
 Copyright (c) 2014-2017 David Brackeen

 This software is provided 'as-is', without any express or implied warranty.
 In no event will the authors be held liable for any damages arising from the
 use of this software. Permission is granted to anyone to use this software
 for any purpose, including commercial applications, and to alter it and
 redistribute it freely, subject to the following restrictions:
 
 1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software in a
    product, an acknowledgment in the product documentation would be appreciated
    but is not required.
 2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.
 3. This notice may not be removed or altered from any source distribution.
 */

#ifndef OK_MO_H
#define OK_MO_H

/**
 * @file
 * Functions to read GNU gettext MO files. Includes UTF-8 support.
 *
 * Example:
 *
 *     #include <stdio.h>
 *     #include "ok_mo.h"

 *     int main() {
 *         FILE *file = fopen("my_strings.mo", "rb");
 *         ok_mo *mo = ok_mo_read(file);
 *         fclose(file);
 *         if (mo->num_strings > 0) {
 *             printf("Got MO! %i strings\n", mo->num_strings);
 *             printf("Value for 'Hello': '%s'\n", ok_mo_value(mo, "Hello"));
 *         }
 *         ok_mo_free(mo);
 *         return 0;
 *     }
 */

#include <stdbool.h>
#include <stdint.h>
#ifndef OK_NO_STDIO
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// Internal storage of key-value data.
struct ok_mo_string;

/**
 * The data returned from #ok_mo_read().
 */
typedef struct {
    uint32_t num_strings;
    struct ok_mo_string *strings;
    const char *error_message;
} ok_mo;

#ifndef OK_NO_STDIO

/**
 * Reads a MO file.
 * On failure, #ok_mo.num_strings is 0 and #ok_mo.error_message is set.
 *
 * @param file The file to read.
 * @return A new #ok_mo object. Never returns `NULL`. The object should be freed with
 * #ok_mo_free().
 */
ok_mo *ok_mo_read(FILE *file);

#endif

/**
 * Gets the value for the specified key.
 * @param mo The mo object.
 * @param key The key string to search for.
 * @return The value for the key. If there is no value for `key`, then `key` is returned.
 */
const char *ok_mo_value(ok_mo *mo, const char *key);

/**
 * Gets the value for the specified key, with possible plural variants.
 * If there are plural variants, returns the plural variant for the specified n value.
 * @param mo The mo object.
 * @param key The key string to search for.
 * @param plural_key The value to return if the value for the key is not found, and `n` is not 1.
 * @param n The grammatical number. In English, the grammatical number categories are "singular" 
 * (`n` is 1) and "plural" (`n` is not 1).
 * @return The value for the key. If there is no value for `key`, then either `key` or `plural_key`
 * is returned, depending on the value of `n`.
 */
const char *ok_mo_plural_value(ok_mo *mo, const char *key, const char *plural_key, int n);

/**
 * Gets the value for the specified key in a context.
 * @param mo The mo object.
 * @param context The key context. For example, the context for "open" may be "file" or "window".
 * @param key The key string to search for.
 * @return The value for the key. If there is no value for `key`, then `key` is returned.
 */
const char *ok_mo_value_in_context(ok_mo *mo, const char *context, const char *key);

/**
 * Gets the value for the specified key in a context, with possible plural variants.
 * @param mo The mo object.
 * @param context The key context. For example, the context for "open" may be "file" or "window".
 * @param key The key string to search for.
 * @param plural_key The value to return if the value for the key is not found, and `n` is not 1.
 * @param n The grammatical number. In English, the grammatical number categories are "singular"
 * (`n` is 1) and "plural" (`n` is not 1).
 * @return The value for the key. If there is no value for `key`, then either `key` or `plural_key`
 * is returned, depending on the value of `n`.
 */
const char *ok_mo_plural_value_in_context(ok_mo *mo, const char *context, const char *key,
                                          const char *plural_key, int n);

/**
 * Frees the MO data. This function should always be called when finished with the MO data, even if
 * reading failed.
 */
void ok_mo_free(ok_mo *mo);

/**
 * Gets the character length (as opposed to the byte length) of an UTF-8 string.
 * @param utf8 A UTF-8 encoded string, with a `NULL` terminator.
 */
unsigned int ok_utf8_strlen(const char *utf8);

/**
 * Converts the first `n` characters of a UTF-8 string to a 32-bit Unicode (UCS-4) string.
 * @param utf8 A UTF-8 encoded string.
 * @param dest The destination buffer. The buffer must have a length of at least (n + 1) to
 * accommodate the `NULL` terminator.
 * @param n The number of Unicode characters to convert.
 * @return The number of characters copied, excluding the `NULL` termintor. The number may be 
 * shorter than `n` if the end of the UTF-8 string was found.
 */
unsigned int ok_utf8_to_unicode(const char *utf8, uint32_t *dest, unsigned int n);

// MARK: Read from callbacks

/**
 * Read function provided to the #ok_mo_read_from_callbacks() function.
 *
 * This function must read bytes from its source (typically `user_data`) and copy the data to
 * `buffer`.
 *
 * @param user_data The parameter that was passed to the #ok_mo_read_from_callbacks() function.
 * @param buffer The data buffer to copy bytes to.
 * @param count The number of bytes to read.
 * @return The number of bytes read.
 */
typedef size_t (*ok_mo_read_func)(void *user_data, uint8_t *buffer, size_t count);

/**
 * Seek function provided to the #ok_mo_read_from_callbacks() function.
 *
 * This function must skip bytes from its source (typically `user_data`).
 *
 * @param user_data The parameter that was passed to the #ok_mo_read_from_callbacks() function.
 * @param count The number of bytes to skip.
 * @return `true` if success.
 */
typedef bool (*ok_mo_seek_func)(void *user_data, long count);

/**
 * Reads a MO file.
 * On failure, #ok_mo.num_strings is 0 and #ok_mo.error_message is set.
 *
 * @param user_data The parameter to be passed to `read_func` and `seek_func`.
 * @param read_func The read function.
 * @param seek_func The seek function.
 * @return A new #ok_mo object. Never returns `NULL`. The object should be freed with
 * #ok_mo_free().
 */
ok_mo *ok_mo_read_from_callbacks(void *user_data, ok_mo_read_func read_func,
                                 ok_mo_seek_func seek_func);

#ifdef __cplusplus
}
#endif

#endif
