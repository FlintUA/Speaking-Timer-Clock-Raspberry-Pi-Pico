#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "clock_datetime.h"

#define CLOCK_BIRTHDAY_COUNT 5u
#define BIRTHDAY_WAIT_MS (30u * 60000u)
typedef struct {
    bool enabled;
    uint8_t month, day, hour, minute;
    uint16_t last_year; // Persisted: at most one occurrence per calendar year.
} birthday_t;

void birthday_defaults(birthday_t dates[CLOCK_BIRTHDAY_COUNT]);
bool birthday_valid(const birthday_t *date);
// Claims matching events and records the year. No catch-up for missed minutes.
// February 29 matches only in leap years. RTC weekday is irrelevant.
uint8_t birthday_claim(birthday_t dates[CLOCK_BIRTHDAY_COUNT], const ds1302_datetime_t *now);
bool birthday_wait_expired(uint64_t queued_ms, uint64_t now_ms);
