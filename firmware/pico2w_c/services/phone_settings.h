#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHONE_TOKEN_LEN 32u
#define PHONE_PASSWORD_LEN 16u
#define PHONE_BROWSER_MAX 4u

enum { PHONE_SCOPE_HOME = 1, PHONE_SCOPE_DIRECT = 2 };

typedef struct {
    uint8_t scope;
    char token[PHONE_TOKEN_LEN + 1];
} phone_browser_t;

typedef struct {
    uint8_t home_trusted;
    char password[PHONE_PASSWORD_LEN + 1];
    phone_browser_t browsers[PHONE_BROWSER_MAX];
} phone_settings_t;

static inline bool phone_hex_valid(const char *text, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}
