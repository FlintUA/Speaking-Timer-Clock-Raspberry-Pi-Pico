// Host test, no Pico SDK required:
//   cc -std=c11 -Wall -Wextra -Werror -Iapp app/alarm_engine.c app/clock_time.c
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
    // The daily scheduler filters real calendar weekdays, including leap dates.
    alarm_defaults(a); a[0].enabled=true; a[0].weekdays=CLOCK_ALARM_DAYS_WEEKDAYS;
    alarm_scheduler_init(&state);
    assert(alarm_scheduler_claim_due(&state,a,2026,11,7,7,0,&skipped)==-1); // Saturday.
    assert(alarm_scheduler_claim_due(&state,a,2026,11,8,7,0,&skipped)==-1); // Sunday.
    assert(alarm_scheduler_claim_due(&state,a,2026,11,9,7,0,&skipped)==0); // Monday.
    assert(alarm_scheduler_claim_due(&state,a,2026,11,9,7,0,&skipped)==-1);
    alarm_scheduler_init(&state);
    assert(alarm_scheduler_pick(&state,a,2026,11,8,7,0,false)==-1);
    assert(alarm_scheduler_pick(&state,a,2026,11,9,7,0,false)==0);
    a[1].enabled=true; a[1].hour=7; a[1].weekdays=CLOCK_ALARM_DAYS_WEEKEND;
    assert(alarm_scheduler_claim_due(&state,a,2026,11,8,7,0,&skipped)==1 && skipped==0);
    a[0].weekdays=CLOCK_ALARM_DAYS_WEEKEND;
    assert(alarm_day_matches(&a[0],2026,11,8));
    assert(!alarm_day_matches(&a[0],2026,11,9));
    a[0].weekdays=CLOCK_ALARM_DAYS_WEEKDAYS;
    assert(alarm_day_matches(&a[0],2027,1,1));
    a[0].weekdays=1u<<3; // Thursday.
    assert(alarm_day_matches(&a[0],2024,2,29));
    assert(!alarm_day_matches(&a[0],2025,2,29));
    a[0].weekdays=0; assert(!alarm_entry_valid(&a[0]));
    a[0].weekdays=0x80; assert(!alarm_entry_valid(&a[0]));
    uint8_t mask=0;
    assert(alarm_days_parse("135",&mask) && mask==0x15);
    assert(alarm_days_parse("daily",&mask) && mask==0x7f);
    assert(alarm_days_parse("weekdays",&mask) && mask==0x1f);
    assert(alarm_days_parse("weekends",&mask) && mask==0x60);
    assert(!alarm_days_parse("",&mask)); assert(!alarm_days_parse("11",&mask));
    assert(!alarm_days_parse("0",&mask)); assert(!alarm_days_parse("18",&mask));
    for(unsigned days=1;days<=0x7f;++days) {
        a[0].weekdays=(uint8_t)days;
        for(unsigned day=0;day<7;++day)
            assert(alarm_day_matches(&a[0],2026,11,(uint8_t)(9+day))==((days&(1u<<day))!=0));
    }
    // Snooze is an independent, one-shot monotonic deadline for each slot.
    alarm_snooze_t snoozes[CLOCK_ALARM_COUNT]={0}, due;
    alarm_snooze_schedule(&snoozes[0],1000,23,59,false);
    alarm_snooze_schedule(&snoozes[1],2000,8,30,true);
    assert(!alarm_snooze_take(&snoozes[0],300999,&due));
    assert(alarm_snooze_take(&snoozes[0],301000,&due));
    assert(due.hour==23 && due.minute==59 && !due.test);
    assert(!alarm_snooze_take(&snoozes[0],301001,&due));
    assert(!alarm_snooze_take(&snoozes[1],301999,&due));
    assert(alarm_snooze_take(&snoozes[1],302000,&due) && due.test);
    alarm_snooze_schedule(&snoozes[0],302000,23,59,false); // Repeated snooze.
    assert(!alarm_snooze_take(&snoozes[0],601999,&due));
    assert(alarm_snooze_take(&snoozes[0],602000,&due));
    alarm_snooze_schedule(&snoozes[0],603000,23,59,false);
    snoozes[0].pending=false; // Explicit cancellation / restart.
    assert(!alarm_snooze_take(&snoozes[0],1000000,&due));
    puts("alarm_engine_host_test: PASS");
    return 0;
}
