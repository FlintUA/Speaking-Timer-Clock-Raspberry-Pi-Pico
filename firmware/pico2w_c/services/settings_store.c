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
// explicit save/autosave after a user settings change, not at startup.
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
#define RECORD_VERSION 6u
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

_Static_assert(sizeof(settings_record_t) == 52, "Unexpected v3 size");
_Static_assert(offsetof(settings_record_t, crc32) == 48, "Unexpected v3 CRC offset");
_Static_assert(sizeof(settings_record_v4_t) == 56, "Unexpected v4 size");
_Static_assert(offsetof(settings_record_v4_t, crc32) == 52, "Unexpected v4 CRC offset");
_Static_assert(sizeof(settings_record_v5_t) == 64, "Unexpected v5 size");
_Static_assert(offsetof(settings_record_v5_t, crc32) == 60, "Unexpected v5 CRC offset");
_Static_assert(sizeof(settings_record_v6_t) == 68, "Unexpected v6 size");
_Static_assert(offsetof(settings_record_v6_t, crc32) == 64, "Unexpected v6 CRC offset");

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

static bool record_valid(const settings_record_v6_t *r) {
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
        r->version != RECORD_V5 && r->version != RECORD_VERSION) return false;
    if (r->quiet_enabled > 1u || r->quiet_start > 23u ||
        r->quiet_end > 23u || r->half_hour_enabled > 1u ||
        r->clock_mode > 1u) return false;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const uint8_t *a = r->alarms[i];
        if (a[0] > 1u || a[1] > 23u || a[2] > 59u ||
            a[3] > 1u || a[4] < 1u || a[4] > 45u) return false;
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
    return r->crc32 == record_crc_bytes(r, offsetof(settings_record_v6_t, crc32));
}

static size_t record_size(const settings_record_v6_t *r) {
    if (r->version == RECORD_V1) return sizeof(settings_record_v1_t);
    if (r->version == RECORD_V2) return sizeof(settings_record_v2_t);
    if (r->version == RECORD_V3) return sizeof(settings_record_t);
    if (r->version == RECORD_V4) return sizeof(settings_record_v4_t);
    if (r->version == RECORD_V5) return sizeof(settings_record_v5_t);
    return sizeof(settings_record_v6_t);
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
    settings_record_v6_t r;
    const uint8_t *bytes = slot_bytes(slot);
    memcpy(&r, bytes, sizeof(r));
    if (!record_valid(&r)) return false;
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

static bool newest_valid(unsigned *slot, settings_record_v6_t *result) {
    // Include the old slot0 as a read-only fallback so the existing clock
    // configuration survives its first firmware upgrade with the new map.
    bool have = false;
    for (unsigned candidate = 0u; candidate < 3u; ++candidate) {
        if (!sector_owned(candidate)) continue;
        settings_record_v6_t record;
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
        settings_record_v6_t r;
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
    settings_record_v6_t r;
    if (!newest_valid(&slot, &r)) return SETTINGS_STORE_EMPTY;
    if (out) {
        // Older versions have no audio preferences. Zero all fields before
        // decoding so v1-v3 always boot with four LOCAL defaults.
        memset(out, 0, sizeof(*out));
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
        if (r.version == RECORD_V3 || r.version == RECORD_V4 ||
            r.version == RECORD_V5 || r.version == RECORD_VERSION) {
            for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
                const uint8_t *src = r.alarms[i];
                out->alarms[i].enabled = src[0] != 0u;
                out->alarms[i].hour = src[1];
                out->alarms[i].minute = src[2];
                out->alarms[i].sound = (alarm_sound_t)src[3];
                out->alarms[i].track = src[4];
            }
        }
        if (r.version == RECORD_V4 || r.version == RECORD_V5 ||
            r.version == RECORD_VERSION) {
            out->audio_manual_jbl = r.audio_manual_jbl;
            out->audio_hourly_jbl = r.audio_hourly_jbl;
            out->audio_timer_jbl = r.audio_timer_jbl;
            out->audio_alarm_jbl = r.audio_alarm_jbl;
        }
        if (r.version == RECORD_V5 || r.version == RECORD_VERSION)
            memcpy(out->bt_target_addr, r.bt_target_addr, sizeof(out->bt_target_addr));
        out->bt_volume = r.version == RECORD_VERSION ? r.bt_volume : 16u;
    }
    return SETTINGS_STORE_OK;
}

typedef struct {
    uint32_t offset;
    uint8_t page[FLASH_PAGE_SIZE];
} flash_write_context_t;

static void flash_write_callback(void *arg) {
    flash_write_context_t *ctx = (flash_write_context_t *)arg;
    flash_range_erase(ctx->offset, FLASH_SECTOR_SIZE);
    flash_range_program(ctx->offset, ctx->page, FLASH_PAGE_SIZE);
}

settings_store_result_t settings_store_save(const clock_settings_t *cfg) {
    if (!settings_store_layout_safe()) return SETTINGS_STORE_IMAGE_OVERLAP;
    if (!cfg || cfg->language > 1u || cfg->volume > 30u ||
        cfg->quiet_start > 23u || cfg->quiet_end > 23u ||
        cfg->clock_mode > 1u || cfg->audio_manual_jbl > 1u ||
        cfg->audio_hourly_jbl > 1u || cfg->audio_timer_jbl > 1u ||
        cfg->audio_alarm_jbl > 1u || cfg->bt_volume > 30u)
        return SETTINGS_STORE_UNSUPPORTED;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        if (!alarm_entry_valid(&cfg->alarms[i])) return SETTINGS_STORE_UNSUPPORTED;
    }
    unsigned active_slot;
    settings_record_v6_t current;
    bool valid = newest_valid(&active_slot, &current);
    // A legacy record is never a destination: first new save targets slot0.
    unsigned dest = valid && active_slot < 2u ? (1u - active_slot) : 0u;
    // Never erase unknown content; applies even after a first good save.
    // In particular, never touch BTstack -3/-2 or the legacy -4 sector.
    if (!sector_blank(dest)) {
        settings_record_v6_t existing;
        memcpy(&existing, slot_bytes(dest), sizeof(existing));
        if (!sector_owned(dest)) return SETTINGS_STORE_OCCUPIED;
    }

    flash_write_context_t context = {
        .offset = dest ? SLOT1_OFFSET : SLOT0_OFFSET
    };
    memset(context.page, 0xff, sizeof(context.page));
    settings_record_v6_t r = {
        .magic = RECORD_MAGIC,
        .version = RECORD_VERSION,
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
        .reserved_v6 = {0xffu, 0xffu, 0xffu}
    };
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &cfg->alarms[i];
        r.alarms[i][0] = a->enabled ? 1u : 0u;
        r.alarms[i][1] = a->hour;
        r.alarms[i][2] = a->minute;
        r.alarms[i][3] = (uint8_t)a->sound;
        r.alarms[i][4] = a->track;
    }
    memcpy(r.bt_target_addr, cfg->bt_target_addr, sizeof(r.bt_target_addr));
    r.crc32 = record_crc_bytes(&r, offsetof(settings_record_v6_t, crc32));
    memcpy(context.page, &r, sizeof(r));

    int rc = flash_safe_execute(flash_write_callback, &context, 5000u);
    if (rc != PICO_OK) return SETTINGS_STORE_FLASH_ERROR;

    settings_record_v6_t verify;
    memcpy(&verify, slot_bytes(dest), sizeof(verify));
    if (!sector_owned(dest) || verify.sequence != r.sequence ||
        memcmp(&verify, &r, sizeof(r)) != 0) return SETTINGS_STORE_VERIFY_ERROR;
    return SETTINGS_STORE_OK;
}
