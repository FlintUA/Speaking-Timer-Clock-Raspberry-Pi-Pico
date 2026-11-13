#include <assert.h>
#include "music_sleep.h"

int main(void) {
    music_sleep_t s={0};
    assert(!music_sleep_set(&s,1,100) && !s.deadline_ms);
    assert(music_sleep_set(&s,60,100));
    assert(s.deadline_ms==3600100 && s.preset==3);
    assert(!music_sleep_set(&s,91,200) && s.deadline_ms==3600100);
    assert(music_sleep_set(&s,60,200) && s.deadline_ms==3600200);
    assert(music_sleep_cycle(&s,200)==90); // Physical cycle follows browser selection.
    assert(music_sleep_set(&s,0,300) && !s.deadline_ms && !s.preset);
    assert(!music_sleep_expired(&s,100) && !music_sleep_minutes(&s,100));
    uint64_t start=UINT64_C(0xffffffff)-500;
    assert(music_sleep_cycle(&s,start)==15);
    assert(music_sleep_minutes(&s,start)==15);
    assert(music_sleep_minutes(&s,start+59999)==15);
    assert(music_sleep_minutes(&s,start+60000)==14);
    assert(!music_sleep_expired(&s,start+899999));
    assert(music_sleep_minutes(&s,start+899999)==1);
    assert(music_sleep_expired(&s,start+900000));
    assert(!music_sleep_minutes(&s,start+900000));
    assert(music_sleep_expired(&s,start+900001));
    // Selecting a new preset resets its monotonic deadline.
    start+=900001;
    assert(music_sleep_cycle(&s,start)==30);
    assert(s.deadline_ms==start+1800000);
    assert(music_sleep_cycle(&s,start)==60);
    assert(music_sleep_cycle(&s,start)==90);
    assert(music_sleep_minutes(&s,start)==90);
    assert(music_sleep_cycle(&s,start)==0);
    assert(!s.deadline_ms && !music_sleep_expired(&s,UINT64_MAX));
    assert(music_sleep_cycle(&s,start)==15);
    s=(music_sleep_t){0}; // Closing/reopening does not restore a deadline.
    assert(!music_sleep_minutes(&s,start) && !music_sleep_expired(&s,start));
    return 0;
}
