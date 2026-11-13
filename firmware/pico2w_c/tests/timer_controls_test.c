#include <assert.h>
#include "timer_controls.h"
#include "countdown.h"

int main(void) {
    timer_editor_t e;
    timer_editor_open(&e,3661);
    assert(e.field==TIMER_FIELD_HOUR && e.hour==1 && e.minute==1 && e.second==1);
    uint32_t draft=999;
    timer_editor_rotate(&e,-1);
    assert(!timer_editor_confirm(&e,&draft) && draft==999);
    timer_editor_rotate(&e,-1);
    assert(e.minute==0);
    timer_editor_rotate(&e,-1);
    assert(e.minute==59);
    assert(!timer_editor_confirm(&e,&draft));
    assert(!timer_editor_back(&e) && e.field==TIMER_FIELD_MINUTE);
    assert(!timer_editor_back(&e) && e.field==TIMER_FIELD_HOUR);
    assert(timer_editor_back(&e)); // Cancellation never publishes a duration.
    assert(draft==999);

    timer_editor_open(&e,0);
    assert(!timer_editor_confirm(&e,&draft));
    assert(!timer_editor_confirm(&e,&draft));
    assert(!timer_editor_confirm(&e,&draft) && draft==999);
    timer_editor_rotate(&e,1);
    assert(timer_editor_confirm(&e,&draft) && draft==1);
    countdown_t timer;
    countdown_init(&timer);
    countdown_set_duration(&timer,draft);
    countdown_start(&timer,500);
    assert(countdown_remaining(&timer,1499)==1);
    countdown_service(&timer,1500);
    assert(countdown_consume_finished(&timer));
    assert(!countdown_consume_finished(&timer));

    timer_editor_open(&e,22*3600+59*60+59);
    timer_editor_rotate(&e,1);
    assert(e.hour==23 && e.minute==59 && e.second==59);
    assert(!timer_editor_confirm(&e,&draft));
    timer_editor_rotate(&e,1);
    assert(e.minute==0);
    timer_editor_rotate(&e,-1);
    assert(!timer_editor_confirm(&e,&draft));
    assert(timer_editor_confirm(&e,&draft) && draft==CLOCK_TIMER_MAX_SECONDS);
    countdown_set_duration(&timer,draft);
    countdown_start(&timer,0);
    assert(countdown_remaining(&timer,0)==CLOCK_TIMER_MAX_SECONDS);
    countdown_service(&timer,(uint64_t)CLOCK_TIMER_MAX_SECONDS*1000u);
    assert(countdown_consume_finished(&timer));

    timer_acceleration_t accel={0};
    unsigned step;
    uint32_t seconds=60;
    for(unsigned i=0;i<8;i++) {
        seconds=timer_quick_adjust(&accel,seconds,1,1000+i*100,&step);
        assert(step==(i<2?1u:i<4?5u:10u));
    }
    seconds=timer_quick_adjust(&accel,seconds,-1,1800,&step);
    assert(step==1); // Reversing direction must not make a large accidental jump.
    seconds=timer_quick_adjust(&accel,seconds,-1,2060,&step);
    assert(step==1); // A 260 ms gap resets acceleration.
    accel=(timer_acceleration_t){0};
    for(unsigned i=0;i<5;i++)
        seconds=timer_quick_adjust(&accel,seconds,1,3000+i*259,&step);
    assert(step==10);
    assert(timer_quick_adjust(&accel,CLOCK_TIMER_MAX_SECONDS,1,5000,&step)==CLOCK_TIMER_MAX_SECONDS);
    assert(timer_quick_adjust(&accel,1,-1,6000,&step)==60);
    assert(timer_quick_adjust(&accel,59,1,7000,&step)==60);
    assert(timer_quick_adjust(&accel,61,0,7100,&step)==61 && step==0);
    return 0;
}
