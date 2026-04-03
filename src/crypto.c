#include "crypto.h"

#include <openssl/rand.h>
#include <stdio.h>

int crypto_random_bytes(unsigned char *buffer, size_t length) {
    if (!buffer || length == 0) {
        return -1;
    }

    return RAND_bytes(buffer, (int)length) == 1 ? 0 : -1;
}

int crypto_random_token(char *dest, size_t dest_size, size_t byte_count) {
    static const char HEX[] = "0123456789abcdef";
    unsigned char bytes[32];
    size_t index;

    if (!dest || byte_count == 0 || byte_count > sizeof(bytes) || dest_size <= (byte_count * 2U)) {
        return -1;
    }

    if (crypto_random_bytes(bytes, byte_count) != 0) {
        return -1;
    }

    for (index = 0; index < byte_count; index++) {
        dest[index * 2U] = HEX[(bytes[index] >> 4) & 0x0fU];
        dest[index * 2U + 1U] = HEX[bytes[index] & 0x0fU];
    }

    dest[byte_count * 2U] = '\0';
    return 0;
}

int crypto_random_numeric_code(char *dest, size_t dest_size, size_t digits) {
    unsigned char bytes[16];
    size_t index;

    if (!dest || digits == 0 || digits > sizeof(bytes) || dest_size <= digits) {
        return -1;
    }

    if (crypto_random_bytes(bytes, digits) != 0) {
        return -1;
    }

    for (index = 0; index < digits; index++) {
        dest[index] = (char)('0' + (bytes[index] % 10U));
    }

    dest[digits] = '\0';
    return 0;
}
