#include "ds1302.h"
#include "pico/stdlib.h"

static uint s_clk, s_dio, s_ce;

void ds1302_init(uint clk_gpio, uint dio_gpio, uint ce_gpio) {
    s_clk = clk_gpio;
    s_dio = dio_gpio;
    s_ce = ce_gpio;
    gpio_init(s_clk);
    gpio_set_dir(s_clk, GPIO_OUT);
    gpio_put(s_clk, 0);
    gpio_init(s_ce);
    gpio_set_dir(s_ce, GPIO_OUT);
    gpio_put(s_ce, 0);
    gpio_init(s_dio);
    gpio_set_dir(s_dio, GPIO_IN);
}

static void write_byte(uint8_t value) {
    gpio_set_dir(s_dio, GPIO_OUT);
    for (unsigned bit = 0; bit < 8; ++bit) {
        gpio_put(s_dio, (value >> bit) & 1u);
        sleep_us(1);
        gpio_put(s_clk, 1);
        sleep_us(1);
        gpio_put(s_clk, 0);
        sleep_us(1);
    }
}

static uint8_t read_register(uint8_t address) {
    gpio_put(s_ce, 1);
    sleep_us(4);
    write_byte(address | 1u);
    gpio_set_dir(s_dio, GPIO_IN);
    sleep_us(1);
    uint8_t value = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
        value |= (uint8_t)(gpio_get(s_dio) << bit);
        gpio_put(s_clk, 1);
        sleep_us(1);
        gpio_put(s_clk, 0);
        sleep_us(1);
    }
    gpio_put(s_ce, 0);
    sleep_us(4);
    return value;
}

static uint8_t bcd_to_dec(uint8_t b) {
    return (uint8_t)(((b >> 4) * 10u) + (b & 0x0fu));
}

bool ds1302_read_datetime(ds1302_datetime_t *out) {
    if (!out) return false;
    // Same register masks as library/ds1302.py. CH bit indicates halted clock.
    uint8_t seconds_raw = read_register(0x80);
    uint8_t minutes_raw = read_register(0x82);
    uint8_t hours_raw = read_register(0x84);
    uint8_t day_raw = read_register(0x86);
    uint8_t month_raw = read_register(0x88);
    uint8_t weekday_raw = read_register(0x8a);
    uint8_t year_raw = read_register(0x8c);

    out->second = bcd_to_dec(seconds_raw & 0x7f);
    out->minute = bcd_to_dec(minutes_raw & 0x7f);
    // First bring-up supports 24-hour mode (production uses 24-hour clock).
    if (hours_raw & 0x80) return false;
    out->hour = bcd_to_dec(hours_raw & 0x3f);
    out->day = bcd_to_dec(day_raw & 0x3f);
    out->month = bcd_to_dec(month_raw & 0x1f);
    out->weekday = bcd_to_dec(weekday_raw & 0x07);
    out->year = 2000u + bcd_to_dec(year_raw);
    return !(seconds_raw & 0x80) &&
           out->second < 60 && out->minute < 60 && out->hour < 24 &&
           out->day >= 1 && out->day <= 31 &&
           out->month >= 1 && out->month <= 12 &&
           out->weekday >= 1 && out->weekday <= 7 &&
           out->year >= 2000 && out->year <= 2099;
}
