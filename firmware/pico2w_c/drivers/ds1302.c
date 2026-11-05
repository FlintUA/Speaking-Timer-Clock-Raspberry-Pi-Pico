#include "ds1302.h"
#include "pico/stdlib.h"
#include <string.h>

static uint s_clk, s_dio, s_ce;
static ds1302_diagnostics_t stats;

void ds1302_init(uint clk_gpio, uint dio_gpio, uint ce_gpio) {
    memset(&stats, 0, sizeof(stats));
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

static bool invalid_bcd(uint8_t value) {
    return ((value & 0x0fu) > 9u) || (((value >> 4) & 0x0fu) > 9u);
}

void ds1302_get_diagnostics(ds1302_diagnostics_t *out) {
    if (out) *out = stats;
}

void ds1302_reset_diagnostics(void) {
    memset(&stats, 0, sizeof(stats));
}

bool ds1302_read_datetime(ds1302_datetime_t *out) {
    if (!out) return false;
    // Preserve original read timings/register order. Capture EXACT raw bytes
    // from the same read used by the clock; no extra transactions or retries.
    uint8_t raw[7] = {
        read_register(0x80), read_register(0x82), read_register(0x84),
        read_register(0x86), read_register(0x88), read_register(0x8a),
        read_register(0x8c)
    };
    memcpy(stats.last_raw, raw, sizeof(raw));
    uint8_t seconds_raw = raw[0];
    uint8_t minutes_raw = raw[1];
    uint8_t hours_raw = raw[2];
    uint8_t day_raw = raw[3];
    uint8_t month_raw = raw[4];
    uint8_t weekday_raw = raw[5];
    uint8_t year_raw = raw[6];

    // Identical masking and decimal conversion as the original C driver.
    out->second = bcd_to_dec(seconds_raw & 0x7fu);
    out->minute = bcd_to_dec(minutes_raw & 0x7fu);
    out->hour = bcd_to_dec(hours_raw & 0x3fu);
    out->day = bcd_to_dec(day_raw & 0x3fu);
    out->month = bcd_to_dec(month_raw & 0x1fu);
    out->weekday = bcd_to_dec(weekday_raw & 0x07u);
    out->year = 2000u + bcd_to_dec(year_raw);

    uint32_t flags = 0;
    if (seconds_raw & 0x80u) flags |= DS1302_ERR_CH;
    if (hours_raw & 0x80u) flags |= DS1302_ERR_12H_MODE;
    if (out->second >= 60u) flags |= DS1302_ERR_SECONDS;
    if (out->minute >= 60u) flags |= DS1302_ERR_MINUTES;
    if (out->hour >= 24u) flags |= DS1302_ERR_HOURS;
    if (out->day < 1u || out->day > 31u) flags |= DS1302_ERR_DAY;
    if (out->month < 1u || out->month > 12u) flags |= DS1302_ERR_MONTH;
    if (out->weekday < 1u || out->weekday > 7u) flags |= DS1302_ERR_WEEKDAY;
    // Invalid BCD diagnostics are new: some invalid digit combinations
    // previously passed the numeric range checks. Still reject them.
    if (invalid_bcd(seconds_raw & 0x7fu) ||
        invalid_bcd(minutes_raw & 0x7fu) ||
        invalid_bcd(hours_raw & 0x3fu) ||
        invalid_bcd(day_raw & 0x3fu) ||
        invalid_bcd(month_raw & 0x1fu) ||
        invalid_bcd(weekday_raw & 0x07u) ||
        invalid_bcd(year_raw)) {
        flags |= DS1302_ERR_INVALID_BCD;
    }
    if (invalid_bcd(year_raw)) flags |= DS1302_ERR_YEAR_BCD;

    if (flags) {
        ++stats.reads_failed;
        ++stats.consecutive_failures;
        if (stats.consecutive_failures > stats.max_consecutive_failures) {
            stats.max_consecutive_failures = stats.consecutive_failures;
        }
        memcpy(stats.last_failed_raw, raw, sizeof(raw));
        stats.last_failure_flags = flags;
        stats.have_last_failure = true;
        return false;
    }
    ++stats.reads_ok;
    stats.consecutive_failures = 0;
    return true;
}


static bool leap_year(uint16_t y) {
    return y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u);
}
static uint8_t month_days(uint16_t y, uint8_t m) {
    static const uint8_t days[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1u || m > 12u) return 0;
    return (uint8_t)(days[m-1u] + (m == 2u && leap_year(y)));
}
bool ds1302_datetime_valid(const ds1302_datetime_t *d) {
    return d && d->year >= 2000u && d->year <= 2099u &&
           d->month >= 1u && d->month <= 12u &&
           d->day >= 1u && d->day <= month_days(d->year,d->month) &&
           d->weekday >= 1u && d->weekday <= 7u &&
           d->hour <= 23u && d->minute <= 59u && d->second <= 59u;
}
static uint8_t dec_to_bcd(uint8_t v) {
    return (uint8_t)(((v / 10u) << 4) | (v % 10u));
}
static void write_register(uint8_t address, uint8_t value) {
    gpio_put(s_clk, 0);
    gpio_put(s_ce, 1);
    sleep_us(4);
    write_byte(address & 0xfeu);
    write_byte(value);
    gpio_put(s_ce, 0);
    gpio_set_dir(s_dio, GPIO_IN);
    sleep_us(4);
}
bool ds1302_write_datetime(const ds1302_datetime_t *d) {
    if (!ds1302_datetime_valid(d)) return false;

    // DS1302 supports write-protect bit 7 of control register.
    // Preserve its previous value; never permanently change the policy.
    uint8_t previous_control = read_register(0x8e);
    write_register(0x8e, 0u);

    // Write burst order: seconds, minutes, hours, date, month, weekday,
    // year, control. The final byte restores WP if originally set.
    gpio_put(s_clk, 0);
    gpio_put(s_ce, 1);
    sleep_us(4);
    write_byte(0xbe);
    write_byte(dec_to_bcd(d->second)); // CH=0: running
    write_byte(dec_to_bcd(d->minute));
    write_byte(dec_to_bcd(d->hour)); // 24-hour mode
    write_byte(dec_to_bcd(d->day));
    write_byte(dec_to_bcd(d->month));
    write_byte(dec_to_bcd(d->weekday));
    write_byte(dec_to_bcd((uint8_t)(d->year - 2000u)));
    write_byte(previous_control & 0x80u);
    gpio_put(s_ce, 0);
    gpio_set_dir(s_dio, GPIO_IN);
    sleep_us(4);

    ds1302_datetime_t verify = {0};
    if (!ds1302_read_datetime(&verify)) return false;
    // A read across a seconds boundary is permitted; check immutable
    // fields and accept the seconds tick advancing at most one second.
    bool date_match = verify.year == d->year && verify.month == d->month &&
        verify.day == d->day && verify.weekday == d->weekday;
    uint32_t actual = (uint32_t)verify.hour*3600u + (uint32_t)verify.minute*60u + verify.second;
    uint32_t wanted = (uint32_t)d->hour*3600u + (uint32_t)d->minute*60u + d->second;
    return date_match && actual >= wanted && actual <= wanted + 1u;
}
