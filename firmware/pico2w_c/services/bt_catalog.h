#pragma once
#include <stddef.h>
#include "settings_store.h"
#include "clock_a2dp_control.h"

#define BT_CATALOG_CAPACITY (CLOCK_BT_KNOWN_MAX + CLOCK_BT_MAX_DEVICES)
bool bt_address_parse(const char *text, uint8_t out[6]);
bool bt_name_usable(const char *name);
const char *bt_preferred_name(const char *saved, const char *discovered,
                              const char *address);
// Known devices first, then newly discovered addresses; each MAC occurs once.
size_t bt_catalog_build(const clock_bt_known_t *known, size_t known_count,
                        const clock_bt_device_t *scan, size_t scan_count,
                        clock_bt_device_t *out, size_t capacity);
