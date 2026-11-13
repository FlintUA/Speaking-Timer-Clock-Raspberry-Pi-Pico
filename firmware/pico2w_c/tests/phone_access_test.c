#include "phone_access.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#define TOKEN "0123456789abcdef0123456789abcdef"
#define OTHER "fedcba9876543210fedcba9876543210"
#define THIRD "0123456789abcdef0123456789abcde1"
#define FOURTH "0123456789abcdef0123456789abcde2"
#define FIFTH "0123456789abcdef0123456789abcde3"

int main(void) {
    const char headers[] = "HTTP/1.1\r\nHoSt: 192.168.4.1\r\nOrigin: http://192.168.4.1";
    const char wrong[] = "HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: https://192.168.4.1";
    const char duplicate[] = "HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1";
    assert(phone_access_same_origin(headers, sizeof(headers), "192.168.4.1"));
    assert(!phone_access_same_origin(headers, sizeof(headers) - 2u, "192.168.4.1"));
    assert(!phone_access_same_origin(headers, sizeof(headers), "192.168.2.116"));
    assert(!phone_access_same_origin(wrong, sizeof(wrong), "192.168.4.1"));
    assert(!phone_access_same_origin(duplicate, sizeof(duplicate), "192.168.4.1"));

    phone_access_t access = {0};
    phone_access_reset(&access, 9);
    assert(phone_access_pair(&access, "123456 " TOKEN, 1, PHONE_SCOPE_HOME) == PHONE_DENIED);
    phone_access_begin(&access, 123456, 10);
    for (unsigned i = 0; i < 5; ++i)
        assert(phone_access_pair(&access, "000000 " TOKEN, 11 + i, PHONE_SCOPE_HOME) == PHONE_DENIED);
    assert(phone_access_pair(&access, "123456 " TOKEN, 16, PHONE_SCOPE_HOME) == PHONE_DENIED);
    phone_access_begin(&access, 123456, 20);
    assert(phone_access_pair(&access, "123456 " TOKEN, 20 + PHONE_CODE_MS, PHONE_SCOPE_HOME) == PHONE_DENIED);
    phone_access_begin(&access, 42, 100);
    assert(phone_access_pair(&access, "000042 " TOKEN, 101, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(access.paired && access.changed && !access.code_until);
    assert(phone_access_pair(&access, "000042 " TOKEN, 102, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_pair(&access, "000042 " OTHER, 103, PHONE_SCOPE_HOME) == PHONE_DENIED);
    assert(phone_access_check(&access, TOKEN, PHONE_SCOPE_DIRECT) == PHONE_DENIED);
    phone_access_begin(&access, 43, 104);
    assert(phone_access_pair(&access, "000043 " OTHER, 105, PHONE_SCOPE_DIRECT) == PHONE_OK);
    assert(phone_access_check(&access, TOKEN, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_check(&access, OTHER, PHONE_SCOPE_DIRECT) == PHONE_OK);

    assert(phone_access_timer(&access, "- 9 1 11 start 60", PHONE_SCOPE_HOME, true) == PHONE_DENIED);
    access.settings.home_trusted = 1;
    assert(phone_access_timer(&access, "- 9 1 11 start 60", PHONE_SCOPE_HOME, false) == PHONE_DENIED);
    assert(phone_access_timer(&access, "- 9 1 11 start 60", PHONE_SCOPE_DIRECT, true) == PHONE_DENIED);
    assert(phone_access_timer(&access, OTHER " 9 1 11 start 60", PHONE_SCOPE_HOME, false) == PHONE_DENIED);
    assert(phone_access_timer(&access, TOKEN " 9 1 11 start 0", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 9 1 11 set 86400", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 9 1 11 cancel 1", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 9 4294967296 11 start 60", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
    assert(phone_access_timer(&access, TOKEN " 9 1 11 start 60 extra", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
    assert(phone_access_timer(&access, "- 9 1 11 start 60", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(access.last.id == 1 && access.last.action == PHONE_TIMER_START && access.last.value == 60);
    assert(access.result == PHONE_COMMAND_PENDING);
    assert(phone_access_timer(&access, TOKEN " 9 2 12 cancel 0", PHONE_SCOPE_HOME, false) == PHONE_BUSY);
    access.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_timer(&access, "- 9 1 11 start 60", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(access.result == PHONE_COMMAND_APPLIED); // A retry never queues it again.
    assert(phone_access_timer(&access, "- 9 1 12 start 60", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    assert(phone_access_timer(&access, TOKEN " 9 2 12 cancel 0", PHONE_SCOPE_HOME, false) == PHONE_OK);
    access.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_timer(&access, TOKEN " 9 3 13 set 86399", PHONE_SCOPE_HOME, false) == PHONE_OK);
    assert(access.last.action == PHONE_TIMER_SET && access.last.value == 86399u);
    access.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_timer(&access, TOKEN " 9 1 11 start 60", PHONE_SCOPE_HOME, false) == PHONE_BUSY);

    phone_access_reset(&access, 10);
    assert(!access.last.id && phone_access_check(&access, TOKEN, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_timer(&access, TOKEN " 9 1 11 start 60", PHONE_SCOPE_HOME, false) == PHONE_BUSY);
    assert(phone_access_logout(&access, TOKEN, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_logout(&access, TOKEN, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_check(&access, TOKEN, PHONE_SCOPE_HOME) == PHONE_DENIED);
    assert(phone_access_check(&access, OTHER, PHONE_SCOPE_DIRECT) == PHONE_OK);
    phone_access_begin(&access, 42, 1000);
    assert(phone_access_pair(&access, "000042 " THIRD, 1001, PHONE_SCOPE_HOME) == PHONE_OK);
    phone_access_begin(&access, 42, 1010);
    assert(phone_access_pair(&access, "000042 " FOURTH, 1011, PHONE_SCOPE_DIRECT) == PHONE_OK);
    phone_access_begin(&access, 42, 1020);
    assert(phone_access_pair(&access, "000042 " FIFTH, 1021, PHONE_SCOPE_HOME) == PHONE_OK);
    phone_access_begin(&access, 42, 1030);
    assert(phone_access_pair(&access, "000042 " TOKEN, 1031, PHONE_SCOPE_HOME) == PHONE_BUSY);
    assert(phone_access_check(&access, OTHER, PHONE_SCOPE_DIRECT) == PHONE_OK);
    assert(phone_access_logout(&access, THIRD, PHONE_SCOPE_HOME) == PHONE_OK);
    assert(phone_access_pair(&access, "000042 " TOKEN, 1032, PHONE_SCOPE_HOME) == PHONE_OK);

    phone_access_t player = {0};
    phone_access_reset(&player, 20);
    assert(phone_access_player(&player, "- 20 1 11 play 0", PHONE_SCOPE_HOME, true) == PHONE_DENIED);
    player.settings.home_trusted = 1;
    assert(phone_access_player(&player, "- 20 1 11 play 0", PHONE_SCOPE_DIRECT, true) == PHONE_DENIED);
    assert(phone_access_player(&player, "- 20 1 11 play 0", PHONE_SCOPE_HOME, false) == PHONE_DENIED);
    assert(phone_access_player(&player, "- 20 1 11 start 60", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_timer(&player, "- 20 1 11 next 0", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 20 1 11 play 1", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 20 1 11 next 0 extra", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 20 1 11 next 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(player.last.action == PHONE_PLAYER_NEXT && player.result == PHONE_COMMAND_PENDING);
    assert(phone_access_timer(&player, "- 20 2 12 cancel 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    player.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_player(&player, "- 20 1 11 next 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(player.result == PHONE_COMMAND_APPLIED); // Retrying Next must not advance a second time.
    assert(phone_access_player(&player, "- 20 1 12 next 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    assert(phone_access_timer(&player, "- 20 2 12 cancel 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    player.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_player(&player, "- 20 1 11 next 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    const char *const actions[] = {"play", "pause", "resume", "stop", "next", "previous"};
    for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        char body[80];
        snprintf(body, sizeof(body), "- 20 %u %u %s 0", i + 3u, i + 30u, actions[i]);
        assert(phone_access_player(&player, body, PHONE_SCOPE_HOME, true) == PHONE_OK);
        assert(player.last.action == (phone_action_t)(PHONE_PLAYER_PLAY + i));
        player.result = PHONE_COMMAND_APPLIED;
    }
    phone_access_reset(&player, 21);
    assert(phone_access_player(&player, "- 20 1 11 play 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    assert(phone_access_player(&player, "- 21 1 11 test_track 0", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 21 1 11 test_track 256", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_alarm(&player, "- 21 1 11 test_track 12", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 21 1 11 test_track 255", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(player.last.action == PHONE_PLAYER_TEST_TRACK && player.last.value == 255);
    player.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_player(&player, "- 21 1 11 test_track 255", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(player.result == PHONE_COMMAND_APPLIED);

    assert(phone_access_player(&player, "- 21 2 12 test_stop 1", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&player, "- 21 2 12 test_stop 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(player.last.action == PHONE_PLAYER_TEST_STOP);
    player.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_player(&player, "- 21 2 12 test_stop 0", PHONE_SCOPE_HOME, true) == PHONE_OK);

    phone_access_reset(&player,22);
    assert(phone_access_player(&player,"- 22 1 31 mode 3",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_player(&player,"- 22 1 31 sleep 1",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_player(&player,"- 22 1 31 sleep 91",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&player,"- 22 1 31 mode 1",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_player(&player,"- 22 1 31 mode 2",PHONE_SCOPE_DIRECT,true)==PHONE_DENIED);
    assert(phone_access_player(&player,"- 22 1 31 mode 2",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(player.last.action==PHONE_PLAYER_MODE && player.last.value==2);
    player.result=PHONE_COMMAND_APPLIED;
    const unsigned sleeps[]={0,15,30,60,90};
    for(unsigned i=0;i<5;++i) {
        char body[80];snprintf(body,sizeof(body),"- 22 %u %u sleep %u",i+2,i+32,sleeps[i]);
        assert(phone_access_player(&player,body,PHONE_SCOPE_HOME,true)==PHONE_OK);
        assert(player.last.action==PHONE_PLAYER_SLEEP && player.last.value==sleeps[i]);
        player.result=PHONE_COMMAND_APPLIED;
        assert(phone_access_player(&player,body,PHONE_SCOPE_HOME,true)==PHONE_OK);
        assert(player.result==PHONE_COMMAND_APPLIED); // Lost response must not restart sleep.
    }

    phone_access_t settings = {0};
    phone_access_reset(&settings, 30);
    assert(phone_access_settings(&settings, "- 30 1 40 volume_local 15", PHONE_SCOPE_HOME, true) == PHONE_DENIED);
    settings.settings.home_trusted = 1;
    assert(phone_access_settings(&settings, "- 30 1 40 light_day 50", PHONE_SCOPE_HOME, false) == PHONE_DENIED);
    assert(phone_access_settings(&settings, "- 30 1 40 light_day 50", PHONE_SCOPE_DIRECT, true) == PHONE_DENIED);
    assert(phone_access_settings(&settings, "- 30 1 40 next 0", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_player(&settings, "- 30 1 40 volume_local 15", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_timer(&settings, "- 30 1 40 light_day 50", PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    const struct { const char *name; unsigned maximum; phone_action_t action; } fields[] = {
        {"volume_local", 30, PHONE_SET_VOLUME_LOCAL}, {"volume_bt", 30, PHONE_SET_VOLUME_BT},
        {"language", 1, PHONE_SET_LANGUAGE}, {"audio_manual", 1, PHONE_SET_AUDIO_MANUAL},
        {"audio_hourly", 1, PHONE_SET_AUDIO_HOURLY}, {"audio_timer", 1, PHONE_SET_AUDIO_TIMER},
        {"audio_alarm", 1, PHONE_SET_AUDIO_ALARM}, {"light_mode", 2, PHONE_SET_LIGHT_MODE},
        {"light_day", 100, PHONE_SET_LIGHT_DAY}, {"light_night", 100, PHONE_SET_LIGHT_NIGHT}
    };
    unsigned id = 1;
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        char body[96];
        snprintf(body, sizeof(body), "- 30 %u 40 %s %u", id, fields[i].name, fields[i].maximum + 1u);
        assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_INVALID);
        snprintf(body, sizeof(body), "- 30 %u 40 %s %u", id, fields[i].name, fields[i].maximum);
        assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_OK);
        assert(settings.last.action == fields[i].action && settings.last.value == fields[i].maximum);
        assert(phone_access_timer(&settings, "- 30 99 45 cancel 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
        settings.result = PHONE_COMMAND_APPLIED;
        assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_OK);
        assert(settings.result == PHONE_COMMAND_APPLIED);
        snprintf(body, sizeof(body), "- 30 %u 41 %s 0", ++id, fields[i].name);
        assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_OK);
        assert(settings.last.value == 0);
        settings.result = PHONE_COMMAND_APPLIED;
        ++id;
    }
    char body[96];
    snprintf(body, sizeof(body), "- 30 %u 40 light_day -1", id);
    assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    snprintf(body, sizeof(body), "- 30 %u 40 light_day 4294967296", id);
    assert(phone_access_settings(&settings, body, PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    phone_access_begin(&settings, 42, 100);
    assert(phone_access_pair(&settings, "000042 " TOKEN, 101, PHONE_SCOPE_DIRECT) == PHONE_OK);
    snprintf(body, sizeof(body), TOKEN " 30 %u 4294967295 audio_manual 1", id);
    assert(phone_access_settings(&settings, body, PHONE_SCOPE_DIRECT, false) == PHONE_OK);
    settings.result = PHONE_COMMAND_APPLIED;
    settings.settings.home_trusted = 0;
    assert(phone_access_settings(&settings, "- 30 30 40 light_day 50", PHONE_SCOPE_HOME, true) == PHONE_DENIED);

    phone_access_t schedule = {0};
    phone_access_reset(&schedule, 40);
    const char *alarm_body = "- 40 1 10 set 5 0 1 23 59 127 1 255";
    assert(phone_access_alarm(&schedule, alarm_body, PHONE_SCOPE_HOME, true) == PHONE_DENIED);
    schedule.settings.home_trusted = 1;
    assert(phone_access_alarm(&schedule, alarm_body, PHONE_SCOPE_DIRECT, true) == PHONE_DENIED);
    assert(phone_access_alarm(&schedule, alarm_body, PHONE_SCOPE_HOME, false) == PHONE_DENIED);
    const char *const invalid_alarms[] = {
        "- 40 1 10 set 0 0 1 7 0 127 0 1", "- 40 1 10 set 6 0 1 7 0 127 0 1",
        "- 40 1 10 set 1 0 2 7 0 127 0 1", "- 40 1 10 set 1 0 1 24 0 127 0 1",
        "- 40 1 10 set 1 0 1 7 60 127 0 1", "- 40 1 10 set 1 0 1 7 0 0 0 1",
        "- 40 1 10 set 1 0 1 7 0 128 0 1", "- 40 1 10 set 1 0 1 7 0 127 3 1",
        "- 40 1 10 set 1 0 1 7 0 127 1 0", "- 40 1 10 set 1 0 1 7 0 127 1 256",
        "- 40 1 10 set 1 0 1 256 0 127 0 1", "- 40 1 10 set 1 4294967296 1 7 0 127 0 1",
        "- 40 1 10 set 1 0 1 7 0 127 0", "- 40 1 10 set 1 0 1 7 0 127 0 1 extra"
    };
    for (unsigned i = 0; i < sizeof(invalid_alarms) / sizeof(invalid_alarms[0]); ++i) {
        assert(phone_access_alarm(&schedule, invalid_alarms[i], PHONE_SCOPE_HOME, true) == PHONE_INVALID);
        assert(!schedule.last.id);
    }
    assert(phone_access_alarm(&schedule, alarm_body, PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(schedule.last.slot == 4 && schedule.last.alarm.track == 255);
    assert(schedule.last.alarm.weekdays == 127 && schedule.last.alarm.hour == 23);
    assert(phone_access_settings(&schedule, "- 40 2 11 volume_local 15", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    schedule.result = PHONE_COMMAND_APPLIED;
    assert(phone_access_alarm(&schedule, alarm_body, PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(schedule.result == PHONE_COMMAND_APPLIED);
    assert(phone_access_alarm(&schedule, "- 40 1 10 set 5 1 1 23 59 127 1 255", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    assert(phone_access_settings(&schedule, alarm_body, PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_timer(&schedule, alarm_body, PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    const char *const invalid_birthdays[] = {
        "- 40 2 11 set 0 0 1 1 1 7 0", "- 40 2 11 set 16 0 1 1 1 7 0",
        "- 40 2 11 set 1 0 2 1 1 7 0", "- 40 2 11 set 1 0 1 0 1 7 0",
        "- 40 2 11 set 1 0 1 13 1 7 0", "- 40 2 11 set 1 0 1 4 31 7 0",
        "- 40 2 11 set 1 0 1 2 30 7 0", "- 40 2 11 set 1 0 1 1 0 7 0",
        "- 40 2 11 set 1 0 1 1 1 24 0", "- 40 2 11 set 1 0 1 1 1 7 60",
        "- 40 2 11 set 1 0 1 1 1 7", "- 40 2 11 set 1 0 1 1 1 7 0 extra"
    };
    for (unsigned i = 0; i < sizeof(invalid_birthdays) / sizeof(invalid_birthdays[0]); ++i)
        assert(phone_access_birthday(&schedule, invalid_birthdays[i], PHONE_SCOPE_HOME, true) == PHONE_INVALID);
    assert(phone_access_birthday(&schedule, "- 40 2 11 set 1 0 1 2 29 7 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(schedule.last.slot == 0 && schedule.last.birthday.month == 2 && schedule.last.birthday.day == 29);
    assert(schedule.last.birthday.last_year == 0);
    birthday_t birthday = schedule.last.birthday;
    uint64_t revision = phone_birthday_revision(&birthday);
    birthday.last_year = 2026;
    assert(phone_birthday_revision(&birthday) == revision);
    birthday.minute++;
    assert(phone_birthday_revision(&birthday) != revision);
    birthday.birth_year=2099;
    assert(phone_birthday_revision(&birthday)>UINT32_MAX);
    alarm_entry_t alarm = {.enabled=true,.hour=7,.minute=30,.weekdays=31,.track=255,.sound=ALARM_SOUND_MUSIC};
    revision = phone_alarm_revision(&alarm);
    alarm.weekdays = 96;
    assert(phone_alarm_revision(&alarm) != revision);
    schedule.result = PHONE_COMMAND_CONFLICT;
    assert(phone_access_birthday(&schedule, "- 40 2 11 set 1 0 1 2 29 7 0", PHONE_SCOPE_HOME, true) == PHONE_OK);
    assert(schedule.result == PHONE_COMMAND_CONFLICT);
    assert(phone_access_birthday(&schedule, "- 40 2 11 set 1 0 1 2 28 7 0", PHONE_SCOPE_HOME, true) == PHONE_BUSY);
    schedule.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_birthday(&schedule,"- 40 3 12 set 1 0 1 12 10 7 0 1985",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(schedule.last.birthday.birth_year==1985 && schedule.last.birthday_year_supplied);
    schedule.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_birthday(&schedule,"- 40 4 13 set 1 4163690497 1 2 29 7 0 1985",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_birthday(&schedule,"- 40 4 13 set 1 18446744073709551616 1 12 10 7 0",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_birthday(&schedule,"- 40 4 13 set 1 4163690497 1 12 10 7 0 10000",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_birthday(&schedule,"- 40 4 13 set 1 4401920001 1 12 10 7 0 0",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(schedule.last.expected==UINT64_C(4401920001));
    assert(schedule.last.birthday.birth_year==0 && schedule.last.birthday_year_supplied);
    schedule.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_birthday(&schedule,"- 40 5 14 set 1 4163690497 1 12 10 7 0",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(!schedule.last.birthday_year_supplied); // Legacy callers preserve stored metadata.
    schedule.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_birthday(&schedule,"- 40 6 15 set 15 0 1 12 31 23 59",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(schedule.last.slot==14 && schedule.last.birthday.hour==23);

    phone_access_t control={0};
    phone_access_reset(&control,50);
    assert(phone_access_control(&control,"- 50 1 10 sound 0",PHONE_SCOPE_HOME,true)==PHONE_DENIED);
    control.settings.home_trusted=1;
    assert(phone_access_control(&control,"- 50 1 10 sound 0",PHONE_SCOPE_DIRECT,true)==PHONE_DENIED);
    assert(phone_access_control(&control,"- 50 1 10 sound 0",PHONE_SCOPE_HOME,false)==PHONE_DENIED);
    const char *const invalid_controls[]={"sound 2","sound -1","say_time 1","say_date 0 extra",
        "next_alarm 1","play 0","light_day 10","sound"};
    for(unsigned i=0;i<sizeof(invalid_controls)/sizeof(invalid_controls[0]);++i){
        char command[96];
        snprintf(command,sizeof(command),"- 50 1 10 %s",invalid_controls[i]);
        assert(phone_access_control(&control,command,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        assert(!control.last.id);
    }
    assert(phone_access_settings(&control,"- 50 1 10 sound 0",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_player(&control,"- 50 1 10 say_time 0",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_control(&control,"- 50 1 10 sound 0",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(control.last.action==PHONE_SOUND_SET && control.last.value==0);
    assert(phone_access_player(&control,"- 50 2 11 play 0",PHONE_SCOPE_HOME,true)==PHONE_BUSY);
    control.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_control(&control,"- 50 1 10 sound 0",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(control.result==PHONE_COMMAND_APPLIED);
    const char *const speech_controls[]={"say_time","say_date","next_alarm"};
    for(unsigned i=0;i<3;++i){
        char command[96];
        snprintf(command,sizeof(command),"- 50 %u %u %s 0",i+2,i+11,speech_controls[i]);
        assert(phone_access_control(&control,command,PHONE_SCOPE_HOME,true)==PHONE_OK);
        control.result=PHONE_COMMAND_RTC_UNAVAILABLE;
        assert(phone_access_control(&control,command,PHONE_SCOPE_HOME,true)==PHONE_OK);
        assert(control.result==PHONE_COMMAND_RTC_UNAVAILABLE);
    }
    char control_token[PHONE_TOKEN_LEN+1], control_body[96];
    memset(control_token,'a',PHONE_TOKEN_LEN); control_token[PHONE_TOKEN_LEN]=0;
    control.settings.browsers[0].scope=PHONE_SCOPE_DIRECT;
    memcpy(control.settings.browsers[0].token,control_token,sizeof(control_token));
    snprintf(control_body,sizeof(control_body),"%s 50 5 14 sound 1",control_token);
    assert(phone_access_control(&control,control_body,PHONE_SCOPE_HOME,false)==PHONE_DENIED);
    assert(phone_access_control(&control,control_body,PHONE_SCOPE_DIRECT,false)==PHONE_OK);
    assert(control.last.value==1);
    phone_access_t finish={0}; phone_access_reset(&finish,88); finish.settings.home_trusted=1;
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 0",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 4294967296",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_control(&finish,"- 88 1 9 stop_sound 17",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 17",PHONE_SCOPE_HOME,false)==PHONE_DENIED);
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 17",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(finish.last.action==PHONE_TIMER_SOUND_STOP && finish.last.value==17);
    finish.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 17",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(finish.result==PHONE_COMMAND_APPLIED);
    assert(phone_access_timer(&finish,"- 88 1 9 stop_sound 18",PHONE_SCOPE_HOME,true)==PHONE_BUSY);
    phone_access_t timer_settings={0};phone_access_reset(&timer_settings,90);timer_settings.settings.home_trusted=1;
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_sound 3",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_voice 2",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_track 0",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_track 256",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_control(&timer_settings,"- 90 1 9 timer_voice 1",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_sound 2",PHONE_SCOPE_HOME,false)==PHONE_DENIED);
    assert(phone_access_settings(&timer_settings,"- 90 1 9 timer_sound 2",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(timer_settings.last.action==PHONE_SET_TIMER_SOUND && timer_settings.last.value==2);
    timer_settings.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_settings(&timer_settings,"- 90 2 10 timer_voice 0",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(timer_settings.last.action==PHONE_SET_TIMER_VOICE && !timer_settings.last.value);
    timer_settings.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_settings(&timer_settings,"- 90 3 11 timer_track 255",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(timer_settings.last.action==PHONE_SET_TIMER_TRACK && timer_settings.last.value==255);
    timer_settings.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_settings(&timer_settings,"- 90 4 12 timer_random 2",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 4 12 timer_random 1",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(timer_settings.last.action==PHONE_SET_TIMER_RANDOM && timer_settings.last.value==1);
    alarm_entry_t x={.enabled=true,.hour=7,.minute=0,.sound=ALARM_SOUND_RANDOM,.track=1,.weekdays=127};
    alarm_entry_t y=x;y.sound=ALARM_SOUND_SIGNAL;
    assert(phone_alarm_revision(&x)!=phone_alarm_revision(&y));
    y=x;y.sound=ALARM_SOUND_SIGNAL;y.track=2;
    assert(phone_alarm_revision(&x)!=phone_alarm_revision(&y)); // Both sound bits are independent of track bits.
    phone_access_t random_schedule={0};phone_access_reset(&random_schedule,91);random_schedule.settings.home_trusted=1;
    assert(phone_access_alarm(&random_schedule,"- 91 1 3 set 5 0 1 7 0 127 2 255",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(random_schedule.last.alarm.sound==ALARM_SOUND_RANDOM && random_schedule.last.slot==4);
    timer_settings.result=PHONE_COMMAND_APPLIED;
    assert(phone_access_settings(&timer_settings,"- 90 5 13 timer_warning 2",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_timer(&timer_settings,"- 90 5 13 timer_warning 1",PHONE_SCOPE_HOME,true)==PHONE_INVALID);
    assert(phone_access_settings(&timer_settings,"- 90 5 13 timer_warning 1",PHONE_SCOPE_HOME,false)==PHONE_DENIED);
    assert(phone_access_settings(&timer_settings,"- 90 5 13 timer_warning 1",PHONE_SCOPE_HOME,true)==PHONE_OK);
    assert(timer_settings.last.action==PHONE_SET_TIMER_WARNING && timer_settings.last.value==1);
    const char *const live_actions[]={"alarm_stop","alarm_snooze","snooze_cancel"};
    for(unsigned i=0;i<3;++i) {
        phone_access_t live={0}; phone_access_reset(&live,77); live.settings.home_trusted=1;
        char live_body[128];
        snprintf(live_body,sizeof(live_body),"- 77 1 11 %s 0",live_actions[i]);
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        snprintf(live_body,sizeof(live_body),"- 77 1 11 %s 4294967296",live_actions[i]);
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        snprintf(live_body,sizeof(live_body),"- 77 1 11 %s 4294967295",live_actions[i]);
        assert(phone_access_alarm(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        assert(phone_access_player(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        assert(phone_access_settings(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_INVALID);
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,false)==PHONE_DENIED);
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_OK);
        assert(live.last.value==UINT32_MAX && live.last.action==PHONE_ALARM_STOP+(phone_action_t)i);
        live.result=PHONE_COMMAND_CONFLICT;
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_OK);
        assert(live.result==PHONE_COMMAND_CONFLICT); // Retry cannot be applied to a later occurrence.
        snprintf(live_body,sizeof(live_body),"- 77 1 11 %s 1",live_actions[i]);
        assert(phone_access_control(&live,live_body,PHONE_SCOPE_HOME,true)==PHONE_BUSY);
    }
    return 0;
}
