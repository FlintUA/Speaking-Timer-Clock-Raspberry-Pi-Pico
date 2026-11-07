#pragma once
#include <stdint.h>
typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} ds1302_datetime_t;
