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

// Leave the final flash sector untouched. UF2 installers may program
// the final 256-byte page with EF bytes for RP2350-E10 bootrom workaround.
// Do not overwrite unfamiliar data: an old MicroPython FS may still be here.
// This layout is provisional until its two sectors are inspected on hardware.
#define SLOT0_OFFSET (PICO_FLASH_SIZE_BYTES - 4u * FLASH_SECTOR_SIZE)
#define SLOT1_OFFSET (PICO_FLASH_SIZE_BYTES - 3u * FLASH_SECTOR_SIZE)
#define RECORD_MAGIC 0x53434c4bu /* SCLK */
#define RECORD_VERSION 2u
#define RECORD_V1 1u
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
} settings_record_t;

_Static_assert(sizeof(settings_record_v1_t) == 20, "Unexpected v1 size");
_Static_assert(sizeof(settings_record_t) == 24, "Unexpected v2 size");
_Static_assert(offsetof(settings_record_t, crc32) == 20, "Unexpected v2 CRC offset");

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

static const uint8_t *slot_bytes(unsigned slot) {
    return (const uint8_t *)(uintptr_t)(XIP_BASE + (slot ? SLOT1_OFFSET : SLOT0_OFFSET));
}

static bool record_valid(const settings_record_t *r) {
    if (r->magic != RECORD_MAGIC || r->language > 1u ||
        r->volume > 30u || r->exact_hour > 1u) return false;
    if (r->version == RECORD_V1) {
        settings_record_v1_t old;
        memcpy(&old, r, sizeof(old));
        return old.crc32 ==
            record_crc_bytes(&old, offsetof(settings_record_v1_t, crc32));
    }
    if (r->version != RECORD_VERSION) return false;
    return r->quiet_enabled <= 1u && r->quiet_start <= 23u &&
           r->quiet_end <= 23u && r->half_hour_enabled <= 1u &&
           (r->reserved == 0xffu || r->reserved <= 1u) &&
           r->crc32 == record_crc_bytes(r, offsetof(settings_record_t, crc32));
}

static size_t record_size(const settings_record_t *r) {
    return r->version == RECORD_V1 ? sizeof(settings_record_v1_t) :
                                     sizeof(settings_record_t);
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
    settings_record_t r;
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

static bool newest_valid(unsigned *slot, settings_record_t *result) {
    settings_record_t a, b;
    memcpy(&a, slot_bytes(0), sizeof(a));
    memcpy(&b, slot_bytes(1), sizeof(b));
    bool va = sector_owned(0), vb = sector_owned(1);
    if (!va && !vb) return false;
    bool choose_b = vb && (!va || (int32_t)(b.sequence - a.sequence) > 0);
    *slot = choose_b ? 1u : 0u;
    *result = choose_b ? b : a;
    return true;
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
    for (unsigned i = 0; i < 2u; ++i) {
        settings_record_t r;
        memcpy(&r, slot_bytes(i), sizeof(r));
        printf("[FLASH] slot%u=%s sequence=%lu version=%lu\n", i,
               sector_owned(i) ? "VALID" : (sector_blank(i) ? "BLANK" : "OCCUPIED"),
               sector_owned(i) ? (unsigned long)r.sequence : 0ul,
               sector_owned(i) ? (unsigned long)r.version : 0ul);
    }
}


void settings_store_inspect(void) {
    // Read-only sampling from the two reserved sectors. We do not try to
    // interpret or mount prior MicroPython filesystem contents.
    puts("[FLASH] Inspect is READ ONLY; hexadecimal samples are NOT proof of filesystem type.");
    for (unsigned slot = 0u; slot < 2u; ++slot) {
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
        printf("[FLASH] slot%u offset=0x%08lx status=%s ff=%lu zero=%lu other=%lu\n",
               slot, (unsigned long)(slot ? SLOT1_OFFSET : SLOT0_OFFSET),
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
    settings_record_t r;
    if (!newest_valid(&slot, &r)) return SETTINGS_STORE_EMPTY;
    if (out) {
        out->language = r.language;
        out->volume = r.volume;
        out->exact_hour = r.exact_hour != 0u;
        out->quiet_enabled = r.version == RECORD_V1 ? true : r.quiet_enabled != 0u;
        out->quiet_start = r.version == RECORD_V1 ? 22u : r.quiet_start;
        out->quiet_end = r.version == RECORD_V1 ? 7u : r.quiet_end;
        out->half_hour_enabled = r.version == RECORD_V1 ? false :
                                 r.half_hour_enabled != 0u;
        // v2 records before clock-mode support stored FF in reserved.
        out->clock_mode = r.version == RECORD_V1 || r.reserved == 0xffu
                              ? 0u : r.reserved;
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
        cfg->clock_mode > 1u)
        return SETTINGS_STORE_UNSUPPORTED;
    unsigned active_slot;
    settings_record_t current;
    bool valid = newest_valid(&active_slot, &current);
    unsigned dest = valid ? (1u - active_slot) : 0u;
    // Never erase unknown content; applies even after a first good save.
    if (!sector_blank(dest)) {
        settings_record_t existing;
        memcpy(&existing, slot_bytes(dest), sizeof(existing));
        if (!sector_owned(dest)) return SETTINGS_STORE_OCCUPIED;
    }

    flash_write_context_t context = {
        .offset = dest ? SLOT1_OFFSET : SLOT0_OFFSET
    };
    memset(context.page, 0xff, sizeof(context.page));
    settings_record_t r = {
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
        .reserved = cfg->clock_mode
    };
    r.crc32 = record_crc_bytes(&r, offsetof(settings_record_t, crc32));
    memcpy(context.page, &r, sizeof(r));

    int rc = flash_safe_execute(flash_write_callback, &context, 5000u);
    if (rc != PICO_OK) return SETTINGS_STORE_FLASH_ERROR;

    settings_record_t verify;
    memcpy(&verify, slot_bytes(dest), sizeof(verify));
    if (!sector_owned(dest) || verify.sequence != r.sequence ||
        memcmp(&verify, &r, sizeof(r)) != 0) return SETTINGS_STORE_VERIFY_ERROR;
    return SETTINGS_STORE_OK;
}
