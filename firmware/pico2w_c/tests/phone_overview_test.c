#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "phone_overview.h"

int main(void) {
    alarm_entry_t alarms[CLOCK_ALARM_COUNT];
    alarm_defaults(alarms);
    alarm_snooze_t snoozes[CLOCK_ALARM_COUNT] = {0};
    phone_overview_t s = {0};
    ds1302_datetime_t now = {.year=2026,.month=11,.day=13,.hour=13,.weekday=1};
    phone_overview_calendar(&s, &now, alarms, snoozes, 0, 1000);
    assert(s.rtc_valid && s.weekday == 5 && !strcmp(s.date,"2026-11-13"));
    assert(!s.next_slot && !s.snooze_count);
    alarms[0].enabled=alarms[1].enabled=true;
    alarms[0].hour=alarms[1].hour=14;
    alarms[0].weekdays=alarms[1].weekdays=CLOCK_ALARM_DAYS_WEEKDAYS;
    phone_overview_calendar(&s, &now, alarms, snoozes, 0, 1000);
    assert(s.next_slot==1 && !strcmp(s.next_at,"2026-11-13T14:00:00"));
    assert(!s.next_snoozed);
    alarms[0].weekdays=alarms[1].weekdays=CLOCK_ALARM_DAYS_WEEKEND;
    phone_overview_calendar(&s, &now, alarms, snoozes, 0, 1000);
    assert(!strcmp(s.next_at,"2026-11-14T14:00:00"));
    snoozes[4]=(alarm_snooze_t){.pending=true,.deadline_ms=181001};
    snoozes[1]=(alarm_snooze_t){.pending=true,.deadline_ms=181001};
    phone_overview_calendar(&s, &now, alarms, snoozes, 0, 1000);
    assert(s.snooze_count==2 && s.snooze_slot==2 && s.snooze_seconds==181);
    assert(s.next_slot==2 && s.next_snoozed && !s.snooze_waiting);
    phone_overview_calendar(&s, &now, alarms, snoozes, 1u<<2, 182000);
    assert(s.snooze_count==3 && s.snooze_slot==2 && s.snooze_waiting && !s.snooze_seconds);
    phone_overview_calendar(&s, NULL, alarms, snoozes, 0, 1000);
    assert(!s.rtc_valid && !s.date[0] && !s.next_slot && !s.next_at[0]);
    assert(s.snooze_count==2 && s.snooze_seconds==181);
    now.month=2; now.day=30;
    phone_overview_calendar(&s, &now, alarms, snoozes, 0, 1000);
    assert(!s.rtc_valid);
    // The calculation never claims, cancels or changes a schedule/snooze.
    assert(snoozes[1].pending && snoozes[4].pending && alarms[0].enabled);
    snoozes[1].pending=false;
    snoozes[4].deadline_ms=(uint64_t)UINT32_MAX+2000u;
    now.month=11; now.day=13;
    phone_overview_calendar(&s,&now,alarms,snoozes,0,(uint64_t)UINT32_MAX-1000u);
    assert(s.snooze_slot==5 && s.snooze_seconds==3 && s.next_snoozed);

    char buffer[1024];
    s=(phone_overview_t){.sound_on=true,.bt_state=PHONE_BT_CONNECTED};
    memcpy(s.bt_name,"A\"\\\n",5);
    int length=phone_overview_json(buffer,sizeof(buffer),&s);
    assert(length>0 && strstr(buffer,"A\\\"\\\\\\u000a"));
    memcpy(s.bt_name,"\xd0\x9a\xd0\xbe\xd0\xbb\xd0\xbe\xd0\xbd\xd0\xba\xd0\xb0",15);
    assert(phone_overview_json(buffer,sizeof(buffer),&s)>0 && strstr(buffer,s.bt_name));
    memset(s.bt_name,0xff,1); s.bt_name[1]=0;
    assert(phone_overview_json(buffer,sizeof(buffer),&s)>0 && strstr(buffer,"\\ufffd"));
    memset(s.bt_name,1,sizeof(s.bt_name)-1); s.bt_name[24]=0;
    s.timer_sound_id=s.active_id=s.snooze_revision=UINT32_MAX;
    s.snooze_seconds=s.network.seconds=s.ntp.sample_age=s.ntp.sync_age=UINT32_MAX;
    length=phone_overview_json(buffer,sizeof(buffer),&s);
    assert(length>0 && length<1000);
    char short_buffer[9]; memset(short_buffer,'!',sizeof(short_buffer));
    assert(phone_overview_json(short_buffer,8,&s)==-1 && short_buffer[8]=='!');
    puts("phone_overview_test: PASS");
    return 0;
}
