#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "phone_settings.h"

#define PHONE_CODE_MS 60000u

typedef enum {
    PHONE_TIMER_SET, PHONE_TIMER_START, PHONE_TIMER_CANCEL,
    PHONE_PLAYER_PLAY, PHONE_PLAYER_PAUSE, PHONE_PLAYER_RESUME,
    PHONE_PLAYER_STOP, PHONE_PLAYER_NEXT, PHONE_PLAYER_PREVIOUS
} phone_action_t;
typedef enum { PHONE_OK, PHONE_INVALID, PHONE_DENIED, PHONE_BUSY } phone_reply_t;
typedef enum { PHONE_COMMAND_PENDING, PHONE_COMMAND_APPLIED, PHONE_COMMAND_BLOCKED } phone_command_result_t;

typedef struct {
    uint32_t id, nonce, seconds;
    phone_action_t action;
} phone_command_t;

typedef struct {
    phone_settings_t settings;
    uint64_t code_until;
    uint32_t code;
    unsigned attempts;
    uint32_t epoch;
    bool paired, changed;
    phone_command_t last;
    phone_command_result_t result;
} phone_access_t;

void phone_access_begin(phone_access_t *access, uint32_t code, uint64_t now);
void phone_access_reset(phone_access_t *access, uint32_t epoch);
phone_reply_t phone_access_pair(phone_access_t *access, const char *body, uint64_t now, uint8_t scope);
phone_reply_t phone_access_check(const phone_access_t *access, const char *token, uint8_t scope);
phone_reply_t phone_access_logout(phone_access_t *access, const char *token, uint8_t scope);
phone_reply_t phone_access_timer(phone_access_t *access, const char *body, uint8_t scope, bool home_peer);
phone_reply_t phone_access_player(phone_access_t *access, const char *body, uint8_t scope, bool home_peer);
bool phone_access_same_origin(const char *header, size_t length, const char *address);
