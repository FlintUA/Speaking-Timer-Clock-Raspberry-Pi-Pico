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

uint32_t phone_snooze_revision(phone_snooze_revision_t *s,
    const alarm_snooze_t snoozes[CLOCK_ALARM_COUNT], uint8_t waiting,
    const uint64_t queued[CLOCK_ALARM_COUNT]) {
    bool changed = !s->revision || s->waiting != waiting;
    uint8_t pending = 0;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        uint64_t deadline = snoozes[i].pending ? snoozes[i].deadline_ms : 0;
        uint64_t due = (waiting & (1u << i)) ? queued[i] : 0;
        if (snoozes[i].pending) pending |= (uint8_t)(1u << i);
        if (s->deadlines[i] != deadline || s->queued[i] != due) changed = true;
        s->deadlines[i] = deadline;
        s->queued[i] = due;
    }
    if (s->pending != pending) changed = true;
    s->pending = pending;
    s->waiting = waiting;
    if (changed && !++s->revision) ++s->revision;
    return s->revision;
}

phone_command_result_t phone_alarm_guard(phone_action_t action, uint32_t expected,
    uint32_t active_id, uint32_t revision, bool ringing, bool snoozes) {
    if (action == PHONE_ALARM_CANCEL_SNOOZES) {
        if (!snoozes) return PHONE_COMMAND_NO_SNOOZE;
        return expected && expected == revision ? PHONE_COMMAND_APPLIED : PHONE_COMMAND_CONFLICT;
    }
    if (action != PHONE_ALARM_STOP && action != PHONE_ALARM_SNOOZE) return PHONE_COMMAND_BLOCKED;
    if (!ringing) return PHONE_COMMAND_NO_RINGING;
    return expected && expected == active_id ? PHONE_COMMAND_APPLIED : PHONE_COMMAND_CONFLICT;
}
