#include "phone_access.h"
#include <assert.h>
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
    assert(phone_access_timer(&access, TOKEN " 9 1 11 set 14401", PHONE_SCOPE_HOME, false) == PHONE_INVALID);
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
    return 0;
}
