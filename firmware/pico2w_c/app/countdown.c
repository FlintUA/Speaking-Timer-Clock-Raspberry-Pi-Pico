#include "countdown.h"

void countdown_init(countdown_t *timer) {
    *timer = (countdown_t){.duration_seconds = 60u};
}

void countdown_set_duration(countdown_t *timer, uint32_t seconds) {
    if (seconds < 1u) seconds = 1u;
    if (seconds > CLOCK_TIMER_MAX_SECONDS) seconds = CLOCK_TIMER_MAX_SECONDS;
    timer->duration_seconds = seconds;
}

void countdown_start(countdown_t *timer, uint64_t now_ms) {
    timer->deadline_ms = now_ms + (uint64_t)timer->duration_seconds * 1000u;
    timer->running = true;
    timer->finished_event = false;
}

void countdown_cancel(countdown_t *timer) {
    timer->running = false;
    timer->deadline_ms = 0;
    timer->finished_event = false;
}

uint32_t countdown_remaining(const countdown_t *timer, uint64_t now_ms) {
    if (!timer->running || now_ms >= timer->deadline_ms) return 0;
    uint64_t remain = timer->deadline_ms - now_ms;
    return (uint32_t)((remain + 999u) / 1000u);
}

void countdown_service(countdown_t *timer, uint64_t now_ms) {
    if (timer->running && countdown_remaining(timer, now_ms) == 0) {
        timer->running = false;
        timer->deadline_ms = 0;
        timer->finished_event = true;
    }
}

bool countdown_consume_finished(countdown_t *timer) {
    if (!timer->finished_event) return false;
    timer->finished_event = false;
    return true;
}
