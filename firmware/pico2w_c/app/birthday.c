#include "birthday.h"
#include "clock_time.h"

void birthday_defaults(birthday_t dates[CLOCK_BIRTHDAY_COUNT]) {
    for (unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i)
        dates[i]=(birthday_t){.month=1,.day=1,.hour=9};
}

bool birthday_valid(const birthday_t *date) {
    if (!date || (date->last_year && (date->last_year<2000 || date->last_year>2099))) return false;
    if (date->birth_year && (date->birth_year < 1000u || date->birth_year > 9999u)) return false;
    if (date->birth_year && date->month == 2u && date->day == 29u &&
        (date->birth_year % 4u != 0u ||
         (date->birth_year % 100u == 0u && date->birth_year % 400u != 0u))) return false;
    ds1302_datetime_t d={.year=2000,.month=date->month,.day=date->day,
        .hour=date->hour,.minute=date->minute};
    uint64_t wall;
    return clock_datetime_to_utc(&d,&wall);
}

uint8_t birthday_claim(birthday_t dates[CLOCK_BIRTHDAY_COUNT], const ds1302_datetime_t *now) {
    uint64_t wall;
    if (!dates || !clock_datetime_to_utc(now,&wall)) return 0;
    uint8_t mask=0;
    for (unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i) {
        birthday_t *b=&dates[i];
        // Don't repeat a previously handled year, including a backward RTC correction.
        if (!b->enabled || !birthday_valid(b) || b->last_year>=now->year ||
            b->month!=now->month || b->day!=now->day ||
            b->hour!=now->hour || b->minute!=now->minute) continue;
        b->last_year=now->year;
        mask|=(uint8_t)(1u<<i);
    }
    return mask;
}

bool birthday_wait_expired(uint64_t queued_ms, uint64_t now_ms) {
    return now_ms<queued_ms || now_ms-queued_ms>=BIRTHDAY_WAIT_MS;
}
