#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static inline int clock_bt_hex(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='A' && c<='F') return c-'A'+10;
    if (c>='a' && c<='f') return c-'a'+10;
    return -1;
}

// An empty target represents explicit disconnect, including while scanning.
// A late event from a previous target cannot revive that connection.
static inline bool clock_bt_target_accepts(const char *target, const uint8_t peer[6], bool scanning) {
    if (!target || strlen(target)!=17u || scanning) return false;
    for (unsigned i=0;i<6;++i) {
        unsigned pos=i*3;
        int hi=clock_bt_hex(target[pos]), lo=clock_bt_hex(target[pos+1]);
        if (hi<0 || lo<0 || (i<5 && target[pos+2]!=':')) return false;
        if (peer && peer[i]!=(uint8_t)((hi<<4)|lo)) return false;
    }
    return true;
}

static inline bool clock_bt_retry_allowed(const char *target, bool scanning, bool switching, bool has_link) {
    return clock_bt_target_accepts(target,0,scanning) && !switching && !has_link;
}
