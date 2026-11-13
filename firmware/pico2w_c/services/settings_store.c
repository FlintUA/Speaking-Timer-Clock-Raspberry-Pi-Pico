#include "settings_store.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "pico/flash.h"
#include "pico/platform.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

#if !defined(PICO_FLASH_SIZE_BYTES)
#error "Settings store requires an explicit PICO_FLASH_SIZE_BYTES board definition"
#endif
#if PICO_FLASH_SIZE_BYTES < (1024 * 1024)
#error "Flash unexpectedly small; do not enable settings storage"
#endif

// Flash map (4 KiB sectors; RP2350):
// -6, -5: NEW clock A/B settings (safe from BTstack).
// -4: previous clock slot0; READ ONLY for migration/fallback.
// -3, -2: Pico SDK BTstack's two flash banks (DO NOT TOUCH).
// -1: reserved for RP2350-E10 UF2 bootrom workaround (DO NOT TOUCH).
// Never erase sectors with unknown data or the legacy clock sector.
// The active clock record is migrated to the new A/B area only upon an
// explicit save/autosave after a settings change or claimed birthday occurrence.
// Merely loading old settings never migrates them.
#define SLOT0_OFFSET (PICO_FLASH_SIZE_BYTES - 6u * FLASH_SECTOR_SIZE)
#define SLOT1_OFFSET (PICO_FLASH_SIZE_BYTES - 5u * FLASH_SECTOR_SIZE)
#define LEGACY_SLOT_OFFSET (PICO_FLASH_SIZE_BYTES - 4u * FLASH_SECTOR_SIZE)
#ifdef CLOCK_ENABLE_A2DP
#include "pico/btstack_flash_bank.h"
_Static_assert(SLOT1_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET,
               "Clock settings Flash overlaps BTstack storage");
_Static_assert(LEGACY_SLOT_OFFSET + FLASH_SECTOR_SIZE <= PICO_FLASH_BANK_STORAGE_OFFSET,
               "Legacy clock Flash overlaps BTstack storage");
#endif
#define RECORD_MAGIC 0x53434c4bu /* SCLK */
#define RECORD_VERSION 17u
#define RECORD_V16 16u
#define RECORD_V15 15u
#define RECORD_V14 14u
#define RECORD_V13 13u
#define STORED_BIRTHDAY_COUNT 5u
#define RECORD_V12 12u
#define RECORD_V11 11u
#define RECORD_V10 10u
#define RECORD_V9 9u
#define RECORD_V8 8u
#define RECORD_V7 7u
#define RECORD_V6 6u
#define RECORD_V5 5u
#define RECORD_V4 4u
#define RECORD_V3 3u
#define RECORD_V1 1u
#define RECORD_V2 2u
#define IMAGE_SAFETY_MARGIN FLASH_SECTOR_SIZE

extern uint8_t __flash_binary_end;

// Version 1 stays readable and erasable. Version 2 adds quiet/half-hour
// settings without changing A/B sector offsets, magic or sequence numbering.
typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, reserved;
    uint32_t crc32;
} settings_record_v1_t;

typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, reserved; // v2 reserved: FF=legacy voice, 0=voice, 1=chime
    uint32_t crc32;
} settings_record_v2_t;

// Explicit compact on-flash layout, independent of alarm_entry_t ABI padding.
// v3 keeps all v2 setting bytes in the same locations. Each alarm occupies
// five bytes: enabled, hour, minute, sound, track. Three FF reserved bytes
// place CRC32 at offset 48, total record 52 bytes.
typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint32_t crc32;
} settings_record_t;

_Static_assert(sizeof(settings_record_v1_t) == 20, "Unexpected v1 size");
_Static_assert(sizeof(settings_record_v2_t) == 24, "Unexpected v2 size");
_Static_assert(offsetof(settings_record_v2_t, crc32) == 20, "Unexpected v2 CRC offset");
// v4 retains the complete v3 prefix (except the old CRC), then adds four
// independent output preferences. CRC is moved to byte 52.
typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint32_t crc32;
} settings_record_v4_t;

// v5 extends v4 by a six-byte Bluetooth sink address. Two reserved bytes
// keep the CRC naturally aligned; v1-v4 are still read using their own CRC.
typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint32_t crc32;
} settings_record_v5_t;

// v6 adds independently stored Bluetooth output volume. Three reserved
// bytes maintain 4-byte CRC alignment; legacy records remain readable.
typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint8_t bt_volume;
    uint8_t reserved_v6[3];
    uint32_t crc32;
} settings_record_v6_t;

typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint8_t bt_volume;
    uint8_t reserved_v6[3];
    uint8_t bt_known_count;
    uint8_t reserved_v7[3];
    clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
    uint32_t crc32;
} settings_record_v7_t;

typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint8_t bt_volume;
    uint8_t reserved_v6[3];
    uint8_t bt_known_count;
    uint8_t reserved_v7[3];
    clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
    char wifi_ssid[33];
    char wifi_password[65];
    uint8_t reserved_v8[2];
    uint32_t crc32;
} settings_record_v8_t;

typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint8_t bt_volume;
    uint8_t reserved_v6[3];
    uint8_t bt_known_count;
    uint8_t reserved_v7[3];
    clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
    char wifi_ssid[33];
    char wifi_password[65];
    uint8_t reserved_v8[2];
    uint8_t alarm_weekdays[CLOCK_ALARM_COUNT];
    uint8_t reserved_v9[3];
    uint32_t crc32;
} settings_record_v9_t;

typedef struct {
    uint32_t magic, version, sequence;
    uint8_t language, volume, exact_hour, quiet_enabled;
    uint8_t quiet_start, quiet_end, half_hour_enabled, clock_mode;
    uint8_t alarms[CLOCK_ALARM_COUNT][5];
    uint8_t reserved[3];
    uint8_t audio_manual_jbl, audio_hourly_jbl;
    uint8_t audio_timer_jbl, audio_alarm_jbl;
    uint8_t bt_target_addr[6];
    uint8_t reserved_v5[2];
    uint8_t bt_volume;
    uint8_t reserved_v6[3];
    uint8_t bt_known_count;
    uint8_t reserved_v7[3];
    clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
    char wifi_ssid[33];
    char wifi_password[65];
    uint8_t reserved_v8[2];
    uint8_t alarm_weekdays[CLOCK_ALARM_COUNT];
    uint8_t reserved_v9[3];
    uint8_t birthdays[STORED_BIRTHDAY_COUNT][8];
    display_light_t display_light;
    uint8_t reserved_v11;
    phone_settings_t phone;
    uint16_t birth_years[STORED_BIRTHDAY_COUNT];
    uint32_t crc32;
} settings_record_current_t;

// v14 extends the immutable v13 prefix. Its checksum covers the prefix and
// all additional birthdays; the old v13 checksum field is set to erased FF.
typedef struct {
    settings_record_current_t prefix;
    uint8_t birthdays[CLOCK_BIRTHDAY_COUNT - STORED_BIRTHDAY_COUNT][8];
    uint16_t birth_years[CLOCK_BIRTHDAY_COUNT - STORED_BIRTHDAY_COUNT];
    uint32_t crc32;
} settings_record_v14_t;

// v15 keeps every existing byte offset immutable, including all 15 birthdays.
// Both embedded historical checksum fields are FF; the final CRC covers all.
typedef struct {
    settings_record_v14_t prefix;
    uint8_t timer_sound, timer_track, timer_voice;
    uint8_t reserved;
    uint32_t crc32;
} settings_record_v15_t;
typedef struct {
    settings_record_v15_t prefix;
    uint8_t timer_random;
    uint8_t reserved[3];
    uint32_t crc32;
} settings_record_v16_t;
typedef struct {
    settings_record_v16_t prefix;
    uint8_t timer_warning;
    uint8_t reserved[3];
    uint32_t crc32;
} settings_record_v17_t;
_Static_assert(offsetof(settings_record_v17_t,crc32)==636u,"Unexpected v17 CRC offset");
_Static_assert(sizeof(settings_record_v17_t)==640u,"Unexpected v17 size");
_Static_assert(sizeof(settings_record_v17_t)<=3u*FLASH_PAGE_SIZE,"v17 exceeds three pages");
_Static_assert(offsetof(settings_record_v16_t,crc32)==628u,"Unexpected v16 CRC offset");
_Static_assert(sizeof(settings_record_v16_t)==632u,"Unexpected v16 size");
_Static_assert(sizeof(settings_record_v16_t)<=3u*FLASH_PAGE_SIZE,"v16 exceeds three pages");
_Static_assert(offsetof(settings_record_v15_t, crc32) == 620u, "Unexpected v15 CRC offset");
_Static_assert(sizeof(settings_record_v15_t) == 624u, "Unexpected v15 size");
_Static_assert(sizeof(settings_record_v15_t) <= 3u * FLASH_PAGE_SIZE, "v15 exceeds three flash pages");

_Static_assert(sizeof(settings_record_t) == 52, "Unexpected v3 size");
_Static_assert(offsetof(settings_record_t, crc32) == 48, "Unexpected v3 CRC offset");
_Static_assert(sizeof(settings_record_v4_t) == 56, "Unexpected v4 size");
_Static_assert(offsetof(settings_record_v4_t, crc32) == 52, "Unexpected v4 CRC offset");
_Static_assert(sizeof(settings_record_v5_t) == 64, "Unexpected v5 size");
_Static_assert(offsetof(settings_record_v5_t, crc32) == 60, "Unexpected v5 CRC offset");
_Static_assert(sizeof(settings_record_v6_t) == 68, "Unexpected v6 size");
_Static_assert(offsetof(settings_record_v6_t, crc32) == 64, "Unexpected v6 CRC offset");
_Static_assert(sizeof(clock_bt_known_t) == 31, "Unexpected known-device size");
_Static_assert(sizeof(settings_record_v7_t) == 196, "Unexpected v7 size");
_Static_assert(offsetof(settings_record_v8_t,crc32) == 292, "Unexpected v8 CRC offset");
_Static_assert(sizeof(settings_record_v8_t) <= 2u * FLASH_PAGE_SIZE, "v8 exceeds two flash pages");
_Static_assert(offsetof(settings_record_v9_t,crc32)==300, "Unexpected v9 CRC offset");
_Static_assert(sizeof(settings_record_v9_t)<=2u*FLASH_PAGE_SIZE, "v9 exceeds two flash pages");
#define RECORD_V10_CRC_OFFSET offsetof(settings_record_current_t, display_light)
_Static_assert(RECORD_V10_CRC_OFFSET == 340, "Unexpected v10 CRC offset");
#define RECORD_V11_CRC_OFFSET offsetof(settings_record_current_t, phone)
_Static_assert(RECORD_V11_CRC_OFFSET == 344, "Unexpected v11 CRC offset");
#define RECORD_V12_CRC_OFFSET 500u
_Static_assert(offsetof(settings_record_current_t, birth_years) == 498, "Unexpected birth year offset");
_Static_assert(offsetof(settings_record_current_t, crc32) == 508, "Unexpected v13 CRC offset");
_Static_assert(sizeof(settings_record_current_t) <= 2u * FLASH_PAGE_SIZE, "v13 exceeds two flash pages");
_Static_assert(sizeof(settings_record_v14_t) <= 3u * FLASH_PAGE_SIZE, "v14 exceeds three flash pages");
_Static_assert(offsetof(settings_record_v14_t, crc32) == 612u, "Unexpected v14 CRC offset");

static uint32_t record_crc_bytes(const void *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    const uint8_t *ptr = (const uint8_t *)data;
    for (size_t i = 0; i < length; ++i) {
        crc ^= ptr[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    return crc ^ 0xffffffffu;
}

static uint32_t slot_offset(unsigned slot) {
    return slot == 0u ? SLOT0_OFFSET :
           slot == 1u ? SLOT1_OFFSET : LEGACY_SLOT_OFFSET;
}

static const uint8_t *slot_bytes(unsigned slot) {
    return (const uint8_t *)(uintptr_t)(XIP_BASE + slot_offset(slot));
}

static bool phone_settings_valid(const phone_settings_t *phone) {
    if (phone->home_trusted > 1u || phone->password[PHONE_PASSWORD_LEN] ||
        (phone->password[0] && !phone_hex_valid(phone->password, PHONE_PASSWORD_LEN))) return false;
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i) {
        const phone_browser_t *browser = &phone->browsers[i];
        if (browser->scope > PHONE_SCOPE_DIRECT || browser->token[PHONE_TOKEN_LEN] ||
            (browser->scope ? !phone_hex_valid(browser->token, PHONE_TOKEN_LEN) : browser->token[0] != 0))
            return false;
    }
    return true;
}

static bool record_valid(const settings_record_current_t *r) {
    if (r->magic != RECORD_MAGIC || r->language > 1u ||
        r->volume > 30u || r->exact_hour > 1u) return false;
    if (r->version == RECORD_V1) {
        settings_record_v1_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v1_t, crc32));
    }
    if (r->version == RECORD_V2) {
        settings_record_v2_t old;
        memcpy(&old, r, sizeof(old));
        return old.quiet_enabled <= 1u && old.quiet_start <= 23u &&
               old.quiet_end <= 23u && old.half_hour_enabled <= 1u &&
               (old.reserved == 0xffu || old.reserved <= 1u) &&
               old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v2_t, crc32));
    }
    if (r->version != RECORD_V3 && r->version != RECORD_V4 &&
         r->version != RECORD_V5 && r->version != RECORD_V6 && r->version != RECORD_V7 && r->version != RECORD_V8 && r->version != RECORD_V9 &&
         r->version != RECORD_V10 && r->version != RECORD_V11 &&
         r->version != RECORD_V12 && r->version != RECORD_V13 && r->version != RECORD_V14 && r->version != RECORD_V15 && r->version != RECORD_V16 &&
         r->version != RECORD_VERSION) return false;
    if (r->quiet_enabled > 1u || r->quiet_start > 23u ||
        r->quiet_end > 23u || r->half_hour_enabled > 1u ||
        r->clock_mode > 1u) return false;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const uint8_t *a = r->alarms[i];
        if (a[0] > 1u || a[1] > 23u || a[2] > 59u ||
            a[3] > (r->version >= RECORD_V16 ? 2u : 1u) || a[4] < 1u ||
            (r->version < RECORD_V8 && a[4] > 45u)) return false;
    }
    if (r->reserved[0] != 0xffu || r->reserved[1] != 0xffu ||
        r->reserved[2] != 0xffu) return false;
    if (r->version == RECORD_V3) {
        settings_record_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_t, crc32));
    }
    if (r->audio_manual_jbl > 1u || r->audio_hourly_jbl > 1u ||
        r->audio_timer_jbl > 1u || r->audio_alarm_jbl > 1u) return false;
    if (r->version == RECORD_V4) {
        settings_record_v4_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v4_t, crc32));
    }
    if (r->reserved_v5[0] != 0xffu || r->reserved_v5[1] != 0xffu)
        return false;
    if (r->version == RECORD_V5) {
        settings_record_v5_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v5_t, crc32));
    }
    if (r->bt_volume > 30u || r->reserved_v6[0] != 0xffu ||
        r->reserved_v6[1] != 0xffu || r->reserved_v6[2] != 0xffu)
        return false;
    if (r->version == RECORD_V6) {
        settings_record_v6_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v6_t, crc32));
    }
    if (r->bt_known_count > CLOCK_BT_KNOWN_MAX ||
        r->reserved_v7[0] != 0xffu || r->reserved_v7[1] != 0xffu ||
        r->reserved_v7[2] != 0xffu) return false;
    for (unsigned i = 0; i < CLOCK_BT_KNOWN_MAX; ++i) {
        if (i < r->bt_known_count &&
            !memchr(r->bt_known[i].name, 0, CLOCK_BT_NAME_SIZE)) return false;
    }
    if (r->version == RECORD_V7) {
        settings_record_v7_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 == record_crc_bytes(&old, offsetof(settings_record_v7_t, crc32));
    }
    if (!memchr(r->wifi_ssid,0,sizeof(r->wifi_ssid)) ||
        !memchr(r->wifi_password,0,sizeof(r->wifi_password)) ||
        r->reserved_v8[0] != 0xffu || r->reserved_v8[1] != 0xffu) return false;
    if(r->version==RECORD_V8) {
        settings_record_v8_t old;
        memcpy(&old,r,sizeof(old));
        return old.crc32==record_crc_bytes(&old,offsetof(settings_record_v8_t,crc32));
    }
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i)
        if(!r->alarm_weekdays[i] || r->alarm_weekdays[i]>CLOCK_ALARM_DAYS_ALL) return false;
    for(unsigned i=0;i<sizeof(r->reserved_v9);++i)
        if(r->reserved_v9[i]!=0xffu) return false;
    if (r->version==RECORD_V9) {
        settings_record_v9_t old;
        memcpy(&old,r,sizeof(old));
        return old.crc32==record_crc_bytes(&old,offsetof(settings_record_v9_t,crc32));
    }
    for (unsigned i=0;i<STORED_BIRTHDAY_COUNT;++i) {
        const uint8_t *b=r->birthdays[i];
        birthday_t date={.enabled=b[0]!=0,.month=b[1],.day=b[2],.hour=b[3],.minute=b[4],
            .last_year=(uint16_t)(b[5]|((uint16_t)b[6]<<8)),
            .birth_year=r->version >= RECORD_V13 ? r->birth_years[i] : 0};
        if (b[0]>1u || b[7]!=0xffu || !birthday_valid(&date)) return false;
    }
    if (r->version == RECORD_V10) {
        uint32_t old_crc;
        memcpy(&old_crc, (const uint8_t *)r + RECORD_V10_CRC_OFFSET, sizeof(old_crc));
        return old_crc == record_crc_bytes(r, RECORD_V10_CRC_OFFSET);
    }
    if (r->reserved_v11 != 0xffu || !display_light_valid(r->display_light)) return false;
    if (r->version == RECORD_V11) {
        uint32_t old_crc;
        memcpy(&old_crc, (const uint8_t *)r + RECORD_V11_CRC_OFFSET, sizeof(old_crc));
        return old_crc == record_crc_bytes(r, RECORD_V11_CRC_OFFSET);
    }
    if (!phone_settings_valid(&r->phone)) return false;
    if (r->version == RECORD_V12) {
        uint32_t old_crc;
        memcpy(&old_crc, (const uint8_t *)r + RECORD_V12_CRC_OFFSET, sizeof(old_crc));
        return old_crc == record_crc_bytes(r, RECORD_V12_CRC_OFFSET);
    }
    if (r->version >= RECORD_V14) return r->crc32 == UINT32_MAX;
    return r->crc32 == record_crc_bytes(r, offsetof(settings_record_current_t, crc32));
}

static bool record_v14_valid(const uint8_t *bytes) {
    settings_record_v14_t r;
    memcpy(&r, bytes, sizeof(r));
    if (!record_valid(&r.prefix) || r.prefix.version < RECORD_V14 ||
        (r.prefix.version == RECORD_V14 ?
            r.crc32 != record_crc_bytes(&r, offsetof(settings_record_v14_t, crc32)) :
            r.crc32 != UINT32_MAX)) return false;
    for (unsigned i = 0; i < CLOCK_BIRTHDAY_COUNT - STORED_BIRTHDAY_COUNT; ++i) {
        const uint8_t *b = r.birthdays[i];
        birthday_t date = {.enabled = b[0] != 0, .month = b[1], .day = b[2],
            .hour = b[3], .minute = b[4],
            .last_year = (uint16_t)(b[5] | ((uint16_t)b[6] << 8)),
            .birth_year = r.birth_years[i]};
        if (b[0] > 1u || b[7] != 0xffu || !birthday_valid(&date)) return false;
    }
    return true;
}

static bool record_v15_valid(const uint8_t *bytes) {
    settings_record_v15_t r;
    memcpy(&r, bytes, sizeof(r));
    return record_v14_valid(bytes) && r.prefix.prefix.version >= RECORD_V15 &&
        r.timer_sound <= 2u && r.timer_track >= 1u && r.timer_voice <= 1u &&
        r.reserved == 0xffu &&
        (r.prefix.prefix.version == RECORD_V15 ?
            r.crc32 == record_crc_bytes(&r, offsetof(settings_record_v15_t, crc32)) : r.crc32 == UINT32_MAX);
}

static bool record_v16_valid(const uint8_t *bytes) {
    settings_record_v16_t r; memcpy(&r,bytes,sizeof(r));
    return record_v15_valid(bytes) && r.prefix.prefix.prefix.version>=RECORD_V16 &&
        r.timer_random<=1u && r.reserved[0]==0xffu && r.reserved[1]==0xffu && r.reserved[2]==0xffu &&
        (r.prefix.prefix.prefix.version==RECORD_V16 ?
            r.crc32==record_crc_bytes(&r,offsetof(settings_record_v16_t,crc32)) : r.crc32==UINT32_MAX);
}
static bool record_v17_valid(const uint8_t *bytes) {
    settings_record_v17_t r;memcpy(&r,bytes,sizeof(r));
    return record_v16_valid(bytes) && r.prefix.prefix.prefix.prefix.version==RECORD_VERSION &&
        r.timer_warning<=1u && r.reserved[0]==0xffu && r.reserved[1]==0xffu && r.reserved[2]==0xffu &&
        r.crc32==record_crc_bytes(&r,offsetof(settings_record_v17_t,crc32));
}
static size_t record_size(const settings_record_current_t *r) {
    if (r->version == RECORD_V1) return sizeof(settings_record_v1_t);
    if (r->version == RECORD_V2) return sizeof(settings_record_v2_t);
    if (r->version == RECORD_V3) return sizeof(settings_record_t);
    if (r->version == RECORD_V4) return sizeof(settings_record_v4_t);
    if (r->version == RECORD_V5) return sizeof(settings_record_v5_t);
    if (r->version == RECORD_V6) return sizeof(settings_record_v6_t);
    if (r->version == RECORD_V7) return sizeof(settings_record_v7_t);
    if(r->version==RECORD_V8) return sizeof(settings_record_v8_t);
    if(r->version==RECORD_V9) return sizeof(settings_record_v9_t);
    if (r->version == RECORD_V10) return RECORD_V10_CRC_OFFSET + sizeof(uint32_t);
    if (r->version == RECORD_V11) return RECORD_V11_CRC_OFFSET + sizeof(uint32_t);
    if (r->version == RECORD_V12) return RECORD_V12_CRC_OFFSET + sizeof(uint32_t);
    if (r->version == RECORD_V14) return sizeof(settings_record_v14_t);
    if (r->version == RECORD_V15) return sizeof(settings_record_v15_t);
    if (r->version == RECORD_V16) return sizeof(settings_record_v16_t);
    if (r->version == RECORD_VERSION) return sizeof(settings_record_v17_t);
    return sizeof(settings_record_current_t);
}

static bool sector_blank(unsigned slot) {
    const uint8_t *bytes = slot_bytes(slot);
    for (size_t i = 0; i < FLASH_SECTOR_SIZE; ++i) {
        if (bytes[i] != 0xffu) return false;
    }
    return true;
}

static bool sector_owned(unsigned slot) {
    // A valid header alone is insufficient: don't erase a sector containing
    // unrelated data after our record page (e.g. a legacy filesystem).
    settings_record_current_t r;
    const uint8_t *bytes = slot_bytes(slot);
    memcpy(&r, bytes, sizeof(r));
    if (!record_valid(&r) || (r.version == RECORD_V14 && !record_v14_valid(bytes)) ||
        (r.version == RECORD_V15 && !record_v15_valid(bytes)) ||
        (r.version == RECORD_V16 && !record_v16_valid(bytes)) ||
        (r.version == RECORD_VERSION && !record_v17_valid(bytes))) return false;
    for (size_t i = record_size(&r); i < FLASH_SECTOR_SIZE; ++i) {
        if (bytes[i] != 0xffu) return false;
    }
    return true;
}

bool settings_store_layout_safe(void) {
    // Runtime firmware image guard; leave a whole additional sector gap.
    // Never touch either sector if build output would collide with them.
    uintptr_t image_end = (uintptr_t)&__flash_binary_end;
    uintptr_t limit = (uintptr_t)XIP_BASE + SLOT0_OFFSET - IMAGE_SAFETY_MARGIN;
    return image_end >= (uintptr_t)XIP_BASE &&
           image_end < limit;
}

static bool newest_valid(unsigned *slot, settings_record_current_t *result) {
    // Include the old slot0 as a read-only fallback so the existing clock
    // configuration survives its first firmware upgrade with the new map.
    bool have = false;
    for (unsigned candidate = 0u; candidate < 3u; ++candidate) {
        if (!sector_owned(candidate)) continue;
        settings_record_current_t record;
        memcpy(&record, slot_bytes(candidate), sizeof(record));
        if (!have || (int32_t)(record.sequence - result->sequence) > 0) {
            *slot = candidate; // 2 means legacy, never a write target.
            *result = record;
            have = true;
        }
    }
    return have;
}

const char *settings_store_result_name(settings_store_result_t r) {
    switch (r) {
        case SETTINGS_STORE_OK: return "OK";
        case SETTINGS_STORE_EMPTY: return "EMPTY";
        case SETTINGS_STORE_OCCUPIED: return "OCCUPIED (unknown flash contents)";
        case SETTINGS_STORE_IMAGE_OVERLAP: return "IMAGE_OVERLAP";
        case SETTINGS_STORE_UNSUPPORTED: return "UNSUPPORTED";
        case SETTINGS_STORE_FLASH_ERROR: return "FLASH_ERROR";
        case SETTINGS_STORE_VERIFY_ERROR: return "VERIFY_ERROR";
    }
    return "UNKNOWN";
}

void settings_store_print_info(void) {
    printf("[FLASH] size=%lu KiB slots=0x%08lx,0x%08lx image_end=0x%08lx guard=%s\n",
           (unsigned long)(PICO_FLASH_SIZE_BYTES / 1024u),
           (unsigned long)SLOT0_OFFSET, (unsigned long)SLOT1_OFFSET,
           (unsigned long)(uintptr_t)&__flash_binary_end,
           settings_store_layout_safe() ? "OK" : "FAIL");
    for (unsigned i = 0; i < 3u; ++i) {
        settings_record_current_t r;
        memcpy(&r, slot_bytes(i), sizeof(r));
        bool owned = sector_owned(i);
        printf("[FLASH] %s=%s sequence=%lu version=%lu offset=0x%08lx%s\n",
               i == 2u ? "legacy_slot0" : (i == 0u ? "slot0" : "slot1"),
               owned ? "VALID" : (sector_blank(i) ? "BLANK" : "OCCUPIED"),
               owned ? (unsigned long)r.sequence : 0ul,
               owned ? (unsigned long)r.version : 0ul,
               (unsigned long)slot_offset(i),
               i == 2u ? " READ_ONLY" : "");
    }
#ifdef CLOCK_ENABLE_A2DP
    printf("[FLASH] BTstack banks start=0x%08lx (reserved, not inspected)\n",
           (unsigned long)PICO_FLASH_BANK_STORAGE_OFFSET);
#endif
}


void settings_store_inspect(void) {
    // Read-only sampling from the two reserved sectors. We do not try to
    // interpret or mount prior MicroPython filesystem contents.
    puts("[FLASH] Inspect is READ ONLY; hexadecimal samples are NOT proof of filesystem type.");
    for (unsigned slot = 0u; slot < 3u; ++slot) {
        const uint8_t *data = slot_bytes(slot);
        size_t ff_count = 0, zero_count = 0, other_count = 0;
        size_t first_non_ff = FLASH_SECTOR_SIZE;
        size_t last_non_ff = FLASH_SECTOR_SIZE;
        for (size_t i = 0; i < FLASH_SECTOR_SIZE; ++i) {
            uint8_t b = data[i];
            if (b == 0xffu) {
                ++ff_count;
            } else {
                if (first_non_ff == FLASH_SECTOR_SIZE) first_non_ff = i;
                last_non_ff = i;
                if (b == 0u) ++zero_count;
                else ++other_count;
            }
        }
        printf("[FLASH] %s offset=0x%08lx status=%s ff=%lu zero=%lu other=%lu\n",
               slot == 2u ? "legacy_slot0" : (slot == 0u ? "slot0" : "slot1"),
               (unsigned long)slot_offset(slot),
               sector_owned(slot) ? "VALID" : (sector_blank(slot) ? "BLANK" : "OCCUPIED"),
               (unsigned long)ff_count, (unsigned long)zero_count,
               (unsigned long)other_count);
        if (first_non_ff != FLASH_SECTOR_SIZE) {
            printf("[FLASH] slot%u first_non_ff=0x%03lx last_non_ff=0x%03lx\n",
                   slot, (unsigned long)first_non_ff, (unsigned long)last_non_ff);
        }
        // Print 16 bytes only, never dump full content that may include private
        // configuration files. This is only a signature hint, not a diagnosis.
        printf("[FLASH] slot%u head16=", slot);
        for (unsigned j = 0; j < 16u; ++j) {
            printf("%02x", (unsigned)data[j]);
        }
        putchar('\n');
    }
}

void settings_store_dump_tail(void) {
    // Read ONLY. Print the last 256-byte flash page so we can compare its
    // contents across power cycles. No flash erase/program API is invoked.
    const uint32_t offset = PICO_FLASH_SIZE_BYTES - FLASH_PAGE_SIZE;
    const uint8_t *data = (const uint8_t *)(uintptr_t)(XIP_BASE + offset);
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
        }
    }
    crc ^= 0xffffffffu;

    printf("[FLASH] tail offset=0x%08lx size=%u (READ ONLY)\n",
           (unsigned long)offset, (unsigned)FLASH_PAGE_SIZE);
    for (unsigned row = 0; row < FLASH_PAGE_SIZE; row += 16u) {
        printf("[FLASH] %08lx:", (unsigned long)(offset + row));
        for (unsigned col = 0; col < 16u; ++col) {
            printf(" %02x", (unsigned)data[row + col]);
        }
        putchar('\n');
    }
    printf("[FLASH] tail_crc32=%08lx\n", (unsigned long)crc);
}

settings_store_result_t settings_store_load(clock_settings_t *out) {
    if (!settings_store_layout_safe()) return SETTINGS_STORE_IMAGE_OVERLAP;
    unsigned slot;
    settings_record_current_t r;
    if (!newest_valid(&slot, &r)) return SETTINGS_STORE_EMPTY;
    if (out) {
        // Older versions have no audio preferences. Zero all fields before
        // decoding so v1-v3 always boot with four LOCAL defaults.
        memset(out, 0, sizeof(*out));
        out->timer_track = 1u; out->timer_voice = 1u;
        if (r.version >= RECORD_V15) {
            settings_record_v15_t extended;
            memcpy(&extended, slot_bytes(slot), sizeof(extended));
            out->timer_voice = extended.timer_voice;
            out->timer_sound = extended.timer_sound;
            out->timer_track = extended.timer_track;
        }
        if (r.version >= RECORD_V16) {
            settings_record_v16_t extended; memcpy(&extended,slot_bytes(slot),sizeof(extended));
            out->timer_random=extended.timer_random;
        }
        if (r.version == RECORD_VERSION) {
            settings_record_v17_t extended;memcpy(&extended,slot_bytes(slot),sizeof(extended));
            out->timer_warning=extended.timer_warning;
        }
        out->language = r.language;
        out->volume = r.volume;
        out->exact_hour = r.exact_hour != 0u;
        out->quiet_enabled = r.version == RECORD_V1 ? true : r.quiet_enabled != 0u;
        out->quiet_start = r.version == RECORD_V1 ? 22u : r.quiet_start;
        out->quiet_end = r.version == RECORD_V1 ? 7u : r.quiet_end;
        out->half_hour_enabled = r.version == RECORD_V1 ? false :
                                 r.half_hour_enabled != 0u;
        // v2 records before clock-mode support stored FF in reserved.
        // v3 uses this same byte at offset 19 as clock_mode.
        out->clock_mode = r.version == RECORD_V1 || r.clock_mode == 0xffu
                              ? 0u : r.clock_mode;
        alarm_defaults(out->alarms);
        birthday_defaults(out->birthdays);
        if (r.version >= RECORD_V3) {
            for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
                const uint8_t *src = r.alarms[i];
                out->alarms[i].enabled = src[0] != 0u;
                out->alarms[i].hour = src[1];
                out->alarms[i].minute = src[2];
                out->alarms[i].sound = (alarm_sound_t)src[3];
                out->alarms[i].track = src[4];
                if(r.version>=RECORD_V9) out->alarms[i].weekdays=r.alarm_weekdays[i];
            }
        }
        if (r.version>=RECORD_V10) {
            for (unsigned i=0;i<STORED_BIRTHDAY_COUNT;++i) {
                const uint8_t *b=r.birthdays[i];
                out->birthdays[i]=(birthday_t){.enabled=b[0]!=0,.month=b[1],.day=b[2],.hour=b[3],.minute=b[4],
                    .last_year=(uint16_t)(b[5]|((uint16_t)b[6]<<8)),
                    .birth_year=r.version >= RECORD_V13 ? r.birth_years[i] : 0};
            }
            if (r.version >= RECORD_V14) {
                settings_record_v14_t extended;
                memcpy(&extended, slot_bytes(slot), sizeof(extended));
                for (unsigned i = STORED_BIRTHDAY_COUNT; i < CLOCK_BIRTHDAY_COUNT; ++i) {
                    unsigned j = i - STORED_BIRTHDAY_COUNT;
                    const uint8_t *b = extended.birthdays[j];
                    out->birthdays[i] = (birthday_t){.enabled = b[0] != 0,
                        .month = b[1], .day = b[2], .hour = b[3], .minute = b[4],
                        .last_year = (uint16_t)(b[5] | ((uint16_t)b[6] << 8)),
                        .birth_year = extended.birth_years[j]};
                }
            }
        }
        if (r.version >= RECORD_V12) out->phone = r.phone;
        out->display_light = r.version >= RECORD_V11 ? r.display_light :
            (display_light_t){DISPLAY_LIGHT_OFF, DISPLAY_LIGHT_DAY_DEFAULT, DISPLAY_LIGHT_NIGHT_DEFAULT};
        if (r.version >= RECORD_V4) {
            out->audio_manual_jbl = r.audio_manual_jbl;
            out->audio_hourly_jbl = r.audio_hourly_jbl;
            out->audio_timer_jbl = r.audio_timer_jbl;
            out->audio_alarm_jbl = r.audio_alarm_jbl;
        }
        if (r.version >= RECORD_V5)
            memcpy(out->bt_target_addr, r.bt_target_addr, sizeof(out->bt_target_addr));
        out->bt_volume = r.version >= RECORD_V6 ? r.bt_volume : 16u;
        if (r.version >= RECORD_V7) {
            out->bt_known_count = r.bt_known_count;
            memcpy(out->bt_known, r.bt_known, sizeof(out->bt_known));
        }
        if (r.version >= RECORD_V8) {
            memcpy(out->wifi_ssid,r.wifi_ssid,sizeof(out->wifi_ssid));
            memcpy(out->wifi_password,r.wifi_password,sizeof(out->wifi_password));
        }
    }
    return SETTINGS_STORE_OK;
}

typedef struct {
    uint32_t offset;
    size_t program_length;
    uint8_t page[3u * FLASH_PAGE_SIZE];
} flash_write_context_t;

static void flash_write_callback(void *arg) {
    flash_write_context_t *ctx = (flash_write_context_t *)arg;
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->page, ctx->program_length);
}

settings_store_result_t settings_store_save(const clock_settings_t *cfg) {
    if (!settings_store_layout_safe()) return SETTINGS_STORE_IMAGE_OVERLAP;
    if (!cfg || cfg->language > 1u || cfg->volume > 30u ||
        cfg->quiet_start > 23u || cfg->quiet_end > 23u ||
        cfg->clock_mode > 1u || cfg->timer_sound > 2u || cfg->timer_voice > 1u || cfg->timer_random > 1u || cfg->timer_warning > 1u || !cfg->timer_track ||
        cfg->audio_manual_jbl > 1u ||
        cfg->audio_hourly_jbl > 1u || cfg->audio_timer_jbl > 1u ||
        cfg->audio_alarm_jbl > 1u || cfg->bt_volume > 30u ||
        cfg->bt_known_count > CLOCK_BT_KNOWN_MAX || !display_light_valid(cfg->display_light) ||
        !phone_settings_valid(&cfg->phone))
        return SETTINGS_STORE_UNSUPPORTED;
    if (!memchr(cfg->wifi_ssid,0,sizeof(cfg->wifi_ssid)) ||
        !memchr(cfg->wifi_password,0,sizeof(cfg->wifi_password)))
        return SETTINGS_STORE_UNSUPPORTED;
    for (unsigned i = 0; i < cfg->bt_known_count; ++i) {
        if (!memchr(cfg->bt_known[i].name, 0, CLOCK_BT_NAME_SIZE))
            return SETTINGS_STORE_UNSUPPORTED;
    }
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        if (!alarm_entry_valid(&cfg->alarms[i])) return SETTINGS_STORE_UNSUPPORTED;
    }
    for (unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i)
        if (!birthday_valid(&cfg->birthdays[i])) return SETTINGS_STORE_UNSUPPORTED;
    unsigned active_slot;
    settings_record_current_t current;
    bool valid = newest_valid(&active_slot, &current);
    // A legacy record is never a destination: first new save targets slot0.
    unsigned dest = valid && active_slot < 2u ? (1u - active_slot) : 0u;
    // Never erase unknown content; applies even after a first good save.
    // In particular, never touch BTstack -3/-2 or the legacy -4 sector.
    if (!sector_blank(dest)) {
        if (!sector_owned(dest)) return SETTINGS_STORE_OCCUPIED;
    }

    flash_write_context_t context = {
        .offset = dest ? SLOT1_OFFSET : SLOT0_OFFSET
    };
    memset(context.page, 0xff, sizeof(context.page));
    const phone_settings_t blank_phone = {0};
    bool have_phone = memcmp(&cfg->phone, &blank_phone, sizeof(blank_phone)) != 0;
    bool have_birth_year = false, have_extra = false;
    for (unsigned i = 0; i < STORED_BIRTHDAY_COUNT; ++i)
        have_birth_year |= cfg->birthdays[i].birth_year != 0;
    for (unsigned i = STORED_BIRTHDAY_COUNT; i < CLOCK_BIRTHDAY_COUNT; ++i) {
        const birthday_t *b = &cfg->birthdays[i];
        have_extra |= b->enabled || b->month != 1u || b->day != 1u ||
            b->hour != 9u || b->minute || b->last_year || b->birth_year;
    }
    bool have_warning=cfg->timer_warning!=0;
    bool have_random = cfg->timer_random != 0;
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) have_random |= cfg->alarms[i].sound==ALARM_SOUND_RANDOM;
    bool have_timer = cfg->timer_sound != 0u || cfg->timer_track != 1u || cfg->timer_voice != 1u;
    settings_record_current_t r = {
        .magic = RECORD_MAGIC,
        .version = have_warning ? RECORD_VERSION : have_random ? RECORD_V16 : have_timer ? RECORD_V15 : have_extra ? RECORD_V14 : have_birth_year ? RECORD_V13 :
            have_phone ? RECORD_V12 : RECORD_V11,
        .sequence = valid ? current.sequence + 1u : 1u,
        .language = cfg->language,
        .volume = cfg->volume,
        .exact_hour = cfg->exact_hour ? 1u : 0u,
        .quiet_enabled = cfg->quiet_enabled ? 1u : 0u,
        .quiet_start = cfg->quiet_start,
        .quiet_end = cfg->quiet_end,
        .half_hour_enabled = cfg->half_hour_enabled ? 1u : 0u,
        .clock_mode = cfg->clock_mode,
        .audio_manual_jbl = cfg->audio_manual_jbl,
        .audio_hourly_jbl = cfg->audio_hourly_jbl,
        .audio_timer_jbl = cfg->audio_timer_jbl,
        .audio_alarm_jbl = cfg->audio_alarm_jbl,
        .reserved = {0xffu, 0xffu, 0xffu},
        .reserved_v5 = {0xffu, 0xffu},
        .bt_volume = cfg->bt_volume,
        .reserved_v6 = {0xffu, 0xffu, 0xffu},
        .bt_known_count = cfg->bt_known_count,
        .reserved_v7 = {0xffu, 0xffu, 0xffu},
        .reserved_v8 = {0xffu, 0xffu},
        .reserved_v9 = {0xffu, 0xffu, 0xffu},
        .display_light = cfg->display_light,
        .reserved_v11 = 0xffu
    };
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &cfg->alarms[i];
        r.alarms[i][0] = a->enabled ? 1u : 0u;
        r.alarms[i][1] = a->hour;
        r.alarms[i][2] = a->minute;
        r.alarms[i][3] = (uint8_t)a->sound;
        r.alarms[i][4] = a->track;
        r.alarm_weekdays[i]=a->weekdays;
    }
    for (unsigned i=0;i<STORED_BIRTHDAY_COUNT;++i) {
        const birthday_t *b=&cfg->birthdays[i];
        uint8_t *dst=r.birthdays[i];
        dst[0]=b->enabled?1u:0u; dst[1]=b->month; dst[2]=b->day;
        dst[3]=b->hour; dst[4]=b->minute;
        dst[5]=(uint8_t)b->last_year; dst[6]=(uint8_t)(b->last_year>>8); dst[7]=0xffu;
        r.birth_years[i] = b->birth_year;
    }
    memcpy(r.bt_target_addr, cfg->bt_target_addr, sizeof(r.bt_target_addr));
    memcpy(r.bt_known, cfg->bt_known, sizeof(r.bt_known));
    memcpy(r.wifi_ssid,cfg->wifi_ssid,sizeof(r.wifi_ssid));
    memcpy(r.wifi_password,cfg->wifi_password,sizeof(r.wifi_password));
    size_t crc_offset = have_birth_year ? offsetof(settings_record_current_t, crc32) :
        have_phone ? RECORD_V12_CRC_OFFSET : RECORD_V11_CRC_OFFSET;
    r.phone = cfg->phone;
    size_t length;
    if (have_extra || have_timer || have_random || have_warning) {
        settings_record_v14_t extended;
        memset(&extended, 0xff, sizeof(extended));
        r.crc32 = UINT32_MAX;
        extended.prefix = r;
        for (unsigned i = STORED_BIRTHDAY_COUNT; i < CLOCK_BIRTHDAY_COUNT; ++i) {
            unsigned j = i - STORED_BIRTHDAY_COUNT;
            const birthday_t *b = &cfg->birthdays[i];
            uint8_t *dst = extended.birthdays[j];
            dst[0] = b->enabled ? 1u : 0u; dst[1] = b->month; dst[2] = b->day;
            dst[3] = b->hour; dst[4] = b->minute;
            dst[5] = (uint8_t)b->last_year; dst[6] = (uint8_t)(b->last_year >> 8);
            extended.birth_years[j] = b->birth_year;
        }
        if (have_timer || have_random || have_warning) {
            settings_record_v15_t latest;
            memset(&latest, 0xff, sizeof(latest));
            latest.prefix = extended;
            latest.timer_voice = cfg->timer_voice;
            latest.timer_sound = cfg->timer_sound;
            latest.timer_track = cfg->timer_track;
            if (have_random || have_warning) {
                settings_record_v16_t newest; memset(&newest,0xff,sizeof(newest));
                newest.prefix=latest; newest.timer_random=cfg->timer_random;
                if(have_warning) {
                    settings_record_v17_t warning;memset(&warning,0xff,sizeof(warning));
                    warning.prefix=newest;warning.timer_warning=cfg->timer_warning;
                    warning.crc32=record_crc_bytes(&warning,offsetof(settings_record_v17_t,crc32));
                    length=sizeof(warning);memcpy(context.page,&warning,length);
                } else {
                    newest.crc32=record_crc_bytes(&newest,offsetof(settings_record_v16_t,crc32));
                    length=sizeof(newest);memcpy(context.page,&newest,length);
                }
            } else {
                latest.crc32 = record_crc_bytes(&latest, offsetof(settings_record_v15_t, crc32));
                length = sizeof(latest);
                memcpy(context.page, &latest, length);
            }
        } else {
            extended.crc32 = record_crc_bytes(&extended, offsetof(settings_record_v14_t, crc32));
            length = sizeof(extended);
            memcpy(context.page, &extended, length);
        }
    } else {
        uint32_t crc = record_crc_bytes(&r, crc_offset);
        memcpy((uint8_t *)&r + crc_offset, &crc, sizeof(crc));
        length = record_size(&r);
        memcpy(context.page, &r, length);
    }

    context.program_length = (length + FLASH_PAGE_SIZE - 1u) / FLASH_PAGE_SIZE * FLASH_PAGE_SIZE;
    int rc = flash_safe_execute(flash_write_callback, &context, 5000u);
    if (rc != PICO_OK) return SETTINGS_STORE_FLASH_ERROR;

    settings_record_current_t verify;
    memcpy(&verify, slot_bytes(dest), sizeof(verify));
    if (!sector_owned(dest) || verify.sequence != r.sequence ||
        memcmp(slot_bytes(dest), context.page, length) != 0) return SETTINGS_STORE_VERIFY_ERROR;
    return SETTINGS_STORE_OK;
}
