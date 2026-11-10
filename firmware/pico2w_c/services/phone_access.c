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
    access->code = code;
    access->code_until = now + PHONE_CODE_MS;
    access->attempts = 5;
    access->paired = false;
}

void phone_access_reset(phone_access_t *access, uint32_t epoch) {
    access->code_until = 0;
    access->attempts = 0;
    access->paired = false;
    access->epoch = epoch;
    access->last = (phone_timer_request_t){0};
    access->result = PHONE_TIMER_PENDING;
}

static unsigned browser_index(const phone_access_t *access, const char *token, uint8_t scope) {
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i) {
        const phone_browser_t *browser = &access->settings.browsers[i];
        unsigned difference = 0;
        for (unsigned j = 0; j < PHONE_TOKEN_LEN; ++j)
            difference |= (unsigned)(browser->token[j] ^ token[j]);
        if (browser->scope == scope && !difference) return i;
    }
    return PHONE_BROWSER_MAX;
}

phone_reply_t phone_access_check(const phone_access_t *access, const char *token, uint8_t scope) {
    if (strlen(token) != PHONE_TOKEN_LEN || !phone_hex_valid(token, PHONE_TOKEN_LEN))
        return PHONE_INVALID;
    return browser_index(access, token, scope) < PHONE_BROWSER_MAX ? PHONE_OK : PHONE_DENIED;
}

phone_reply_t phone_access_logout(phone_access_t *access, const char *token, uint8_t scope) {
    if (strlen(token) != PHONE_TOKEN_LEN || !phone_hex_valid(token, PHONE_TOKEN_LEN))
        return PHONE_INVALID;
    unsigned i = browser_index(access, token, scope);
    if (i < PHONE_BROWSER_MAX) {
        access->settings.browsers[i] = (phone_browser_t){0};
        access->changed = true;
    }
    return PHONE_OK;
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

phone_reply_t phone_access_pair(phone_access_t *access, const char *body, uint64_t now, uint8_t scope) {
    if (strlen(body) != 7u + PHONE_TOKEN_LEN || body[6] != ' ' ||
        !phone_hex_valid(body + 7, PHONE_TOKEN_LEN)) return PHONE_INVALID;
    const char *end = body;
    uint32_t code;
    if (!read_number(&end, &code) || end != body + 6) return PHONE_INVALID;
    const char *token = body + 7;
    if (browser_index(access, token, scope) < PHONE_BROWSER_MAX) return PHONE_OK;
    if (now >= access->code_until || !access->attempts) return PHONE_DENIED;
    --access->attempts;
    if (code != access->code) return PHONE_DENIED;
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i) {
        phone_browser_t *browser = &access->settings.browsers[i];
        if (!browser->scope) {
            browser->scope = scope;
            memcpy(browser->token, token, sizeof(browser->token));
            access->code_until = 0;
            access->paired = true;
            access->changed = true;
            return PHONE_OK;
        }
    }
    return PHONE_BUSY;
}

phone_reply_t phone_access_timer(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    const char *p;
    if (strncmp(body, "- ", 2) == 0) {
        if (scope != PHONE_SCOPE_HOME || !home_peer || !access->settings.home_trusted) return PHONE_DENIED;
        p = body + 2;
    } else {
        if (strlen(body) <= PHONE_TOKEN_LEN || body[PHONE_TOKEN_LEN] != ' ' ||
            !phone_hex_valid(body, PHONE_TOKEN_LEN)) return PHONE_INVALID;
        if (browser_index(access, body, scope) == PHONE_BROWSER_MAX) return PHONE_DENIED;
        p = body + PHONE_TOKEN_LEN + 1;
    }

    uint32_t epoch;
    phone_timer_request_t request = {0};
    if (!read_number(&p, &epoch) || *p++ != ' ' ||
        !read_number(&p, &request.id) || !request.id || *p++ != ' ' ||
        !read_number(&p, &request.nonce) || !request.nonce || *p++ != ' ') return PHONE_INVALID;
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

    if (epoch != access->epoch) return PHONE_BUSY;
    if (request.id == access->last.id)
        return request.nonce == access->last.nonce && request.action == access->last.action &&
               request.seconds == access->last.seconds ? PHONE_OK : PHONE_BUSY;
    if (request.id != access->last.id + 1u ||
        (access->last.id && access->result == PHONE_TIMER_PENDING)) return PHONE_BUSY;
    access->last = request;
    access->result = PHONE_TIMER_PENDING;
    return PHONE_OK;
}
