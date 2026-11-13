#include "music_sleep.h"

static const unsigned presets[] = {0, 15, 30, 60, 90};

bool music_sleep_set(music_sleep_t *sleep, unsigned minutes, uint64_t now_ms) {
    for (unsigned i=0; i<5u; ++i) {
        if (presets[i] != minutes) continue;
        sleep->preset=i;
        sleep->deadline_ms=minutes ? now_ms+(uint64_t)minutes*60000u : 0;
        return true;
    }
    return false;
}

unsigned music_sleep_cycle(music_sleep_t *sleep, uint64_t now_ms) {
    unsigned minutes = presets[(sleep->preset + 1u) % 5u];
    (void)music_sleep_set(sleep,minutes,now_ms);
    return minutes;
}

unsigned music_sleep_minutes(const music_sleep_t *sleep, uint64_t now_ms) {
    if (!sleep->deadline_ms || now_ms >= sleep->deadline_ms) return 0;
    return (unsigned)((sleep->deadline_ms - now_ms + 59999u) / 60000u);
}

bool music_sleep_expired(const music_sleep_t *sleep, uint64_t now_ms) {
    return sleep->deadline_ms && now_ms >= sleep->deadline_ms;
}
