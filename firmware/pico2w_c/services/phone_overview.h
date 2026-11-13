#pragma once
#include <stddef.h>
#include "alarm_engine.h"
#include "network_time.h"

typedef enum {
    PHONE_BT_UNAVAILABLE, PHONE_BT_OFF, PHONE_BT_SCANNING,
    PHONE_BT_WAITING, PHONE_BT_CONNECTED
} phone_bt_state_t;

typedef struct {
    bool rtc_valid, sound_on, direct;
    char date[16];
    uint8_t weekday;
    uint8_t active_alarm, deferred_count;
    uint8_t next_slot;
    char next_at[32];
    bool next_snoozed;
    uint8_t snooze_count, snooze_slot;
    uint32_t snooze_seconds;
    bool snooze_waiting;
    phone_bt_state_t bt_state;
    char bt_name[25];
    network_home_status_t network;
    network_reference_status_t ntp;
} phone_overview_t;

// Local civil calendar and monotonic snoozes; no device I/O or state mutation.
void phone_overview_calendar(phone_overview_t *out, const ds1302_datetime_t *now,
    const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
    const alarm_snooze_t snoozes[CLOCK_ALARM_COUNT], uint8_t waiting_mask,
    uint64_t uptime_ms);
// Append an overview member to an existing JSON object. Negative means overflow.
int phone_overview_json(char *buffer, size_t capacity, const phone_overview_t *status);
