#ifndef CLOCK_A2DP_CONTROL_H
#define CLOCK_A2DP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

// Connect directly to a known MAC (e.g. 04:21:44:FA:A9:0E), bypass inquiry.
bool clock_a2dp_connect(const char *address);

// Bluetooth Classic inquiry scans for discoverable devices (not BLE-only).
// Up to 12 entries, names from inquiry EIR if available, otherwise MAC.
#define CLOCK_BT_MAX_DEVICES 12u
typedef struct {
    char address[18];
    char name[25];
    int8_t rssi;
} clock_bt_device_t;

bool clock_a2dp_scan_start(void);
void clock_a2dp_scan_stop(void);
bool clock_a2dp_scan_active(void);
unsigned clock_a2dp_scan_count(void);
bool clock_a2dp_scan_get(unsigned index, clock_bt_device_t *out);
// Select the sink, disconnecting the old one if necessary. This also changes
// the in-RAM reconnect target. Caller persists the MAC after user approval.
bool clock_a2dp_select_target(const char *address);
const char *clock_a2dp_target(void);
bool clock_a2dp_has_link_key(const char *address);
bool clock_a2dp_forget(const char *address);
void clock_a2dp_clear_target(void);

// Preferred 0..30 Bluetooth level: AVRCP Absolute Volume when available,
// otherwise gain applied to the outgoing SBC PCM. Never changes DFPlayer.
// Current remote AVRCP level can be queried for smooth first encoder step.
void clock_a2dp_set_volume(uint8_t level);
uint8_t clock_a2dp_volume(void);
bool clock_a2dp_avrcp_available(void);
uint8_t clock_a2dp_remote_volume(void); // 0..127, 255=not reported
bool clock_a2dp_pcm_volume_fallback(void);

// These functions are implemented by the locally patched BTstack example.
// Call them from the BTstack event loop (not an interrupt or another core).
bool clock_a2dp_play_tone(void);
bool clock_a2dp_play_clip(void);
void clock_a2dp_stop_tone(void);
void clock_a2dp_print_status(void);
bool clock_a2dp_stream_active(void);
bool clock_a2dp_stream_matches(const uint8_t address[6]);
uint32_t clock_a2dp_current_rate(void);

#endif
