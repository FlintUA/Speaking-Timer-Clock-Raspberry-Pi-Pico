#ifndef CLOCK_A2DP_CONTROL_H
#define CLOCK_A2DP_CONTROL_H

#include <stdbool.h>

// Connect directly to a known MAC (e.g. 04:21:44:FA:A9:0E), bypass inquiry.
bool clock_a2dp_connect(const char *address);

// These functions are implemented by the locally patched BTstack example.
// Call them from the BTstack event loop (not an interrupt or another core).
bool clock_a2dp_play_tone(void);
bool clock_a2dp_play_clip(void);
void clock_a2dp_stop_tone(void);
void clock_a2dp_print_status(void);

#endif
