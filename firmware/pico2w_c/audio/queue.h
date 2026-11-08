#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_QUEUE_CAPACITY 16u

typedef struct {
    uint8_t folder;
    uint8_t track;
} audio_track_t;

typedef struct {
    audio_track_t items[AUDIO_QUEUE_CAPACITY];
    uint8_t head, size;
    bool track_started, seen_busy, pause_pending;
    bool paused, resume_pending;
    uint64_t started_at_ms;
    bool volume_pending;
    uint8_t volume_value;
} audio_queue_t;

void audio_queue_init(audio_queue_t *q);
bool audio_queue_enqueue(audio_queue_t *q, uint8_t folder, uint8_t track);
void audio_queue_clear(audio_queue_t *q, bool pause);
bool audio_queue_pause(audio_queue_t *q);
bool audio_queue_resume(audio_queue_t *q);
void audio_queue_set_volume(audio_queue_t *q, uint8_t volume);
void audio_queue_service(audio_queue_t *q, uint64_t now_ms);
bool audio_queue_idle(const audio_queue_t *q);
