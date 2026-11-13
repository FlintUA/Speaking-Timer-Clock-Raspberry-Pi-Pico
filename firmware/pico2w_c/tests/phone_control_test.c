#include <assert.h>
#include "phone_control.h"

int main(void) {
    phone_control_state_t state={.sound_on=true};
    for (phone_action_t action=PHONE_SOUND_SET; action<=PHONE_SAY_NEXT_ALARM; ++action)
        assert(phone_control_guard(action,&state)==PHONE_COMMAND_APPLIED);
    assert(phone_control_guard(PHONE_PLAYER_STOP,&state)==PHONE_COMMAND_BLOCKED);
    assert(phone_control_guard(PHONE_SOUND_SET,NULL)==PHONE_COMMAND_BLOCKED);
    state=(phone_control_state_t){.editing=true,.timer_running=true,.player_open=true,.audio_busy=true};
    assert(phone_control_guard(PHONE_SOUND_SET,&state)==PHONE_COMMAND_APPLIED);
    assert(phone_control_guard(PHONE_SAY_DATE,&state)==PHONE_COMMAND_MUTED);
    state.sound_on=true;
    assert(phone_control_guard(PHONE_SAY_DATE,&state)==PHONE_COMMAND_EDITING);
    state.editing=false;
    assert(phone_control_guard(PHONE_SAY_TIME,&state)==PHONE_COMMAND_TIMER_BUSY);
    state.timer_running=false;
    assert(phone_control_guard(PHONE_SAY_NEXT_ALARM,&state)==PHONE_COMMAND_PLAYER_BUSY);
    state.player_open=false;
    assert(phone_control_guard(PHONE_SAY_DATE,&state)==PHONE_COMMAND_AUDIO_BUSY);
    state.alarm_active=true;
    assert(phone_control_guard(PHONE_SOUND_SET,&state)==PHONE_COMMAND_ALARM_BUSY);
    state.sound_on=false;
    assert(phone_control_guard(PHONE_SAY_TIME,&state)==PHONE_COMMAND_ALARM_BUSY);
    // Same-slot retrigger and stale snooze-set commands must not affect a new occurrence.
    assert(phone_alarm_guard(PHONE_ALARM_STOP, 41, 42, 3, true, true)==PHONE_COMMAND_CONFLICT);
    assert(phone_alarm_guard(PHONE_ALARM_SNOOZE, 42, 42, 3, true, true)==PHONE_COMMAND_APPLIED);
    assert(phone_alarm_guard(PHONE_ALARM_STOP, 42, 42, 3, false, true)==PHONE_COMMAND_NO_RINGING);
    assert(phone_alarm_guard(PHONE_ALARM_CANCEL_SNOOZES, 2, 42, 3, true, true)==PHONE_COMMAND_CONFLICT);
    assert(phone_alarm_guard(PHONE_ALARM_CANCEL_SNOOZES, 3, 42, 3, true, true)==PHONE_COMMAND_APPLIED);
    assert(phone_alarm_guard(PHONE_ALARM_CANCEL_SNOOZES, 3, 42, 3, false, false)==PHONE_COMMAND_NO_SNOOZE);
    assert(phone_alarm_guard(PHONE_ALARM_STOP, 0, 0, 3, true, true)==PHONE_COMMAND_CONFLICT);
    assert(phone_alarm_guard(PHONE_PLAYER_STOP, 42, 42, 3, true, true)==PHONE_COMMAND_BLOCKED);
    phone_snooze_revision_t revision={0};
    alarm_snooze_t snoozes[CLOCK_ALARM_COUNT]={0};
    uint64_t queued[CLOCK_ALARM_COUNT]={0};
    uint32_t r=phone_snooze_revision(&revision,snoozes,0,queued);
    assert(r && phone_snooze_revision(&revision,snoozes,0,queued)==r);
    alarm_snooze_schedule(&snoozes[4],1000,7,0,false);
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==++r);
    // Ordinary polling does not expire or change the identity.
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==r);
    snoozes[4].pending=false; queued[4]=181000;
    assert(phone_snooze_revision(&revision,snoozes,1u<<4,queued)==++r);
    assert(phone_snooze_revision(&revision,snoozes,1u<<4,queued)==r);
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==++r);
    alarm_snooze_schedule(&snoozes[4],182000,7,0,false);
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==++r);
    snoozes[4].pending=false;
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==++r);
    revision.revision=UINT32_MAX;
    snoozes[0].pending=true;
    assert(phone_snooze_revision(&revision,snoozes,0,queued)==1);
    return 0;
}
