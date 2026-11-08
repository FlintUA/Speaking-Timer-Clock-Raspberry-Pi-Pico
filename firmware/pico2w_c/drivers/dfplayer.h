#pragma once
#include <stdbool.h>
#include <stdint.h>

// Mirrors source/v3/audio.py DFPlayerTransport:
// UART0 GP16 TX, GP17 RX @9600 8N1; BUSY GP18 active-low.
// Diagnostic milestone: no automatic playback on boot.
void dfplayer_init(void);
bool dfplayer_busy(void);
bool dfplayer_command_ready(uint64_t now_ms);
void dfplayer_play_folder_track(uint8_t folder, uint8_t track, uint64_t now_ms);
void dfplayer_set_volume(uint8_t volume, uint64_t now_ms);
void dfplayer_pause(uint64_t now_ms);
void dfplayer_resume(uint64_t now_ms);

// Returns 1..255 on success or 0 when DFPlayer does not answer reliably.
uint8_t dfplayer_folder_count(uint8_t folder, uint64_t now_ms);
