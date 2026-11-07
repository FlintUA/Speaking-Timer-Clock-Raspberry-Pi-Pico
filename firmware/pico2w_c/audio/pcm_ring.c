#include "pcm_ring.h"
#include <string.h>

// Consumed and produced in one BTstack cooperative run-loop context.
static int16_t samples[CLOCK_PCM_RING_FRAMES * 2u];
static uint32_t head, tail, count, underruns, consumed, produced;
static bool active, paused, source_eof;
void clock_pcm_ring_reset(void) {
    active = false;
    paused = false;
    source_eof = false;
    head = tail = count = underruns = consumed = produced = 0;
}
uint32_t clock_pcm_ring_available(void) { return count; }
uint32_t clock_pcm_ring_free(void) { return CLOCK_PCM_RING_FRAMES - count; }
bool clock_pcm_ring_push(int16_t l, int16_t r) {
    if (count == CLOCK_PCM_RING_FRAMES) return false;
    samples[2u * head] = l;
    samples[2u * head + 1u] = r;
    head = (head + 1u) % CLOCK_PCM_RING_FRAMES;
    ++count;
    ++produced;
    return true;
}
uint32_t clock_pcm_ring_pop(int16_t *dst, uint32_t frames) {
    // Pause means silence to A2DP, but do not consume or discard queued PCM.
    if (paused) {
        memset(dst, 0, (size_t)frames * 2u * sizeof(int16_t));
        return 0u;
    }
    uint32_t take = frames < count ? frames : count;
    for (uint32_t i = 0; i < take; ++i) {
        dst[2u * i] = samples[2u * tail];
        dst[2u * i + 1u] = samples[2u * tail + 1u];
        tail = (tail + 1u) % CLOCK_PCM_RING_FRAMES;
    }
    count -= take;
    consumed += take;
    if (active && !source_eof && take < frames) ++underruns; // not at EOF
    if (take < frames) {
        memset(dst + 2u * take, 0,
               (size_t)(frames - take) * 2u * sizeof(int16_t));
    }
    return take;
}
void clock_pcm_ring_set_active(bool enabled) { active = enabled; }
void clock_pcm_ring_set_paused(bool enabled) { paused = enabled; }
bool clock_pcm_ring_paused(void) { return paused; }
void clock_pcm_ring_set_eof(bool eof) { source_eof = eof; }
bool clock_pcm_ring_eof(void) { return source_eof; }
bool clock_pcm_ring_active(void) { return active; }
uint32_t clock_pcm_ring_underruns(void) { return underruns; }
uint32_t clock_pcm_ring_consumed(void) { return consumed; }
uint32_t clock_pcm_ring_produced(void) { return produced; }
