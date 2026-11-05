#pragma once
#include <stdbool.h>
#include <stdint.h>

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
    uint8_t clock_mode; // 0=VOICE, 1=CHIME (v2 reserved byte, FF=legacy VOICE)
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
settings_store_result_t settings_store_save(const clock_settings_t *settings);
