#include "queue.h"
#include "dfplayer.h"

void audio_queue_init(audio_queue_t *q) {
    *q = (audio_queue_t){0};
}

bool audio_queue_enqueue(audio_queue_t *q, uint8_t folder, uint8_t track) {
    if (q->size >= AUDIO_QUEUE_CAPACITY || folder < 1 || folder > 99 || track < 1) {
        return false;
    }
    uint8_t tail = (uint8_t)((q->head + q->size) % AUDIO_QUEUE_CAPACITY);
    q->items[tail] = (audio_track_t){folder, track};
    ++q->size;
    return true;
}

void audio_queue_clear(audio_queue_t *q, bool pause) {
    q->head = 0;
    q->size = 0;
    q->track_started = false;
    q->seen_busy = false;
    q->pause_pending = pause;
}

void audio_queue_set_volume(audio_queue_t *q, uint8_t volume) {
    q->volume_value = volume > 30 ? 30 : volume;
    q->volume_pending = true;
}

bool audio_queue_idle(const audio_queue_t *q) {
    return q->size == 0 && !q->track_started && !q->pause_pending;
}

void audio_queue_service(audio_queue_t *q, uint64_t now_ms) {
    if (q->pause_pending) {
        if (dfplayer_command_ready(now_ms)) {
            dfplayer_pause(now_ms);
            q->pause_pending = false;
        }
        return;
    }
    if (q->volume_pending && dfplayer_command_ready(now_ms)) {
        dfplayer_set_volume(q->volume_value, now_ms);
        q->volume_pending = false;
        return;
    }
    if (q->track_started) {
        if (dfplayer_busy()) {
            q->seen_busy = true;
            return;
        }
        // Match MicroPython AudioQueue's 2500 ms fallback for clones
        // that never assert BUSY, while avoiding early premature advances.
        if (q->seen_busy || now_ms - q->started_at_ms >= 2500u) {
            q->track_started = false;
            q->seen_busy = false;
        } else {
            return;
        }
    }
    if (q->size && dfplayer_command_ready(now_ms)) {
        audio_track_t t = q->items[q->head];
        q->head = (uint8_t)((q->head + 1u) % AUDIO_QUEUE_CAPACITY);
        --q->size;
        dfplayer_play_folder_track(t.folder, t.track, now_ms);
        q->track_started = true;
        q->seen_busy = false;
        q->started_at_ms = now_ms;
    }
}
