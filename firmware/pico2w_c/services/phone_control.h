#pragma once
#include "phone_access.h"

typedef struct {
    bool sound_on, alarm_active, timer_running, editing, player_open, audio_busy;
} phone_control_state_t;

// Rechecked in the main loop. APPLIED here means admission, not audio completion.
phone_command_result_t phone_control_guard(phone_action_t action, const phone_control_state_t *state);
