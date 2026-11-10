#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHONE_CODE_MS 60000u
#define PHONE_SESSION_MS 900000u
#define PHONE_TOKEN_LEN 32u

typedef enum { PHONE_TIMER_SET, PHONE_TIMER_START, PHONE_TIMER_CANCEL } phone_timer_action_t;
typedef enum { PHONE_OK, PHONE_INVALID, PHONE_DENIED, PHONE_BUSY } phone_reply_t;
typedef enum { PHONE_TIMER_PENDING, PHONE_TIMER_APPLIED, PHONE_TIMER_BLOCKED } phone_timer_result_t;

typedef struct {
    uint32_t id, seconds;
    phone_timer_action_t action;
} phone_timer_request_t;

typedef struct {
    uint64_t code_until, session_until;
    uint32_t code;
    unsigned attempts;
    char token[PHONE_TOKEN_LEN + 1];
    phone_timer_request_t last;
    phone_timer_result_t result;
} phone_access_t;

void phone_access_begin(phone_access_t *access, uint32_t code, uint64_t now);
phone_reply_t phone_access_pair(phone_access_t *access, const char *body, uint64_t now);
phone_reply_t phone_access_timer(phone_access_t *access, const char *body, uint64_t now);
bool phone_access_same_origin(const char *header, size_t length, const char *address);
