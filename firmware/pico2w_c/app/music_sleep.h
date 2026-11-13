#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t deadline_ms;
    unsigned preset;
} music_sleep_t;

// OFF -> 15 -> 30 -> 60 -> 90 -> OFF. Each selection starts a new deadline.
unsigned music_sleep_cycle(music_sleep_t *sleep, uint64_t now_ms);
// Absolute selection; unsupported values leave the deadline unchanged.
bool music_sleep_set(music_sleep_t *sleep, unsigned minutes, uint64_t now_ms);
unsigned music_sleep_minutes(const music_sleep_t *sleep, uint64_t now_ms);
bool music_sleep_expired(const music_sleep_t *sleep, uint64_t now_ms);
