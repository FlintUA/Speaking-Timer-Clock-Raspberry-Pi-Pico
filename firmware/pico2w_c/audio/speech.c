#include "speech.h"

// Confirmed language mappings from source/v3/audio.py:
// RU hours 01, minutes 02, service phrases 07
// DE hours 11, minutes 12, service phrases 17
uint8_t speech_hours_folder(speech_language_t language) {
    return language == SPEECH_DE ? 11u : 1u;
}

uint8_t speech_minutes_folder(speech_language_t language) {
    return language == SPEECH_DE ? 12u : 2u;
}

uint8_t speech_phrases_folder(speech_language_t language) {
    return language == SPEECH_DE ? 17u : 7u;
}

const char *speech_language_name(speech_language_t language) {
    return language == SPEECH_DE ? "de" : "ru";
}

bool speech_enqueue_time(audio_queue_t *queue, speech_language_t language,
                         uint8_t hour, uint8_t minute, bool exact_hour_phrase) {
    if (hour > 23u || minute > 59u) return false;
    if (queue->size + 2u > AUDIO_QUEUE_CAPACITY) return false;

    // Same conditional minute handling as enqueue_clock_time in main.py.
    // Hour track 000 at midnight follows original MicroPython mapping.
    if (!audio_queue_enqueue(queue, speech_hours_folder(language), hour)) return false;
    if (minute > 0u || exact_hour_phrase) {
        if (!audio_queue_enqueue(queue, speech_minutes_folder(language), minute)) return false;
    }
    return true;
}

void speech_enqueue_timer_start(audio_queue_t *queue, speech_language_t language,
                                uint32_t duration_seconds) {
    audio_queue_enqueue(queue, speech_phrases_folder(language), 7u);
    if (duration_seconds % 60u) return; // MicroPython skips non-whole minutes.
    uint32_t hours = duration_seconds / 3600u;
    uint32_t minutes = (duration_seconds / 60u) % 60u;
    if (language == SPEECH_RU) {
        if (hours > 0u && hours <= 23u) {
            audio_queue_enqueue(queue, speech_hours_folder(language), (uint8_t)hours);
        }
        if (minutes > 0u) {
            audio_queue_enqueue(queue, speech_minutes_folder(language), (uint8_t)minutes);
        }
    } else if (hours == 0u && minutes > 0u) {
        // German timer duration: <number> + 'Minuten' (17/017).
        audio_queue_enqueue(queue, speech_minutes_folder(language), (uint8_t)minutes);
        audio_queue_enqueue(queue, speech_phrases_folder(language), 17u);
    }
}

void speech_enqueue_timer_cancel(audio_queue_t *queue, speech_language_t language) {
    audio_queue_enqueue(queue, speech_phrases_folder(language), 14u);
}

void speech_enqueue_timer_finish(audio_queue_t *queue, speech_language_t language) {
    audio_queue_enqueue(queue, speech_phrases_folder(language), 11u);
    audio_queue_enqueue(queue, speech_phrases_folder(language), 13u);
}
