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
    assert(access.last.id == 1 && access.last.action == PHONE_TIMER_START && access.last.seconds == 60);
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
    return 0;
}
