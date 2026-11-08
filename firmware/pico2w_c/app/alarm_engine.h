#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "clock_datetime.h"

// Mirrors source/v3/config.py: five daily alarms, default OFF at 07..11:00.
// Music tracks are 1..255, from folder 08; the editor uses detected count.
#define CLOCK_ALARM_COUNT 5u
#define CLOCK_ALARM_MAX_TRACK 255u
#define CLOCK_ALARM_DAYS_ALL 0x7fu
#define CLOCK_ALARM_DAYS_WEEKDAYS 0x1fu
#define CLOCK_ALARM_DAYS_WEEKEND 0x60u
#define CLOCK_ALARM_SNOOZE_MINUTES 3u
#define CLOCK_ALARM_SNOOZE_MS (CLOCK_ALARM_SNOOZE_MINUTES * 60000u)

typedef enum {
    ALARM_SOUND_SIGNAL = 0,
    ALARM_SOUND_MUSIC = 1
} alarm_sound_t;

typedef struct {
    bool enabled;
    uint8_t hour;   // 0..23
    uint8_t minute; // 0..59
    alarm_sound_t sound;
    uint8_t track;  // 1..255, used only for music
    uint8_t weekdays; // Bit 0=Monday ... bit 6=Sunday; nonzero.
} alarm_entry_t;

typedef struct {
    bool pending, test;
    uint8_t hour, minute;
    uint64_t deadline_ms;
} alarm_snooze_t;

bool alarm_day_matches(const alarm_entry_t *alarm, uint16_t year, uint8_t month, uint8_t day);
bool alarm_days_parse(const char *text, uint8_t *mask);
const char *alarm_days_name(uint8_t mask);
void alarm_snooze_schedule(alarm_snooze_t *state, uint64_t now_ms,
                           uint8_t hour, uint8_t minute, bool test);
bool alarm_snooze_take(alarm_snooze_t *state, uint64_t now_ms, alarm_snooze_t *due);

// Uses the entire calendar date and clock minute. This state is RAM-only.
// The caller passes false for busy=false when no alarm is currently ringing.
typedef struct {
    bool minute_valid;
    uint64_t minute_key;
    uint8_t fired_mask; // bit N = alarm N has fired during this minute
} alarm_scheduler_t;

void alarm_defaults(alarm_entry_t alarms[CLOCK_ALARM_COUNT]);
bool alarm_entry_valid(const alarm_entry_t *alarm);
// Next strictly future scheduled occurrence in local wall time, not a UTC instant.
// Equal times choose the lowest slot. Snoozes/deferred playback are caller-owned.
int alarm_next_occurrence(const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                          const ds1302_datetime_t *now, ds1302_datetime_t *next);
void alarm_scheduler_init(alarm_scheduler_t *state);

// Returns first due index (0..4), or -1. Each alarm is emitted at most once
// per observed calendar minute; simultaneous alarms can fire sequentially
// once an earlier alarm finishes. No audio or RTC writes occur here.
// Caller implements audio/silence/menu priority separately.
int alarm_scheduler_pick(alarm_scheduler_t *state,
                         const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                         uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, bool alarm_active);

// Consume all enabled matches in one calendar minute in a single decision.
// The lowest-numbered slot wins; others are marked handled to avoid
// surprise playback after the winning alarm completes/stops.
// Returns winning index or -1. *other_mask contains skipped co-due slots.
int alarm_scheduler_claim_due(alarm_scheduler_t *state,
    const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t *other_mask);
