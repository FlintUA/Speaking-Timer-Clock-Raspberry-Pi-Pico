#include "pcm_ring.h"
#include <string.h>
#include "hardware/sync.h"

// Single-producer / single-consumer ring. A2DP media processing can
// interrupt the main-loop PCM producer, so count read-modify-write is
// not safe without a short IRQ critical section. Keep sample copying
// outside the critical section to minimize Bluetooth latency.
static int16_t samples[CLOCK_PCM_RING_FRAMES * 2u];
static uint32_t head, tail, count, underruns, consumed, produced;
static bool active, paused, source_eof;
// First-fault snapshot is written in the same short PCM path as the counters.
// Avoid printf, dynamic allocation and callbacks in the audio consumer.
static clock_pcm_ring_diag_t diag;
static uint32_t pop_depth;
static void pcm_diag_capture(uint32_t code, uint32_t phase,
                             uint32_t request, uint32_t take) {
    if (diag.fault_code) return;
    diag.fault_code = code;
    diag.phase = phase;
    diag.produced = produced;
    diag.consumed = consumed;
    diag.buffered = count;
    diag.head = head;
    diag.tail = tail;
    diag.request = request;
    diag.take = take;
}
static void pcm_diag_check(uint32_t phase, uint32_t request, uint32_t take) {
    if (count > CLOCK_PCM_RING_FRAMES || head >= CLOCK_PCM_RING_FRAMES ||
        tail >= CLOCK_PCM_RING_FRAMES) {
        pcm_diag_capture(4u, phase, request, take);
    } else if (consumed > produced || produced - consumed != count) {
        pcm_diag_capture(1u, phase, request, take);
    }
}
void clock_pcm_ring_reset(void) {
    // A reset while a media callback is reading is a serious ownership
    // violation. Capture it, but do not change playback semantics.
    if (pop_depth) {
        ++diag.resets_during_pop;
        pcm_diag_capture(3u, 4u, 0u, 0u);
    } else {
        memset(&diag, 0, sizeof(diag));
    }
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
    // Publish the written frame atomically with both counters.
    // Otherwise an IRQ-driven pop() can overwrite a stale ++count value.
    uint32_t irq_state = save_and_disable_interrupts();
    ++count;
    ++produced;
    // Sample both counters only while their update is indivisible.
    pcm_diag_check(1u, 0u, 0u);
    restore_interrupts(irq_state);
    return true;
}
uint32_t clock_pcm_ring_pop(int16_t *dst, uint32_t frames) {
    ++pop_depth;
    ++diag.pop_calls;
    if (pop_depth > diag.max_pop_depth) diag.max_pop_depth = pop_depth;
    if (pop_depth > 1u) pcm_diag_capture(2u, 2u, frames, 0u);
    // Pause means silence to A2DP, but do not consume or discard queued PCM.
    if (paused) {
        memset(dst, 0, (size_t)frames * 2u * sizeof(int16_t));
        --pop_depth;
        return 0u;
    }
    uint32_t take = frames < count ? frames : count;
    for (uint32_t i = 0; i < take; ++i) {
        dst[2u * i] = samples[2u * tail];
        dst[2u * i + 1u] = samples[2u * tail + 1u];
        tail = (tail + 1u) % CLOCK_PCM_RING_FRAMES;
    }
    // The producer may resume while A2DP is draining the ring.
    // Atomically retire these frames with respect to producer ++count.
    uint32_t irq_state = save_and_disable_interrupts();
    count -= take;
    consumed += take;
    pcm_diag_check(3u, frames, take);
    restore_interrupts(irq_state);
    if (active && !source_eof && take < frames) ++underruns; // not at EOF
    if (take < frames) {
        memset(dst + 2u * take, 0,
               (size_t)(frames - take) * 2u * sizeof(int16_t));
    }
    --pop_depth;
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

void clock_pcm_ring_diagnostics(clock_pcm_ring_diag_t *snapshot) {
    if (snapshot) *snapshot = diag;
}
