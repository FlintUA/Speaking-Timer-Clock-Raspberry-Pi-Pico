#pragma once
#include <stdbool.h>
#include <stdint.h>

// Mirrors source/v3/config.py: five daily alarms, default OFF at 07..11:00.
// Music tracks are 1..45, from DFPlayer music folder 08.
#define CLOCK_ALARM_COUNT 5u
#define CLOCK_ALARM_MAX_TRACK 45u

typedef enum {
    ALARM_SOUND_SIGNAL = 0,
    ALARM_SOUND_MUSIC = 1
} alarm_sound_t;

typedef struct {
    bool enabled;
    uint8_t hour;   // 0..23
    uint8_t minute; // 0..59
    alarm_sound_t sound;
    uint8_t track;  // 1..45, used only for music
} alarm_entry_t;

// Uses the entire calendar date and clock minute. This state is RAM-only.
// The caller passes false for busy=false when no alarm is currently ringing.
typedef struct {
    bool minute_valid;
    uint64_t minute_key;
    uint8_t fired_mask; // bit N = alarm N has fired during this minute
} alarm_scheduler_t;

void alarm_defaults(alarm_entry_t alarms[CLOCK_ALARM_COUNT]);
bool alarm_entry_valid(const alarm_entry_t *alarm);
void alarm_scheduler_init(alarm_scheduler_t *state);

// Returns first due index (0..4), or -1. Each alarm is emitted at most once
// per observed calendar minute; simultaneous alarms can fire sequentially
// once an earlier alarm finishes. No audio or RTC writes occur here.
// Caller implements audio/silence/menu priority separately.
int alarm_scheduler_pick(alarm_scheduler_t *state,
                         const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                         uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, bool alarm_active);
