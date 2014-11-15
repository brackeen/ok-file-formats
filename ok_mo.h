/*
 ok-file-formats
 Copyright (c) 2014 David Brackeen
 
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
#ifndef _OK_MO_H_
#define _OK_MO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
    
    /**
     Reads GNU gettext MO files.
     On success, num_strings will be > 0.
     */
    
    struct ok_mo_string;
    
    typedef struct {
        uint32_t num_strings;
        struct ok_mo_string *strings;
        char error_message[80];
    } ok_mo;
    
#ifndef _OK_READ_FUNC_
#define _OK_READ_FUNC_
    /// Reads 'count' bytes into buffer. Returns number of bytes read.
    typedef size_t (*ok_read_func)(void *user_data, uint8_t *buffer, const size_t count);
    
    /// Seek function. Should return 0 on success.
    typedef int (*ok_seek_func)(void *user_data, const int count);
#endif
        
    ok_mo *ok_mo_read(void *user_data, ok_read_func read_func, ok_seek_func seek_func);
    
    /// Gets the value for the specified key. If there is no value for the key, the key is returned.
    const char *ok_mo_value(ok_mo *mo, const char *key);

    /// Gets the value for the specified key. If there is no value for the key, the key is returned.
    /// If there are plural variants, returns the plural variant for the specified n value.
    const char *ok_mo_plural_value(ok_mo *mo, const char *key, const char *plural_key, const int n);

    /// Gets the value for the specified context and key. If there is no value for the key, the key is returned.
    const char *ok_mo_value_in_context(ok_mo *mo, const char *context, const char *key);

    /// Gets the value for the specified context and key. If there is no value for the key, the key is returned.
    /// If there are plural variants, returns the plural variant for the specified n value.
    const char *ok_mo_plural_value_in_context(ok_mo *mo, const char *context, const char *key, const char *plural_key,
                                              const int n);

    void ok_mo_free(ok_mo *csv);
    
    /// Gets the character length (as opposed to the byte length) of an UTF-8 string.
    size_t ok_utf8_strlen(const char *utf8);
    
    /// Converts the first n characters of a UTF-8 string to a 32-bit Unicode (UCS-4) string.
    /// The dest string must have a length of at least (n + 1) to accommodate the NULL terminator.
    /// Returns the number of characters copied.
    size_t ok_utf8_to_unicode(const char *utf8, uint32_t *dest, const size_t n);
    
#ifdef __cplusplus
}
#endif

#endif
