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

// Two 4 KiB sectors at the END of the configured board flash.
// Do not overwrite unfamiliar data: a prior MicroPython filesystem may live here.
#define SLOT0_OFFSET (PICO_FLASH_SIZE_BYTES - 2u * FLASH_SECTOR_SIZE)
#define SLOT1_OFFSET (PICO_FLASH_SIZE_BYTES - 1u * FLASH_SECTOR_SIZE)
#define RECORD_MAGIC 0x53434c4bu /* SCLK */
#define RECORD_VERSION 1u
#define IMAGE_SAFETY_MARGIN FLASH_SECTOR_SIZE

extern uint8_t __flash_binary_end;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint8_t language;
    uint8_t volume;
    uint8_t exact_hour;
    uint8_t reserved;
    uint32_t crc32;
} settings_record_t;

_Static_assert(sizeof(settings_record_t) == 20, "Unexpected record padding");
_Static_assert(offsetof(settings_record_t, crc32) == 16, "Unexpected CRC offset");

static uint32_t record_crc(const settings_record_t *r) {
    // IEEE CRC32, reflected polynomial; cover all fields before crc32.
    uint32_t crc = 0xffffffffu;
    const uint8_t *ptr = (const uint8_t *)r;
    for (size_t i = 0; i < offsetof(settings_record_t, crc32); ++i) {
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
    return r->magic == RECORD_MAGIC &&
           r->version == RECORD_VERSION &&
           r->language <= 1u && r->volume <= 30u &&
           r->exact_hour <= 1u &&
           r->crc32 == record_crc(r);
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
    for (size_t i = sizeof(r); i < FLASH_SECTOR_SIZE; ++i) {
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
        printf("[FLASH] slot%u=%s sequence=%lu\n", i,
               sector_owned(i) ? "VALID" : (sector_blank(i) ? "BLANK" : "OCCUPIED"),
               sector_owned(i) ? (unsigned long)r.sequence : 0ul);
    }
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
    if (!cfg || cfg->language > 1u || cfg->volume > 30u) return SETTINGS_STORE_UNSUPPORTED;
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
        .reserved = 0xffu
    };
    r.crc32 = record_crc(&r);
    memcpy(context.page, &r, sizeof(r));

    int rc = flash_safe_execute(flash_write_callback, &context, 5000u);
    if (rc != PICO_OK) return SETTINGS_STORE_FLASH_ERROR;

    settings_record_t verify;
    memcpy(&verify, slot_bytes(dest), sizeof(verify));
    if (!sector_owned(dest) || verify.sequence != r.sequence ||
        memcmp(&verify, &r, sizeof(r)) != 0) return SETTINGS_STORE_VERIFY_ERROR;
    return SETTINGS_STORE_OK;
}
