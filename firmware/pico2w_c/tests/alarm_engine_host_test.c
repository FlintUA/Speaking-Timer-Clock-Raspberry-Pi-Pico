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
    // Collision policy: lowest slot wins, other co-due alarms are
    // consumed within the same RTC minute, including after STOP.
    alarm_defaults(a);
    alarm_scheduler_init(&state);
    a[1].enabled = true;
    a[1].hour = 10;
    a[1].minute = 15;
    a[3].enabled = true;
    a[3].hour = 10;
    a[3].minute = 15;
    uint8_t skipped = 0xffu;
    assert(alarm_scheduler_claim_due(&state, a, 2026, 11, 6,
                                     10, 15, &skipped) == 1);
    assert(skipped == (1u << 3));
    assert(state.fired_mask == ((1u << 1) | (1u << 3)));
    assert(alarm_scheduler_claim_due(&state, a, 2026, 11, 6,
                                     10, 15, &skipped) == -1);
    assert(skipped == 0u);
    assert(alarm_scheduler_claim_due(&state, a, 2026, 11, 6,
                                     10, 16, &skipped) == -1);
    // Next day the same two alarms may compete again.
    assert(alarm_scheduler_claim_due(&state, a, 2026, 11, 7,
                                     10, 15, &skipped) == 1);
    assert(skipped == (1u << 3));
    // An invalid entry cannot win or be included in skipped mask.
    a[1].hour = 25u;
    alarm_scheduler_init(&state);
    assert(alarm_scheduler_claim_due(&state, a, 2026, 11, 8,
                                     10, 15, &skipped) == 3);
    assert(skipped == 0u);
    puts("alarm_engine_host_test: PASS");
    return 0;
}
