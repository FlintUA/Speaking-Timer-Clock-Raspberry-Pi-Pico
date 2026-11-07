#include "clock_time.h"
#include <string.h>

static bool leap(unsigned y) { return y%4 == 0 && (y%100 != 0 || y%400 == 0); }
static unsigned month_days(unsigned y, unsigned m) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return m >= 1 && m <= 12 ? days[m-1] + (m == 2 && leap(y)) : 0;
}
bool clock_datetime_to_utc(const ds1302_datetime_t *d, uint64_t *epoch) {
    if (!d || !epoch || d->year < 2000 || d->year > 2099 ||
        d->month < 1 || d->month > 12 || d->day < 1 ||
        d->day > month_days(d->year,d->month) || d->hour > 23 ||
        d->minute > 59 || d->second > 59) return false;
    uint64_t days = 10957; // 1970-01-01 to 2000-01-01.
    for (unsigned y = 2000; y < d->year; ++y) days += leap(y) ? 366 : 365;
    for (unsigned m = 1; m < d->month; ++m) days += month_days(d->year,m);
    *epoch = ((days+d->day-1)*24+d->hour)*3600+d->minute*60+d->second;
    return true;
}
bool clock_utc_to_datetime(uint64_t epoch, ds1302_datetime_t *out) {
    if (!out || epoch < 946684800ULL || epoch >= 4102444800ULL) return false;
    uint64_t total_days = epoch/86400;
    unsigned days = (unsigned)(total_days-10957), y=2000, m=1;
    while (days >= (leap(y)?366u:365u)) { days -= leap(y)?366u:365u; ++y; }
    while (days >= month_days(y,m)) { days -= month_days(y,m); ++m; }
    *out = (ds1302_datetime_t){.year=(uint16_t)y,.month=(uint8_t)m,
        .day=(uint8_t)(days+1),.weekday=(uint8_t)((total_days+3)%7+1),
        .hour=(uint8_t)(epoch/3600%24),.minute=(uint8_t)(epoch/60%60),
        .second=(uint8_t)(epoch%60)};
    return true;
}
static uint64_t transition(unsigned year, unsigned month) {
    ds1302_datetime_t d = {.year=(uint16_t)year,.month=(uint8_t)month,
                           .day=31,.hour=1};
    uint64_t epoch=0; clock_datetime_to_utc(&d,&epoch);
    // Sunday is zero for Unix day + 4.
    return epoch - ((epoch/86400+4)%7)*86400;
}
int clock_berlin_offset(uint64_t epoch) {
    ds1302_datetime_t d;
    if (!clock_utc_to_datetime(epoch,&d)) return 3600;
    return epoch >= transition(d.year,3) && epoch < transition(d.year,10)
           ? 7200 : 3600;
}
bool clock_berlin_datetime(uint64_t epoch, ds1302_datetime_t *out) {
    return clock_utc_to_datetime(epoch+(unsigned)clock_berlin_offset(epoch),out);
}
clock_adjustment_t clock_adjustment_plan(bool have_previous, uint64_t previous,
                                         uint64_t corrected, bool dst_change) {
    if (!have_previous) return (clock_adjustment_t){false,0};
    if (corrected<previous)
        return (clock_adjustment_t){true,(previous/60+1)*60}; // Skip the entire last observed minute.
    uint64_t difference=corrected-previous;
    // Preserve the real 03:00 boundary at the spring transition.
    return (clock_adjustment_t){dst_change && difference>=3598 && difference<=3602,0};
}
bool clock_ntp_decode(const uint8_t *p, size_t size, const uint8_t nonce[8],
                       uint64_t *epoch) {
    if (!p || !nonce || !epoch || size < 48 || (p[0]>>6) == 3 ||
        ((p[0]>>3)&7) < 3 || ((p[0]>>3)&7) > 4 || (p[0]&7) != 4 ||
        p[1] == 0 || p[1] > 15 || memcmp(p+24,nonce,8)) return false;
    uint32_t seconds = ((uint32_t)p[40]<<24)|((uint32_t)p[41]<<16)|
                       ((uint32_t)p[42]<<8)|p[43];
    if (!seconds) return false;
    uint64_t unix_seconds = seconds >= 2208988800u ?
        (uint64_t)seconds-2208988800u : (uint64_t)seconds+2085978496ULL;
    ds1302_datetime_t d;
    if (unix_seconds < 1577836800ULL || !clock_utc_to_datetime(unix_seconds,&d))
        return false;
    *epoch=unix_seconds;
    return true;
}
