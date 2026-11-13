#include "phone_access.h"
#include "countdown.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool matching_header(const char *header, size_t length, const char *key, const char *value) {
    size_t key_length = strlen(key), value_length = strlen(value);
    unsigned matches = 0;
    const char *p = header, *limit = header + length;
    while (p < limit && *p) {
        const char *end = p;
        while (end < limit && *end && *end != '\r' && *end != '\n') ++end;
        size_t i = 0;
        while (i < key_length && p + i < end &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)key[i])) ++i;
        if (i == key_length) {
            const char *start = p + key_length;
            while (start < end && (*start == ' ' || *start == '\t')) ++start;
            if ((size_t)(end - start) != value_length || memcmp(start, value, value_length) != 0)
                return false;
            ++matches;
        }
        while (end < limit && (*end == '\r' || *end == '\n')) ++end;
        p = end;
    }
    return matches == 1;
}

bool phone_access_same_origin(const char *header, size_t length, const char *address) {
    char origin[sizeof("http://255.255.255.255")];
    int count = snprintf(origin, sizeof(origin), "http://%s", address);
    return address[0] && count > 0 && (size_t)count < sizeof(origin) &&
           matching_header(header, length, "Host:", address) &&
           matching_header(header, length, "Origin:", origin);
}

void phone_access_begin(phone_access_t *access, uint32_t code, uint64_t now) {
    access->code = code;
    access->code_until = now + PHONE_CODE_MS;
    access->attempts = 5;
    access->paired = false;
}

void phone_access_reset(phone_access_t *access, uint32_t epoch) {
    access->code_until = 0;
    access->attempts = 0;
    access->paired = false;
    access->epoch = epoch;
    access->last = (phone_command_t){0};
    access->result = PHONE_COMMAND_PENDING;
}

static unsigned browser_index(const phone_access_t *access, const char *token, uint8_t scope) {
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i) {
        const phone_browser_t *browser = &access->settings.browsers[i];
        unsigned difference = 0;
        for (unsigned j = 0; j < PHONE_TOKEN_LEN; ++j)
            difference |= (unsigned)(browser->token[j] ^ token[j]);
        if (browser->scope == scope && !difference) return i;
    }
    return PHONE_BROWSER_MAX;
}

phone_reply_t phone_access_check(const phone_access_t *access, const char *token, uint8_t scope) {
    if (strlen(token) != PHONE_TOKEN_LEN || !phone_hex_valid(token, PHONE_TOKEN_LEN))
        return PHONE_INVALID;
    return browser_index(access, token, scope) < PHONE_BROWSER_MAX ? PHONE_OK : PHONE_DENIED;
}

phone_reply_t phone_access_logout(phone_access_t *access, const char *token, uint8_t scope) {
    if (strlen(token) != PHONE_TOKEN_LEN || !phone_hex_valid(token, PHONE_TOKEN_LEN))
        return PHONE_INVALID;
    unsigned i = browser_index(access, token, scope);
    if (i < PHONE_BROWSER_MAX) {
        access->settings.browsers[i] = (phone_browser_t){0};
        access->changed = true;
    }
    return PHONE_OK;
}

static bool read_number(const char **text, uint32_t *value) {
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    uint32_t number = 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (number > (UINT32_MAX - digit) / 10u) return false;
        number = number * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = number;
    return true;
}

phone_reply_t phone_access_pair(phone_access_t *access, const char *body, uint64_t now, uint8_t scope) {
    if (strlen(body) != 7u + PHONE_TOKEN_LEN || body[6] != ' ' ||
        !phone_hex_valid(body + 7, PHONE_TOKEN_LEN)) return PHONE_INVALID;
    const char *end = body;
    uint32_t code;
    if (!read_number(&end, &code) || end != body + 6) return PHONE_INVALID;
    const char *token = body + 7;
    if (browser_index(access, token, scope) < PHONE_BROWSER_MAX) return PHONE_OK;
    if (now >= access->code_until || !access->attempts) return PHONE_DENIED;
    --access->attempts;
    if (code != access->code) return PHONE_DENIED;
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i) {
        phone_browser_t *browser = &access->settings.browsers[i];
        if (!browser->scope) {
            browser->scope = scope;
            memcpy(browser->token, token, sizeof(browser->token));
            access->code_until = 0;
            access->paired = true;
            access->changed = true;
            return PHONE_OK;
        }
    }
    return PHONE_BUSY;
}

// Lossless encodings of editable fields, independent of the Flash record layout.
// A birthday's handled year is maintained by the clock and is not editable.
uint32_t phone_alarm_revision(const alarm_entry_t *alarm) {
    return (uint32_t)alarm->enabled | ((uint32_t)alarm->hour << 1) |
        ((uint32_t)alarm->minute << 6) | ((uint32_t)alarm->sound << 12) |
        ((uint32_t)alarm->track << 14) | ((uint32_t)alarm->weekdays << 22);
}

uint64_t phone_birthday_revision(const birthday_t *birthday) {
    return (uint32_t)birthday->enabled | ((uint32_t)birthday->month << 1) |
        ((uint32_t)birthday->day << 5) | ((uint32_t)birthday->hour << 10) |
        ((uint32_t)birthday->minute << 15) | ((uint64_t)birthday->birth_year << 21);
}

static bool read_revision(const char **text, uint64_t *value) {
    const char *p = *text;
    if (*p < '0' || *p > '9') return false;
    uint64_t number = 0;
    do {
        unsigned digit = (unsigned)(*p++ - '0');
        if (number > (UINT64_MAX - digit) / 10u) return false;
        number = number * 10u + digit;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = number;
    return true;
}

static bool read_schedule(const char *text, phone_command_t *request) {
    uint32_t fields[8] = {0};
    unsigned count = request->action == PHONE_SET_ALARM ? 8u : 7u;
    if (!read_number(&text, &fields[0]) || *text++ != ' ' ||
        !read_revision(&text, &request->expected) || *text++ != ' ') return false;
    for (unsigned i = 2; i < count; ++i) {
        if (!read_number(&text, &fields[i]) ||
            (i + 1u < count && *text++ != ' ')) return false;
    }
    unsigned slots = request->action == PHONE_SET_ALARM ? CLOCK_ALARM_COUNT : CLOCK_BIRTHDAY_COUNT;
    if (fields[0] < 1u || fields[0] > slots || fields[2] > 1u) return false;
    for (unsigned i = 3; i < count; ++i)
        if (fields[i] > UINT8_MAX) return false;
    request->slot = (uint8_t)(fields[0] - 1u);
    if (request->action == PHONE_SET_ALARM) {
        if (*text || request->expected > UINT32_MAX) return false;
        request->alarm = (alarm_entry_t){.enabled = fields[2] != 0, .hour = (uint8_t)fields[3],
            .minute = (uint8_t)fields[4], .weekdays = (uint8_t)fields[5],
            .sound = (alarm_sound_t)fields[6], .track = (uint8_t)fields[7]};
        return alarm_entry_valid(&request->alarm);
    }
    request->birthday = (birthday_t){.enabled = fields[2] != 0, .month = (uint8_t)fields[3],
        .day = (uint8_t)fields[4], .hour = (uint8_t)fields[5], .minute = (uint8_t)fields[6]};
    if (*text == ' ') {
        uint32_t year;
        ++text;
        if (!read_number(&text, &year) || year > 9999u) return false;
        request->birthday.birth_year = (uint16_t)year;
        request->birthday_year_supplied = true;
    }
    if (*text) return false;
    return birthday_valid(&request->birthday);
}

static phone_reply_t accept_command(phone_access_t *access, const char *body,
                                    uint8_t scope, bool home_peer, unsigned first, unsigned limit) {
    const char *p;
    if (strncmp(body, "- ", 2) == 0) {
        if (scope != PHONE_SCOPE_HOME || !home_peer || !access->settings.home_trusted) return PHONE_DENIED;
        p = body + 2;
    } else {
        if (strlen(body) <= PHONE_TOKEN_LEN || body[PHONE_TOKEN_LEN] != ' ' ||
            !phone_hex_valid(body, PHONE_TOKEN_LEN)) return PHONE_INVALID;
        if (browser_index(access, body, scope) == PHONE_BROWSER_MAX) return PHONE_DENIED;
        p = body + PHONE_TOKEN_LEN + 1;
    }

    uint32_t epoch;
    phone_command_t request = {0};
    if (!read_number(&p, &epoch) || *p++ != ' ' ||
        !read_number(&p, &request.id) || !request.id || *p++ != ' ' ||
        !read_number(&p, &request.nonce) || !request.nonce || *p++ != ' ') return PHONE_INVALID;
    static const struct {
        const char *text;
        uint32_t minimum, maximum;
    } actions[] = {
        {"set ", 1, CLOCK_TIMER_MAX_SECONDS}, {"start ", 1, CLOCK_TIMER_MAX_SECONDS},
        {"cancel ", 0, 0}, {"stop_sound ", 1, UINT32_MAX}, {"play ", 0, 0}, {"pause ", 0, 0}, {"resume ", 0, 0},
        {"stop ", 0, 0}, {"next ", 0, 0}, {"previous ", 0, 0},
        {"test_track ", 1, 255},
        {"test_stop ", 0, 0},
        {"volume_local ", 0, 30}, {"volume_bt ", 0, 30}, {"language ", 0, 1},
        {"audio_manual ", 0, 1}, {"audio_hourly ", 0, 1},
        {"audio_timer ", 0, 1}, {"audio_alarm ", 0, 1},
        {"light_mode ", 0, 2}, {"light_day ", 0, 100}, {"light_night ", 0, 100},
        {"timer_sound ", 0, 2}, {"timer_track ", 1, 255}, {"timer_voice ", 0, 1}, {"timer_random ", 0, 1}, {"timer_warning ", 0, 1},
        {"sound ", 0, 1}, {"say_time ", 0, 0}, {"say_date ", 0, 0}, {"next_alarm ", 0, 0},
        {"alarm_stop ", 1, UINT32_MAX}, {"alarm_snooze ", 1, UINT32_MAX},
        {"snooze_cancel ", 1, UINT32_MAX},
        {"set ", 0, 0}, {"set ", 0, 0}
    };
    _Static_assert(sizeof(actions) / sizeof(actions[0]) == PHONE_ACTION_COUNT, "Missing phone action");
    unsigned i = first;
    for (; i < limit; ++i) {
        size_t length = strlen(actions[i].text);
        if (strncmp(p, actions[i].text, length) == 0) {
            request.action = (phone_action_t)i;
            p += length;
            break;
        }
    }
    if (i == limit) return PHONE_INVALID;
    if (request.action >= PHONE_SET_ALARM) {
        if (!read_schedule(p, &request)) return PHONE_INVALID;
    } else if (!read_number(&p, &request.value) || *p ||
        request.value < actions[i].minimum || request.value > actions[i].maximum)
        return PHONE_INVALID;

    if (epoch != access->epoch) return PHONE_BUSY;
    if (request.id == access->last.id)
        return request.nonce == access->last.nonce && request.action == access->last.action &&
               request.value == access->last.value && request.slot == access->last.slot &&
               request.expected == access->last.expected &&
               request.birthday_year_supplied == access->last.birthday_year_supplied &&
               phone_alarm_revision(&request.alarm) == phone_alarm_revision(&access->last.alarm) &&
               phone_birthday_revision(&request.birthday) == phone_birthday_revision(&access->last.birthday) ?
               PHONE_OK : PHONE_BUSY;
    if (request.id != access->last.id + 1u ||
        (access->last.id && access->result == PHONE_COMMAND_PENDING)) return PHONE_BUSY;
    access->last = request;
    access->result = PHONE_COMMAND_PENDING;
    return PHONE_OK;
}

phone_reply_t phone_access_timer(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_TIMER_SET, PHONE_PLAYER_PLAY);
}

phone_reply_t phone_access_player(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_PLAYER_PLAY, PHONE_SET_VOLUME_LOCAL);
}

phone_reply_t phone_access_settings(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_SET_VOLUME_LOCAL, PHONE_SOUND_SET);
}

phone_reply_t phone_access_control(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_SOUND_SET, PHONE_SET_ALARM);
}

phone_reply_t phone_access_alarm(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_SET_ALARM, PHONE_SET_BIRTHDAY);
}

phone_reply_t phone_access_birthday(phone_access_t *access, const char *body, uint8_t scope, bool home_peer) {
    return accept_command(access, body, scope, home_peer, PHONE_SET_BIRTHDAY, PHONE_ACTION_COUNT);
}
