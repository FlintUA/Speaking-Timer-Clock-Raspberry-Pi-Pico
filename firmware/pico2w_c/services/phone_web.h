#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PHONE_WEB_SSID "SpeakingClock"

typedef struct {
    char time[9]; // HH:MM:SS, or empty when the RTC is unavailable.
    uint32_t timer_seconds;
    uint8_t track;
    bool timer_running;
    bool bluetooth;
    const char *player; // Static, firmware-defined state label.
} phone_web_snapshot_t;

void phone_web_init(bool radio_ready);
bool phone_web_direct(void);
bool phone_web_set_direct(bool direct);
const char *phone_web_address(void);
const char *phone_web_password(void);
void phone_web_status(void);
void phone_web_publish(const phone_web_snapshot_t *snapshot);
