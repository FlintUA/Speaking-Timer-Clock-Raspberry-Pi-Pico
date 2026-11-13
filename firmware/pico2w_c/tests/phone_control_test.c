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
    return 0;
}
