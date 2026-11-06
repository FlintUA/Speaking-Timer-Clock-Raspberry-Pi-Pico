#pragma once
#include <stdbool.h>
// Experimental optional Helix MP3 diagnostic: decodes at most 16 * 256
// stereo PCM frames to RAM and reports metadata/benchmark, no audio output.
bool mp3_probe_start(const char *path);
void mp3_probe_service(void);
bool mp3_probe_active(void);
