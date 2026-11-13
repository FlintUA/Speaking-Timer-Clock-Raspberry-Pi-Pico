#pragma once
#include "phone_access.h"

typedef struct {
    bool sound_on, alarm_active, timer_running, editing, player_open, audio_busy;
} phone_control_state_t;

// Rechecked in the main loop. APPLIED here means admission, not audio completion.
phone_command_result_t phone_control_guard(phone_action_t action, const phone_control_state_t *state);

// Revisions identify a set of snoozes, never the ticking remaining-time display.
typedef struct {
    uint32_t revision;
    uint8_t pending, waiting;
    uint64_t deadlines[CLOCK_ALARM_COUNT], queued[CLOCK_ALARM_COUNT];
} phone_snooze_revision_t;
uint32_t phone_snooze_revision(phone_snooze_revision_t *state,
    const alarm_snooze_t snoozes[CLOCK_ALARM_COUNT], uint8_t waiting,
    const uint64_t queued[CLOCK_ALARM_COUNT]);
phone_command_result_t phone_alarm_guard(phone_action_t action, uint32_t expected,
    uint32_t active_id, uint32_t snooze_revision, bool ringing, bool snoozes);
