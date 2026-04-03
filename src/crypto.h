#ifndef CRYPTO_H
#define CRYPTO_H

#include <stddef.h>

int crypto_random_bytes(unsigned char *buffer, size_t length);
int crypto_random_token(char *dest, size_t dest_size, size_t byte_count);
int crypto_random_numeric_code(char *dest, size_t dest_size, size_t digits);

#endif
