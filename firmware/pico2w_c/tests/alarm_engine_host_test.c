// Host test, no Pico SDK required:
//   cc -std=c11 -Wall -Wextra -Werror -Iapp app/alarm_engine.c
//      tests/alarm_engine_host_test.c -o alarm_engine_test && ./alarm_engine_test
#include "alarm_engine.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    alarm_entry_t a[CLOCK_ALARM_COUNT];
    alarm_scheduler_t state;
    alarm_defaults(a);
    alarm_scheduler_init(&state);

    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        assert(!a[i].enabled);
        assert(a[i].hour == i + 7u);
        assert(a[i].minute == 0u);
        assert(a[i].sound == ALARM_SOUND_SIGNAL);
        assert(a[i].track == 1u);
    }
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,0,false)==-1);

    a[0].enabled = true;
    a[1].enabled = true;
    a[1].hour = 7;
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,0,false)==0);
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,0,true)==-1);
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,0,false)==1);
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,0,false)==-1);
    assert(alarm_scheduler_pick(&state,a,2026,11,5,7,1,false)==-1);
    // New day permits re-trigger.
    assert(alarm_scheduler_pick(&state,a,2026,11,6,7,0,false)==0);
    // Invalid entry ignored.
    a[2].enabled = true; a[2].hour = 25;
    assert(!alarm_entry_valid(&a[2]));
    assert(alarm_scheduler_pick(&state,a,2026,11,6,9,0,false)==-1);
    puts("alarm_engine_host_test: PASS");
    return 0;
}
