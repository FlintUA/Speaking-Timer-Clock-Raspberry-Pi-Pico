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
void clock_pcm_ring_set_paused(bool paused);
bool clock_pcm_ring_paused(void);
// True when no more source PCM can arrive; partial final SBC packets are
// padded with silence without counting a streaming underrun.
void clock_pcm_ring_set_eof(bool eof);
bool clock_pcm_ring_eof(void);
bool clock_pcm_ring_active(void);
uint32_t clock_pcm_ring_underruns(void);
uint32_t clock_pcm_ring_consumed(void);
// Monotonic successful pushes since reset; used to diagnose accounting drift.
uint32_t clock_pcm_ring_produced(void);

// Captures the *first* violation without printing inside the A2DP callback.
// Zero fault_code means no invariant violation was captured since reset.
// phase: 1=push-entry, 2=pop-entry, 3=pop-exit, 4=reset-during-pop.
typedef struct {
    uint32_t fault_code;
    uint32_t phase;
    uint32_t produced;
    uint32_t consumed;
    uint32_t buffered;
    uint32_t head;
    uint32_t tail;
    uint32_t request;
    uint32_t take;
    uint32_t pop_calls;
    uint32_t max_pop_depth;
    uint32_t resets_during_pop;
} clock_pcm_ring_diag_t;
void clock_pcm_ring_diagnostics(clock_pcm_ring_diag_t *snapshot);
