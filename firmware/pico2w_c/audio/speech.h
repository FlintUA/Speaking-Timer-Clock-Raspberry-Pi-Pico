#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "queue.h"

// Folder/track layout is identical to source/v3/audio.py.
// Language selection lives in RAM for now; no flash/config changes.
typedef enum {
    SPEECH_RU = 0,
    SPEECH_DE = 1
} speech_language_t;

uint8_t speech_hours_folder(speech_language_t language);
uint8_t speech_minutes_folder(speech_language_t language);
uint8_t speech_phrases_folder(speech_language_t language);
const char *speech_language_name(speech_language_t language);
bool speech_enqueue_time(audio_queue_t *queue, speech_language_t language,
                         uint8_t hour, uint8_t minute, bool exact_hour_phrase);
void speech_enqueue_timer_start(audio_queue_t *queue, speech_language_t language,
                                uint32_t duration_seconds);
void speech_enqueue_timer_cancel(audio_queue_t *queue, speech_language_t language);
void speech_enqueue_timer_finish(audio_queue_t *queue, speech_language_t language);
