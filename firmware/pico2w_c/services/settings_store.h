#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "alarm_engine.h"

#define CLOCK_BT_KNOWN_MAX 4u
#define CLOCK_BT_NAME_SIZE 25u
typedef struct {
    uint8_t address[6];
    char name[CLOCK_BT_NAME_SIZE];
} clock_bt_known_t;

// Two provisional 4 KiB sectors, excluding the final sector (RP2350-E10 UF2 workaround).
// A/B records preserve a previous valid record when saving a new one.
// The library refuses to erase any nonblank sector that is not one of our records.
typedef struct {
    uint8_t language;  // 0=RU, 1=DE
    uint8_t volume;    // 0..30
    bool exact_hour;
    bool quiet_enabled;
    uint8_t quiet_start; // 0..23
    uint8_t quiet_end;   // 0..23
    bool half_hour_enabled;
    uint8_t clock_mode; // 0=VOICE, 1=CHIME
    alarm_entry_t alarms[CLOCK_ALARM_COUNT]; // v3/v4; old v1/v2 defaults OFF
    // v4: independent preferred output flags. 0=LOCAL, 1=A2DP JBL.
    // Flash stores preferences, not connection state; LOCAL fallback remains mandatory.
    uint8_t audio_manual_jbl;
    uint8_t audio_hourly_jbl;
    uint8_t audio_timer_jbl;
    uint8_t audio_alarm_jbl;
    // v5: selected A2DP sink Bluetooth MAC (all zeros = legacy build target).
    uint8_t bt_target_addr[6];
    // v6: independent Bluetooth volume step 0..30, legacy defaults to 16.
    uint8_t bt_volume;
    // v7: successful A2DP sinks. BTstack stores pairing keys separately.
    uint8_t bt_known_count;
    clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
    // v8: provisioned over USB; never printed by status/diagnostics.
    char wifi_ssid[33];
    char wifi_password[65];
} clock_settings_t;

typedef enum {
    SETTINGS_STORE_OK = 0,
    SETTINGS_STORE_EMPTY,
    SETTINGS_STORE_OCCUPIED,
    SETTINGS_STORE_IMAGE_OVERLAP,
    SETTINGS_STORE_UNSUPPORTED,
    SETTINGS_STORE_FLASH_ERROR,
    SETTINGS_STORE_VERIFY_ERROR
} settings_store_result_t;

const char *settings_store_result_name(settings_store_result_t result);
bool settings_store_layout_safe(void);
void settings_store_print_info(void);
// Read-only sector fingerprint, no flash program/erase operations.
void settings_store_inspect(void);
// Print the final 256 bytes of flash as hex and their CRC32; read-only.
void settings_store_dump_tail(void);
settings_store_result_t settings_store_load(clock_settings_t *out);
// Writes occur only when requested by the application; load never migrates.
// Current writes use v8; historical v1-v7 records remain readable.
settings_store_result_t settings_store_save(const clock_settings_t *settings);
