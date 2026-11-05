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

// Raw order: seconds, minutes, hours, day, month, weekday, year.
typedef struct {
    uint32_t reads_ok;
    uint32_t reads_failed;
    uint32_t consecutive_failures;
    uint32_t max_consecutive_failures;
    uint8_t last_raw[7];
    uint8_t last_failed_raw[7];
    uint32_t last_failure_flags;
    bool have_last_failure;
} ds1302_diagnostics_t;

// Validation failure bits; multiple bits can be present.
#define DS1302_ERR_CH           (1u << 0)
#define DS1302_ERR_12H_MODE     (1u << 1)
#define DS1302_ERR_SECONDS      (1u << 2)
#define DS1302_ERR_MINUTES      (1u << 3)
#define DS1302_ERR_HOURS        (1u << 4)
#define DS1302_ERR_DAY          (1u << 5)
#define DS1302_ERR_MONTH        (1u << 6)
#define DS1302_ERR_WEEKDAY      (1u << 7)
#define DS1302_ERR_YEAR_BCD     (1u << 8)
#define DS1302_ERR_INVALID_BCD  (1u << 9)

void ds1302_get_diagnostics(ds1302_diagnostics_t *out);
void ds1302_reset_diagnostics(void);
