#include "alarm_engine.h"
#include <string.h>

void alarm_defaults(alarm_entry_t alarms[CLOCK_ALARM_COUNT]) {
    if (!alarms) return;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        alarms[i].enabled = false;
        alarms[i].hour = (uint8_t)(7u + i);
        alarms[i].minute = 0u;
        alarms[i].sound = ALARM_SOUND_SIGNAL;
        alarms[i].track = 1u;
    }
}

bool alarm_entry_valid(const alarm_entry_t *a) {
    return a && a->hour <= 23u && a->minute <= 59u &&
           (a->sound == ALARM_SOUND_SIGNAL || a->sound == ALARM_SOUND_MUSIC) &&
           a->track >= 1u && a->track <= CLOCK_ALARM_MAX_TRACK;
}

void alarm_scheduler_init(alarm_scheduler_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

static uint64_t calendar_minute_key(uint16_t year, uint8_t month,
                                    uint8_t day, uint8_t hour, uint8_t minute) {
    return (((((uint64_t)year * 13u + month) * 32u + day) * 24u + hour) *
            60u + minute);
}

int alarm_scheduler_pick(alarm_scheduler_t *state,
                         const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                         uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, bool alarm_active) {
    if (!state || !alarms || year < 2000u || year > 2099u ||
        month < 1u || month > 12u || day < 1u || day > 31u ||
        hour > 23u || minute > 59u) return -1;
    uint64_t key = calendar_minute_key(year, month, day, hour, minute);
    if (!state->minute_valid || state->minute_key != key) {
        state->minute_valid = true;
        state->minute_key = key;
        state->fired_mask = 0u;
    }
    if (alarm_active) return -1;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        if ((state->fired_mask & (1u << i)) || !alarm_entry_valid(a) ||
            !a->enabled || a->hour != hour || a->minute != minute) {
            continue;
        }
        state->fired_mask |= (uint8_t)(1u << i);
        return (int)i;
    }
    return -1;
}

int alarm_scheduler_claim_due(alarm_scheduler_t *state,
    const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t *other_mask) {
    if (other_mask) *other_mask = 0u;
    if (!state || !alarms || year < 2000u || year > 2099u ||
        month < 1u || month > 12u || day < 1u || day > 31u ||
        hour > 23u || minute > 59u) return -1;
    uint64_t key = calendar_minute_key(year, month, day, hour, minute);
    if (!state->minute_valid || state->minute_key != key) {
        state->minute_valid = true;
        state->minute_key = key;
        state->fired_mask = 0u;
    }
    int winner = -1;
    uint8_t skipped = 0u;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        uint8_t bit = (uint8_t)(1u << i);
        if ((state->fired_mask & bit) || !alarm_entry_valid(a) ||
            !a->enabled || a->hour != hour || a->minute != minute) continue;
        state->fired_mask |= bit;
        if (winner < 0) winner = (int)i;
        else skipped |= bit;
    }
    if (other_mask) *other_mask = skipped;
    return winner;
}
