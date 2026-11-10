#include "phone_access.h"
#include "countdown.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool matching_header(const char *header, size_t length, const char *key, const char *value) {
    size_t key_length = strlen(key), value_length = strlen(value);
    unsigned matches = 0;
    const char *p = header, *limit = header + length;
    while (p < limit && *p) {
        const char *end = p;
        while (end < limit && *end && *end != '\r' && *end != '\n') ++end;
        size_t i = 0;
        while (i < key_length && p + i < end &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)key[i])) ++i;
        if (i == key_length) {
            const char *start = p + key_length;
            while (start < end && (*start == ' ' || *start == '\t')) ++start;
            if ((size_t)(end - start) != value_length || memcmp(start, value, value_length) != 0)
                return false;
            ++matches;
        }
        while (end < limit && (*end == '\r' || *end == '\n')) ++end;
        p = end;
    }
    return matches == 1;
}

bool phone_access_same_origin(const char *header, size_t length, const char *address) {
    char origin[sizeof("http://255.255.255.255")];
    int count = snprintf(origin, sizeof(origin), "http://%s", address);
    return address[0] && count > 0 && (size_t)count < sizeof(origin) &&
           matching_header(header, length, "Host:", address) &&
           matching_header(header, length, "Origin:", origin);
}

void phone_access_begin(phone_access_t *access, uint32_t code, uint64_t now) {
    *access = (phone_access_t){.code_until = now + PHONE_CODE_MS, .code = code, .attempts = 5};
}

static bool hex_token(const char *text) {
    for (unsigned i = 0; i < PHONE_TOKEN_LEN; ++i) {
        char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

static bool authorized(const phone_access_t *access, const char *token, uint64_t now) {
    if (now >= access->session_until) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < PHONE_TOKEN_LEN; ++i)
        difference |= (unsigned)(access->token[i] ^ token[i]);
    return difference == 0;
}

static bool read_number(const char **text, uint32_t *value) {
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    uint32_t number = 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (number > (UINT32_MAX - digit) / 10u) return false;
        number = number * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = number;
    return true;
}

phone_reply_t phone_access_pair(phone_access_t *access, const char *body, uint64_t now) {
    if (strlen(body) != 7u + PHONE_TOKEN_LEN || body[6] != ' ' || !hex_token(body + 7))
        return PHONE_INVALID;
    const char *end = body;
    uint32_t code;
    if (!read_number(&end, &code) || end != body + 6) return PHONE_INVALID;
    const char *token = body + 7;
    if (authorized(access, token, now)) return PHONE_OK;
    if (now >= access->code_until || !access->attempts) return PHONE_DENIED;
    --access->attempts;
    if (code != access->code) return PHONE_DENIED;
    access->code_until = 0;
    memcpy(access->token, token, PHONE_TOKEN_LEN + 1);
    access->session_until = now + PHONE_SESSION_MS;
    return PHONE_OK;
}

phone_reply_t phone_access_timer(phone_access_t *access, const char *body, uint64_t now) {
    if (strlen(body) <= PHONE_TOKEN_LEN || body[PHONE_TOKEN_LEN] != ' ' || !hex_token(body))
        return PHONE_INVALID;
    if (!authorized(access, body, now)) return PHONE_DENIED;

    const char *p = body + PHONE_TOKEN_LEN + 1;
    phone_timer_request_t request = {0};
    if (!read_number(&p, &request.id) || !request.id || *p++ != ' ') return PHONE_INVALID;
    if (strncmp(p, "set ", 4) == 0) {
        request.action = PHONE_TIMER_SET;
        p += 4;
    } else if (strncmp(p, "start ", 6) == 0) {
        request.action = PHONE_TIMER_START;
        p += 6;
    } else if (strncmp(p, "cancel ", 7) == 0) {
        request.action = PHONE_TIMER_CANCEL;
        p += 7;
    } else {
        return PHONE_INVALID;
    }
    if (!read_number(&p, &request.seconds) || *p ||
        (request.action == PHONE_TIMER_CANCEL ? request.seconds != 0 :
         request.seconds == 0 || request.seconds > CLOCK_TIMER_MAX_SECONDS))
        return PHONE_INVALID;

    if (request.id == access->last.id)
        return request.action == access->last.action &&
               request.seconds == access->last.seconds ? PHONE_OK : PHONE_INVALID;
    if (request.id < access->last.id) return PHONE_INVALID;
    if (access->last.id && access->result == PHONE_TIMER_PENDING) return PHONE_BUSY;
    access->last = request;
    access->result = PHONE_TIMER_PENDING;
    access->session_until = now + PHONE_SESSION_MS;
    return PHONE_OK;
}
