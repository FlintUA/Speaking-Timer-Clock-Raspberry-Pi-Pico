#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CLOCK_TIMER_MAX_SECONDS (24u * 60u * 60u - 1u)

// One timer, including 1..86399 seconds,
// ceil-to-next-second remaining time and one-shot finish event.
typedef struct {
    uint32_t duration_seconds;
    uint64_t deadline_ms;
    bool running;
    bool finished_event;
} countdown_t;

void countdown_init(countdown_t *timer);
void countdown_set_duration(countdown_t *timer, uint32_t seconds);
void countdown_start(countdown_t *timer, uint64_t now_ms);
void countdown_cancel(countdown_t *timer);
uint32_t countdown_remaining(const countdown_t *timer, uint64_t now_ms);
void countdown_service(countdown_t *timer, uint64_t now_ms);
bool countdown_consume_finished(countdown_t *timer);
