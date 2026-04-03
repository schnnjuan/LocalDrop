#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stddef.h>

int json_buffer_append(char **buffer, size_t *length, size_t *capacity, const char *text);
int json_buffer_appendf(char **buffer, size_t *length, size_t *capacity, const char *format, ...);
int json_buffer_append_escaped(char **buffer, size_t *length, size_t *capacity, const char *text);

#endif
