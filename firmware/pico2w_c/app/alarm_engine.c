#include "alarm_engine.h"
#include "clock_time.h"
#include <string.h>

int alarm_next_occurrence(const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                          const ds1302_datetime_t *now, ds1302_datetime_t *next) {
    uint64_t wall, best=UINT64_MAX;
    if (!alarms || !next || !clock_datetime_to_utc(now,&wall)) return -1;
    int slot=-1;
    uint64_t midnight=wall-wall%86400u;
    for (unsigned day=0;day<=7;++day) {
        ds1302_datetime_t date;
        if (!clock_utc_to_datetime(midnight+(uint64_t)day*86400u,&date)) continue;
        for (unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
            const alarm_entry_t *a=&alarms[i];
            if (!a->enabled || !alarm_entry_valid(a) ||
                !alarm_day_matches(a,date.year,date.month,date.day)) continue;
            uint64_t due=midnight+(uint64_t)day*86400u+(uint64_t)a->hour*3600u+(uint64_t)a->minute*60u;
            if (due<=wall || due>=best) continue;
            best=due;
            slot=(int)i;
        }
    }
    if (slot>=0 && !clock_utc_to_datetime(best,next)) return -1;
    return slot;
}

void alarm_defaults(alarm_entry_t alarms[CLOCK_ALARM_COUNT]) {
    if (!alarms) return;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        alarms[i].enabled = false;
        alarms[i].hour = (uint8_t)(7u + i);
        alarms[i].minute = 0u;
        alarms[i].sound = ALARM_SOUND_SIGNAL;
        alarms[i].track = 1u;
        alarms[i].weekdays=CLOCK_ALARM_DAYS_ALL;
    }
}

bool alarm_entry_valid(const alarm_entry_t *a) {
    return a && a->hour <= 23u && a->minute <= 59u &&
           (a->sound == ALARM_SOUND_SIGNAL || a->sound == ALARM_SOUND_MUSIC || a->sound == ALARM_SOUND_RANDOM) &&
           a->track >= 1u && a->weekdays>=1 && a->weekdays<=CLOCK_ALARM_DAYS_ALL;
}

bool alarm_day_matches(const alarm_entry_t *a, uint16_t year, uint8_t month, uint8_t day) {
    ds1302_datetime_t d={.year=year,.month=month,.day=day};
    uint64_t epoch;
    return a && clock_datetime_to_utc(&d,&epoch) && clock_utc_to_datetime(epoch,&d) &&
           (a->weekdays & (1u<<(d.weekday-1)))!=0;
}
bool alarm_days_parse(const char *text, uint8_t *mask) {
    if(!text || !mask || !*text) return false;
    if(!strcmp(text,"daily")) { *mask=CLOCK_ALARM_DAYS_ALL; return true; }
    if(!strcmp(text,"weekdays")) { *mask=CLOCK_ALARM_DAYS_WEEKDAYS; return true; }
    if(!strcmp(text,"weekends")) { *mask=CLOCK_ALARM_DAYS_WEEKEND; return true; }
    uint8_t value=0;
    for(const char *p=text;*p;++p) {
        if(*p<'1' || *p>'7' || (value & (1u<<(*p-'1')))) return false;
        value|=(uint8_t)(1u<<(*p-'1'));
    }
    *mask=value;
    return true;
}
const char *alarm_days_name(uint8_t mask) {
    if(mask==CLOCK_ALARM_DAYS_ALL) return "Daily";
    if(mask==CLOCK_ALARM_DAYS_WEEKDAYS) return "Mon-Fri";
    if(mask==CLOCK_ALARM_DAYS_WEEKEND) return "Sat-Sun";
    return "Custom";
}
void alarm_snooze_schedule(alarm_snooze_t *s, uint64_t now, uint8_t hour, uint8_t minute, bool test) {
    *s=(alarm_snooze_t){.pending=true,.test=test,.hour=hour,.minute=minute,
                        .deadline_ms=now+CLOCK_ALARM_SNOOZE_MS};
}
bool alarm_snooze_take(alarm_snooze_t *s, uint64_t now, alarm_snooze_t *due) {
    if(!s->pending || now<s->deadline_ms) return false;
    *due=*s; s->pending=false;
    return true;
}

void alarm_scheduler_init(alarm_scheduler_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
}

static uint64_t calendar_minute_key(uint16_t year, uint8_t month,
                                    uint8_t day, uint8_t hour, uint8_t minute) {
    return (((((uint64_t)year * 13u + month) * 32u + day) * 24u + hour) *
            60u + minute);
}

int alarm_scheduler_pick(alarm_scheduler_t *state,
                         const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
                         uint16_t year, uint8_t month, uint8_t day,
                         uint8_t hour, uint8_t minute, bool alarm_active) {
    if (!state || !alarms || year < 2000u || year > 2099u ||
        month < 1u || month > 12u || day < 1u || day > 31u ||
        hour > 23u || minute > 59u) return -1;
    uint64_t key = calendar_minute_key(year, month, day, hour, minute);
    if (!state->minute_valid || state->minute_key != key) {
        state->minute_valid = true;
        state->minute_key = key;
        state->fired_mask = 0u;
    }
    if (alarm_active) return -1;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        if ((state->fired_mask & (1u << i)) || !alarm_entry_valid(a) ||
            !a->enabled || a->hour != hour || a->minute != minute ||
            !alarm_day_matches(a,year,month,day)) {
            continue;
        }
        state->fired_mask |= (uint8_t)(1u << i);
        return (int)i;
    }
    return -1;
}

int alarm_scheduler_claim_due(alarm_scheduler_t *state,
    const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
    uint16_t year, uint8_t month, uint8_t day,
    uint8_t hour, uint8_t minute, uint8_t *other_mask) {
    if (other_mask) *other_mask = 0u;
    if (!state || !alarms || year < 2000u || year > 2099u ||
        month < 1u || month > 12u || day < 1u || day > 31u ||
        hour > 23u || minute > 59u) return -1;
    uint64_t key = calendar_minute_key(year, month, day, hour, minute);
    if (!state->minute_valid || state->minute_key != key) {
        state->minute_valid = true;
        state->minute_key = key;
        state->fired_mask = 0u;
    }
    int winner = -1;
    uint8_t skipped = 0u;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        uint8_t bit = (uint8_t)(1u << i);
        if ((state->fired_mask & bit) || !alarm_entry_valid(a) ||
            !a->enabled || a->hour != hour || a->minute != minute ||
            !alarm_day_matches(a,year,month,day)) continue;
        state->fired_mask |= bit;
        if (winner < 0) winner = (int)i;
        else skipped |= bit;
    }
    if (other_mask) *other_mask = skipped;
    return winner;
}

const char *alarm_sound_name(alarm_sound_t sound) {
    return sound == ALARM_SOUND_MUSIC ? "MUSIC" : sound == ALARM_SOUND_RANDOM ? "RANDOM" : "SIGNAL";
}
