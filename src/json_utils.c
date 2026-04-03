#include "json_utils.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ensure_capacity(char **buffer, size_t *capacity, size_t required_length) {
    size_t new_capacity;
    char *new_buffer;

    if (*buffer && *capacity > required_length) {
        return 0;
    }

    new_capacity = *capacity > 0 ? *capacity : 256;
    while (new_capacity <= required_length) {
        new_capacity *= 2;
    }

    new_buffer = realloc(*buffer, new_capacity);
    if (!new_buffer) {
        return -1;
    }

    if (!*buffer) {
        new_buffer[0] = '\0';
    }

    *buffer = new_buffer;
    *capacity = new_capacity;
    return 0;
}

int json_buffer_append(char **buffer, size_t *length, size_t *capacity, const char *text) {
    size_t text_length;

    if (!buffer || !length || !capacity || !text) {
        return -1;
    }

    text_length = strlen(text);
    if (ensure_capacity(buffer, capacity, *length + text_length + 1) != 0) {
        return -1;
    }

    memcpy(*buffer + *length, text, text_length);
    *length += text_length;
    (*buffer)[*length] = '\0';
    return 0;
}

int json_buffer_appendf(char **buffer, size_t *length, size_t *capacity, const char *format, ...) {
    va_list args;
    va_list copy;
    int required;

    if (!buffer || !length || !capacity || !format) {
        return -1;
    }

    va_start(args, format);
    va_copy(copy, args);
    required = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (required < 0) {
        va_end(args);
        return -1;
    }

    if (ensure_capacity(buffer, capacity, *length + (size_t)required + 1) != 0) {
        va_end(args);
        return -1;
    }

    vsnprintf(*buffer + *length, *capacity - *length, format, args);
    *length += (size_t)required;
    va_end(args);
    return 0;
}

int json_buffer_append_escaped(char **buffer, size_t *length, size_t *capacity, const char *text) {
    const unsigned char *cursor;

    if (!buffer || !length || !capacity) {
        return -1;
    }

    if (!text) {
        return json_buffer_append(buffer, length, capacity, "");
    }

    cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        switch (*cursor) {
            case '\\':
                if (json_buffer_append(buffer, length, capacity, "\\\\") != 0) {
                    return -1;
                }
                break;
            case '"':
                if (json_buffer_append(buffer, length, capacity, "\\\"") != 0) {
                    return -1;
                }
                break;
            case '\b':
                if (json_buffer_append(buffer, length, capacity, "\\b") != 0) {
                    return -1;
                }
                break;
            case '\f':
                if (json_buffer_append(buffer, length, capacity, "\\f") != 0) {
                    return -1;
                }
                break;
            case '\n':
                if (json_buffer_append(buffer, length, capacity, "\\n") != 0) {
                    return -1;
                }
                break;
            case '\r':
                if (json_buffer_append(buffer, length, capacity, "\\r") != 0) {
                    return -1;
                }
                break;
            case '\t':
                if (json_buffer_append(buffer, length, capacity, "\\t") != 0) {
                    return -1;
                }
                break;
            default:
                if (*cursor < 0x20) {
                    if (json_buffer_appendf(buffer, length, capacity, "\\u%04x", *cursor) != 0) {
                        return -1;
                    }
                } else {
                    char value[2];

                    value[0] = (char)*cursor;
                    value[1] = '\0';
                    if (json_buffer_append(buffer, length, capacity, value) != 0) {
                        return -1;
                    }
                }
                break;
        }
        cursor++;
    }

    return 0;
}
