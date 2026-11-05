#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pico/stdlib.h"

typedef struct {
    uint16_t year;
    uint8_t month, day, weekday, hour, minute, second;
} ds1302_datetime_t;

// Constructor order in production MicroPython: DS1302(Pin(2), Pin(5), Pin(4))
// => CLK=GP2, DIO=GP5, CE=GP4.
void ds1302_init(uint clk_gpio, uint dio_gpio, uint ce_gpio);
bool ds1302_read_datetime(ds1302_datetime_t *out);
// Read-only bring-up intentionally omits setting the RTC.
