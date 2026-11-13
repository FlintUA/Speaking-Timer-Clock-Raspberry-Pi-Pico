#include <assert.h>
#include "birthday.h"

int main(void) {
    birthday_t b[CLOCK_BIRTHDAY_COUNT];
    birthday_defaults(b);
    for(unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i)
        assert(!b[i].enabled && birthday_valid(&b[i]));
    ds1302_datetime_t now={.year=2026,.month=11,.day=8,.hour=9,.minute=30};
    assert(!birthday_claim(b,&now));
    b[0]=(birthday_t){.enabled=true,.month=11,.day=8,.hour=9,.minute=30};
    b[4]=b[0]; b[5]=b[0]; b[14]=b[0];
    assert(birthday_claim(b,&now)==0x4031); // All co-due slots retained.
    assert(b[0].last_year==2026 && b[4].last_year==2026 &&
           b[5].last_year==2026 && b[14].last_year==2026);
    assert(!birthday_claim(b,&now));
    now.second=59;
    assert(!birthday_claim(b,&now));
    now.year=2025;
    assert(!birthday_claim(b,&now)); // Backward clock correction.
    now.year=2027;
    assert(birthday_claim(b,&now)==0x4031);
    birthday_t saved=b[0];
    birthday_defaults(b);
    b[0]=saved; // Reboot with persisted last_year.
    assert(!birthday_claim(b,&now));
    b[0]=(birthday_t){.enabled=true,.month=2,.day=29,.hour=9};
    assert(birthday_valid(&b[0]));
    now=(ds1302_datetime_t){.year=2027,.month=2,.day=28,.hour=9};
    assert(!birthday_claim(b,&now));
    now.day=29;
    assert(!birthday_claim(b,&now)); // Invalid non-leap RTC date.
    now.year=2028;
    assert(birthday_claim(b,&now)==1);
    b[0].day=30;
    assert(!birthday_valid(&b[0]));
    b[0].month=4; b[0].day=31;
    assert(!birthday_valid(&b[0]));
    b[0].day=30; b[0].hour=24;
    assert(!birthday_valid(&b[0]));
    b[0].hour=9; b[0].minute=60;
    assert(!birthday_valid(&b[0]));
    assert(!birthday_valid(0) && !birthday_claim(0,&now) && !birthday_claim(b,0));
    birthday_defaults(b);
    b[0].enabled=true;
    now=(ds1302_datetime_t){.year=2026,.month=1,.day=1,.hour=9,.minute=1};
    assert(!birthday_claim(b,&now)); // No catch-up for a missed minute.
    uint64_t queued=UINT64_C(0xffffffff)-100;
    assert(!birthday_wait_expired(queued,queued+BIRTHDAY_WAIT_MS-1));
    birthday_defaults(b);
    b[0]=(birthday_t){.enabled=true,.month=12,.day=10,.hour=7,.birth_year=1985};
    b[1]=b[0]; b[1].birth_year=0;
    now=(ds1302_datetime_t){.year=2026,.month=12,.day=10,.hour=7};
    assert(birthday_valid(&b[0]) && birthday_valid(&b[1]));
    assert(birthday_claim(b,&now)==3);
    b[0].birth_year=1986;
    assert(!birthday_claim(b,&now)); // Metadata alone does not re-arm this year's event.
    now.year++;
    assert(birthday_claim(b,&now)==3); // Recurrence is independent of a known birth year.
    b[0].birth_year=999;
    assert(!birthday_valid(&b[0]));
    b[0].birth_year=10000;
    assert(!birthday_valid(&b[0]));
    b[0].month=2; b[0].day=29; b[0].birth_year=1984;
    assert(birthday_valid(&b[0]));
    b[0].birth_year=1985;
    assert(!birthday_valid(&b[0]));
    b[0].birth_year=1900;
    assert(!birthday_valid(&b[0]));
    b[0].birth_year=2000;
    assert(birthday_valid(&b[0]));
    b[0].birth_year=0;
    assert(birthday_valid(&b[0]));
    assert(birthday_wait_expired(queued,queued+BIRTHDAY_WAIT_MS));
    assert(birthday_wait_expired(queued,queued-1));
    return 0;
}
