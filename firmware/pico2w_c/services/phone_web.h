#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "phone_access.h"
#include "display_light.h"

#define PHONE_WEB_SSID "SpeakingClock"

typedef struct {
    uint8_t volume_local, volume_bt, language;
    uint8_t audio[4];
    display_light_t light;
    bool editing, idle, bt_supported, pending, retry;
} phone_web_preferences_t;

typedef struct {
    char time[9]; // HH:MM:SS, or empty when the RTC is unavailable.
    uint32_t timer_seconds;
    uint8_t track;
    uint8_t track_count;
    bool timer_running;
    bool bluetooth;
    const char *player; // Static, firmware-defined state label.
    const char *player_mode;
    phone_web_preferences_t preferences;
} phone_web_snapshot_t;

void phone_web_init(bool radio_ready);
bool phone_web_direct(void);
bool phone_web_set_direct(bool direct);
const char *phone_web_address(void);
const char *phone_web_password(void);
bool phone_web_rotate_password(void);
bool phone_web_home_trusted(void);
void phone_web_set_home_trusted(bool enabled);
void phone_web_load_settings(const phone_settings_t *settings);
void phone_web_get_settings(phone_settings_t *settings);
bool phone_web_take_changes(void);
void phone_web_status(void);
void phone_web_publish(const phone_web_snapshot_t *snapshot);
bool phone_web_begin_pair(void);
bool phone_web_pair_code(uint32_t *code);
bool phone_web_pair_succeeded(void);
void phone_web_revoke(void);
bool phone_web_command_pending(phone_command_t *request);
void phone_web_command_complete(uint32_t id, bool applied);
