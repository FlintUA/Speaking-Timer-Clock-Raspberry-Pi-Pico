#include "bt_catalog.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool bt_address_parse(const char *text, uint8_t out[6]) {
    if (!text || !out || strlen(text) != 17u) return false;
    uint8_t parsed[6];
    for (unsigned i = 0; i < 6; ++i) {
        int hi = hex_value(text[3*i]), lo = hex_value(text[3*i+1]);
        if (hi < 0 || lo < 0 || (i < 5 && text[3*i+2] != ':')) return false;
        parsed[i] = (uint8_t)(hi * 16 + lo);
    }
    memcpy(out, parsed, sizeof(parsed));
    return true;
}
bool bt_name_usable(const char *name) {
    uint8_t address[6];
    if (!name || !name[0] || bt_address_parse(name,address) ||
        strcmp(name,"Unknown")==0) return false;
    return !(strlen(name)==13u && strncmp(name,"Unknown ",8)==0 &&
        hex_value(name[8])>=0 && hex_value(name[9])>=0 && name[10]==':' &&
        hex_value(name[11])>=0 && hex_value(name[12])>=0);
}
const char *bt_preferred_name(const char *saved, const char *discovered,
                              const char *address) {
    if (bt_name_usable(discovered)) return discovered;
    if (bt_name_usable(saved)) return saved;
    return address;
}
size_t bt_catalog_build(const clock_bt_known_t *known, size_t known_count,
                        const clock_bt_device_t *scan, size_t scan_count,
                        clock_bt_device_t *out, size_t capacity) {
    if (!out || !capacity) return 0;
    size_t count = 0;
    for (size_t i = 0; i < known_count && count < capacity; ++i) {
        clock_bt_device_t *entry = &out[count++];
        const uint8_t *a = known[i].address;
        snprintf(entry->address, sizeof(entry->address),
                 "%02X:%02X:%02X:%02X:%02X:%02X",a[0],a[1],a[2],a[3],a[4],a[5]);
        snprintf(entry->name, sizeof(entry->name), "%s", known[i].name);
        entry->rssi = 0;
    }
    for (size_t i = 0; i < scan_count; ++i) {
        uint8_t address[6];
        if (!bt_address_parse(scan[i].address, address)) continue;
        size_t j = 0;
        for (; j < count; ++j) {
            uint8_t existing[6];
            if (bt_address_parse(out[j].address, existing) &&
                memcmp(existing, address, 6) == 0) break;
        }
        if (j < count) {
            if (bt_name_usable(scan[i].name))
                snprintf(out[j].name, sizeof(out[j].name), "%s", scan[i].name);
            out[j].rssi = scan[i].rssi;
        } else if (count < capacity) out[count++] = scan[i];
    }
    return count;
}
