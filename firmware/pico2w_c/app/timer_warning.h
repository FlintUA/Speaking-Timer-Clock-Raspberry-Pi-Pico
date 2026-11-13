#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint64_t deadline; uint8_t observed;} timer_warning_t;
void timer_warning_reset(timer_warning_t *state);
// Returns a due second (5..1) once, only within 150 ms of its absolute edge.
// Suppressed/late edges are consumed; no queued/catch-up ticks and never at zero.
uint8_t timer_warning_poll(timer_warning_t *state, bool running, uint64_t deadline,
                           uint64_t now, bool enabled, bool sound_on);
