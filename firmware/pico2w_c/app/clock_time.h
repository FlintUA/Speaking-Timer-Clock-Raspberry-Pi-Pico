#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "clock_datetime.h"
bool clock_utc_to_datetime(uint64_t epoch, ds1302_datetime_t *out);
bool clock_datetime_to_utc(const ds1302_datetime_t *value, uint64_t *epoch);
int clock_berlin_offset(uint64_t epoch);
bool clock_berlin_datetime(uint64_t epoch, ds1302_datetime_t *out);
// Civil RTC time: reject the missing spring hour; repeated autumn times are valid.
bool clock_berlin_local_valid(const ds1302_datetime_t *local);
typedef struct {
    bool allow_boundary;
    uint64_t suppress_before;
} clock_adjustment_t;
clock_adjustment_t clock_adjustment_plan(bool have_previous, uint64_t previous,
                                         uint64_t corrected, bool dst_change);
// Validate NTP header, request correlation, era and plausible calendar range.
bool clock_ntp_decode(const uint8_t *packet, size_t size,
                       const uint8_t nonce[8], uint64_t *epoch);
