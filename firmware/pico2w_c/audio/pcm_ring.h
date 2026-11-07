#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Single-core cooperative PCM producer (main loop) and A2DP consumer.
// 8192 stereo frames = about 186 ms at 44.1 kHz.
#define CLOCK_PCM_RING_FRAMES 8192u
void clock_pcm_ring_reset(void);
uint32_t clock_pcm_ring_available(void);
uint32_t clock_pcm_ring_free(void);
bool clock_pcm_ring_push(int16_t left, int16_t right);
uint32_t clock_pcm_ring_pop(int16_t *interleaved_stereo, uint32_t frames);
void clock_pcm_ring_set_active(bool enabled);
bool clock_pcm_ring_active(void);
uint32_t clock_pcm_ring_underruns(void);
uint32_t clock_pcm_ring_consumed(void);
