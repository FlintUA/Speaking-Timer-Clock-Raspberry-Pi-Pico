#pragma once
#include <stdbool.h>
#include <stdint.h>
// Experimental optional Helix MP3 diagnostic: decodes at most 16 * 256
// stereo PCM frames to RAM and reports metadata/benchmark, no audio output.
bool mp3_probe_start(const char *path);
void mp3_probe_service(void);
bool mp3_probe_active(void);

/* Experimental MP3 to A2DP playback (complete file with pause/resume).
 * Decoding happens only in main loop. A2DP drains a 44.1 kHz PCM ring.
 */
bool mp3_play_start(const char *path, uint32_t a2dp_rate);
// Hands-free pause/resume validation for short (~2 s) MP3 recordings.
bool mp3_play_pause_test_start(const char *path, uint32_t a2dp_rate);
void mp3_play_stop(void);
bool mp3_play_pause(void);
bool mp3_play_resume(void);
bool mp3_play_active(void);
void mp3_play_status(void);
