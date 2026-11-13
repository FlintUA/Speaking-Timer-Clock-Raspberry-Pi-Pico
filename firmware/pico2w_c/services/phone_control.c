#include "phone_control.h"

phone_command_result_t phone_control_guard(phone_action_t action, const phone_control_state_t *s) {
    if (!s || action < PHONE_SOUND_SET || action > PHONE_SAY_NEXT_ALARM) return PHONE_COMMAND_BLOCKED;
    // Physical sound toggle is available in menus/player/timer, but not while ringing.
    if (s->alarm_active) return PHONE_COMMAND_ALARM_BUSY;
    if (action == PHONE_SOUND_SET) return PHONE_COMMAND_APPLIED;
    if (!s->sound_on) return PHONE_COMMAND_MUTED;
    if (s->editing) return PHONE_COMMAND_EDITING;
    if (s->timer_running) return PHONE_COMMAND_TIMER_BUSY;
    if (s->player_open) return PHONE_COMMAND_PLAYER_BUSY;
    if (s->audio_busy) return PHONE_COMMAND_AUDIO_BUSY;
    return PHONE_COMMAND_APPLIED;
}
