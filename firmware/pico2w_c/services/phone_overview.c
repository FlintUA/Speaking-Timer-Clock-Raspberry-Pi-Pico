#include "phone_overview.h"
#include "clock_time.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t remaining_seconds(uint64_t deadline, uint64_t now) {
    uint64_t delta = deadline > now ? deadline - now : 0;
    return delta / 1000u + (delta % 1000u != 0);
}

void phone_overview_calendar(phone_overview_t *out, const ds1302_datetime_t *now,
    const alarm_entry_t alarms[CLOCK_ALARM_COUNT],
    const alarm_snooze_t snoozes[CLOCK_ALARM_COUNT], uint8_t waiting_mask,
    uint64_t uptime) {
    uint64_t wall = 0, best = UINT64_MAX, earliest = UINT64_MAX;
    out->rtc_valid = now && clock_datetime_to_utc(now, &wall);
    out->date[0] = out->next_at[0] = 0;
    out->weekday = out->next_slot = out->snooze_count = out->snooze_slot = 0;
    out->snooze_seconds = 0;
    out->next_snoozed = out->snooze_waiting = false;
    ds1302_datetime_t next = {0};
    if (out->rtc_valid) {
        snprintf(out->date, sizeof(out->date), "%04u-%02u-%02u",
            (unsigned)now->year, (unsigned)now->month, (unsigned)now->day);
        out->weekday = (uint8_t)((wall / 86400u + 3u) % 7u + 1u);
        int slot = alarm_next_occurrence(alarms, now, &next);
        if (slot >= 0 && clock_datetime_to_utc(&next, &best)) out->next_slot = (uint8_t)(slot + 1);
    }
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        bool waiting = (waiting_mask & (1u << i)) != 0;
        if (!snoozes[i].pending && !waiting) continue;
        ++out->snooze_count;
        uint64_t deadline = waiting ? uptime : snoozes[i].deadline_ms;
        if (deadline < earliest || !out->snooze_slot) {
            earliest = deadline;
            out->snooze_slot = (uint8_t)(i + 1);
            uint64_t left = remaining_seconds(deadline, uptime);
            out->snooze_seconds = left > UINT32_MAX ? UINT32_MAX : (uint32_t)left;
            out->snooze_waiting = waiting || deadline <= uptime;
        }
        if (!out->rtc_valid) continue;
        uint64_t left = remaining_seconds(deadline, uptime);
        uint64_t candidate = wall + left;
        if (candidate < best && clock_utc_to_datetime(candidate, &next)) {
            best = candidate;
            out->next_slot = (uint8_t)(i + 1);
            out->next_snoozed = true;
        }
    }
    if (out->next_slot && clock_utc_to_datetime(best, &next))
        snprintf(out->next_at, sizeof(out->next_at), "%04u-%02u-%02uT%02u:%02u:%02u",
            (unsigned)next.year, (unsigned)next.month, (unsigned)next.day,
            (unsigned)next.hour, (unsigned)next.minute, (unsigned)next.second);
    else out->next_slot = 0;
}

// Preserve valid UTF-8. Escape controls/quotes and replace invalid UTF-8 bytes.
static void json_name(char out[151], const char name[25]) {
    size_t used = 0;
    for (size_t i = 0; i < 24u && name[i];) {
        unsigned char c = (unsigned char)name[i];
        if (c == '"' || c == '\\') { out[used++] = '\\'; out[used++] = (char)c; ++i; }
        else if (c < 32u) { used += (size_t)snprintf(out + used, 151u - used, "\\u%04x", (unsigned)c); ++i; }
        else if (c < 128u) { out[used++] = (char)c; ++i; }
        else {
            size_t length = c >= 0xc2u && c <= 0xdfu ? 2u :
                c >= 0xe0u && c <= 0xefu ? 3u : c >= 0xf0u && c <= 0xf4u ? 4u : 0u;
            bool valid = length && i + length <= 24u;
            for (size_t j = 1; valid && j < length; ++j)
                valid = (unsigned char)name[i+j] >= 0x80u && (unsigned char)name[i+j] <= 0xbfu;
            if (valid) {
                unsigned char second = (unsigned char)name[i+1];
                valid = !(c == 0xe0u && second < 0xa0u) && !(c == 0xedu && second >= 0xa0u) &&
                    !(c == 0xf0u && second < 0x90u) && !(c == 0xf4u && second >= 0x90u);
            }
            if (valid) { memcpy(out + used, name + i, length); used += length; i += length; }
            else { memcpy(out + used, "\\ufffd", 6); used += 6; ++i; }
        }
    }
    out[used] = 0;
}

int phone_overview_json(char *buffer, size_t capacity, const phone_overview_t *s) {
    static const char *const bt[] = {"unavailable", "off", "scanning", "waiting", "connected"};
    static const char *const network[] = {"offline", "unconfigured", "suspended", "wait_idle", "connecting", "address", "retry", "ready"};
    char name[151];
    json_name(name, s->bt_name);
    int length = snprintf(buffer, capacity,
        ",\"overview\":{\"rtc_valid\":%s,\"date\":\"%s\",\"weekday\":%u,\"sound_on\":%s,\"timer_sound_id\":%" PRIu32 ","
        "\"alarm\":{\"active\":%u,\"active_id\":%" PRIu32 ",\"snooze_revision\":%" PRIu32 ",\"deferred\":%u,\"next_slot\":%u,\"next_at\":\"%s\",\"next_snoozed\":%s,"
        "\"snoozes\":%u,\"snooze_slot\":%u,\"snooze_seconds\":%" PRIu32 ",\"snooze_waiting\":%s},"
        "\"bluetooth\":{\"state\":\"%s\",\"name\":\"%s\"},"
        "\"network\":{\"mode\":\"%s\",\"state\":\"%s\",\"seconds\":%" PRIu32 "},"
        "\"ntp\":{\"available\":%s,\"configured\":%s,\"sampled\":%s,\"synced\":%s,\"pending\":%s,\"requesting\":%s,\"retry\":%s,\"failed\":%s,"
        "\"sample_age\":%" PRIu32 ",\"sync_age\":%" PRIu32 ",\"offset\":%d}}",
        s->rtc_valid ? "true" : "false", s->date, (unsigned)s->weekday, s->sound_on ? "true" : "false", s->timer_sound_id,
        (unsigned)s->active_alarm, s->active_id, s->snooze_revision, (unsigned)s->deferred_count, (unsigned)s->next_slot, s->next_at,
        s->next_snoozed ? "true" : "false", (unsigned)s->snooze_count, (unsigned)s->snooze_slot,
        s->snooze_seconds, s->snooze_waiting ? "true" : "false",
        (unsigned)s->bt_state < sizeof(bt)/sizeof(bt[0]) ? bt[s->bt_state] : bt[0], name,
        s->direct ? "direct" : "home", (unsigned)s->network.state < sizeof(network)/sizeof(network[0]) ? network[s->network.state] : network[0], s->network.seconds,
        s->ntp.available ? "true" : "false", s->ntp.configured ? "true" : "false", s->ntp.sampled ? "true" : "false",
        s->ntp.synced ? "true" : "false", s->ntp.pending ? "true" : "false", s->ntp.requesting ? "true" : "false", s->ntp.retry ? "true" : "false",
        s->ntp.failed ? "true" : "false",
        s->ntp.sample_age, s->ntp.sync_age, s->ntp.offset);
    return length < 0 || (size_t)length >= capacity ? -1 : length;
}
