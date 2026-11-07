// Speaking-clock C firmware with protected flash settings (no Bluetooth yet).
// DS1302 RTC supports explicit manual time/date writes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/i2c.h"
#include "ds1302.h"
#include "lcd1602_i2c.h"
#include "countdown.h"
#include "alarm_engine.h"
#include "controls.h"
#include "dfplayer.h"
#include "sd_spi.h"
#include "sd_fat32.h"
#include "mp3_probe.h"
#include "queue.h"
#include "router.h"
#include "speech.h"
#include "settings_store.h"
#ifdef CLOCK_ENABLE_A2DP
#include "pico/cyw43_arch.h"
#include "clock_a2dp_control.h"
int btstack_main(int argc, const char *argv[]);
#endif

#define LCD_SDA_GPIO 0
#define LCD_SCL_GPIO 1
#define DS1302_CLK_GPIO 2
#define DS1302_DIO_GPIO 5
#define DS1302_CE_GPIO 4

static int detect_lcd_address(void) {
    // PCF8574 or PCF8574A: scan only the typical expander address ranges.
    uint8_t byte;
    for (int a = 0x20; a <= 0x27; ++a) {
        if (i2c_read_blocking(i2c0, (uint8_t)a, &byte, 1, false) == 1) return a;
    }
    for (int a = 0x38; a <= 0x3f; ++a) {
        if (i2c_read_blocking(i2c0, (uint8_t)a, &byte, 1, false) == 1) return a;
    }
    return -1;
}

static countdown_t timer;
// Five daily alarms; legacy v1/v2 loads disabled defaults.
// v3 persists alarm entries alongside existing settings.
static alarm_entry_t alarms[CLOCK_ALARM_COUNT];
static alarm_scheduler_t alarm_scheduler;
static bool alarm_boot_seeded;
static int alarm_active_index = -1;
static uint64_t alarm_deadline_ms;
static bool alarm_audio_armed;
#define ALARM_SAFETY_TIMEOUT_MS (15u * 60u * 1000u)
static char command_line[48];
static size_t command_length;
// BOOTSEL is intentionally a two-step, time-limited serial operation.
#define BOOTSEL_CONFIRM_WINDOW_MS 15000u
static bool bootsel_armed;
static uint64_t bootsel_armed_ms;
static bool timer_screen;
static audio_queue_t audio_queue;
static audio_router_t audio_router;
#ifdef CLOCK_ENABLE_A2DP
// Opt-in routing test only: normal time, alarm and timer queue stays LOCAL.
// Remember a single remote track so an A2DP loss can replay it locally.
static bool routed_test_pending;
static uint8_t routed_test_folder, routed_test_track;
// Manual speech routing is intentionally separate from the production queue.
// Normal automatic alarms, timer messages and hourly speech remain LOCAL.
static audio_track_t bt_speech_tracks[2];
static uint8_t bt_speech_count, bt_speech_index;
static bool bt_speech_running, bt_speech_track_started;
// Timer sounds have an independent nonblocking Bluetooth playback owner.
// Persisted in Flash v4; older records default LOCAL.
static bool timer_audio_jbl_enabled;
static audio_track_t bt_timer_tracks[AUDIO_QUEUE_CAPACITY];
static uint8_t bt_timer_count, bt_timer_index;
static bool bt_timer_running, bt_timer_started;
static bool alarm_audio_jbl_enabled; // Persisted in Flash v4; default LOCAL.
static audio_track_t bt_alarm_tracks[AUDIO_QUEUE_CAPACITY];
static uint8_t bt_alarm_count, bt_alarm_index;
static bool bt_alarm_running, bt_alarm_started;
static void bt_alarm_stop(const char *reason);
static void bt_alarm_service(void);
static void bt_alarm_fallback(const char *reason);
static bool bt_alarm_prepare(const audio_queue_t *plan);
static void bt_timer_cancel(const char *reason);
static void bt_timer_service(void);
static void bt_timer_fallback(const char *reason);
static bool bt_timer_prepare(const audio_queue_t *plan, const char *event);
static void bt_speech_cancel(const char *reason);

static bool auto_voice_jbl_enabled; // Flash v4 preference; default LOCAL for old records.
static bool bt_speech_start(uint8_t hour, uint8_t minute, bool automatic);
#endif
// In BT-free builds, preserve saved v4 choices opaquely on subsequent
// Flash writes rather than silently overwriting them with LOCAL.
#ifndef CLOCK_ENABLE_A2DP
static uint8_t saved_audio_manual, saved_audio_hourly;
static uint8_t saved_audio_timer, saved_audio_alarm;
#endif
static uint8_t volume = 10; // Default; overridden by saved flash settings.
static bool sound_enabled = true;
static speech_language_t language = SPEECH_RU;
// Two-level settings menu, following the MicroPython navigation model.
typedef enum {
    MENU_CLOSED = 0,
    MENU_LIST,
    MENU_EDIT_LANGUAGE,
    MENU_EDIT_TIME_H,
    MENU_EDIT_TIME_M,
    MENU_EDIT_TIME_S,
    MENU_EDIT_DATE_D,
    MENU_EDIT_DATE_M,
    MENU_EDIT_DATE_Y,
    MENU_EDIT_VOLUME,
    MENU_EDIT_QUIET_ENABLED,
    MENU_EDIT_QUIET_START,
    MENU_EDIT_QUIET_END,
    MENU_EDIT_HALF_HOUR,
    MENU_EDIT_EXACT
} menu_state_t;
static menu_state_t menu_state;
static uint8_t menu_index;
static speech_language_t menu_language_choice;
static uint8_t menu_volume_choice;
static bool menu_exact_choice;
static bool menu_quiet_enabled_choice;
static uint8_t menu_quiet_start_choice;
static uint8_t menu_quiet_end_choice;
static bool menu_half_hour_choice;
static ds1302_datetime_t menu_rtc_choice;
static uint64_t menu_last_input_ms;
#define SETTINGS_MENU_TIMEOUT_MS 20000u
#define SETTINGS_MENU_ITEMS 7u
// Alarm editor is separate from settings; changes stay in draft until DONE.
typedef enum {
    ALARM_UI_CLOSED = 0,
    ALARM_UI_LIST,
    ALARM_UI_ENABLED,
    ALARM_UI_HOUR,
    ALARM_UI_MINUTE,
    ALARM_UI_SOUND,
    ALARM_UI_TRACK
} alarm_ui_state_t;
static alarm_ui_state_t alarm_ui_state;
static uint8_t alarm_ui_index;
static alarm_entry_t alarm_ui_draft;
static uint64_t alarm_ui_last_input_ms;
static bool alarm_preview_active;
#define ALARM_LONG_PRESS_MS 900u
static bool alarm_button_pressed;
static bool alarm_button_consumed;
static uint64_t alarm_button_pressed_at_ms;
// Standalone music player is RAM-only; no flash schema changes.
static bool music_ui_open;
static bool music_ui_playing;
static uint8_t music_ui_track = 1u;
static uint8_t music_track_count = 45u; // Used only if DFPlayer folder count query fails.
static uint8_t music_shuffle[255];
static unsigned music_shuffle_size;
static unsigned music_shuffle_next;
static uint8_t music_recent[7];
static unsigned music_recent_size;
static bool music_random_seeded;
// Preview starts only by SETTINGS in TRACK field (not on each encoder tick).
// This keeps rapid track selection silent and never changes saved config.
static bool exact_hour_phrase;
static bool quiet_enabled = true;
static uint8_t quiet_start = 22;
static uint8_t quiet_end = 7;
static bool half_hour_enabled;
static uint8_t clock_mode; // 0=VOICE, 1=CHIME.
static uint64_t mode_pressed_at_ms;
static bool mode_press_active;
static uint64_t speech_overlay_until_ms;
static char speech_overlay_time[17];
static uint64_t volume_overlay_until_ms;
static uint64_t mode_overlay_until_ms;
#define VOLUME_OVERLAY_MS 1800u

static uint64_t finished_screen_until_ms;

#define AUTOSAVE_DEBOUNCE_MS 5000u
#define AUTOSAVE_MIN_WRITE_INTERVAL_MS 60000u
#define AUTOSAVE_ERROR_RETRY_MS 60000u

static clock_settings_t last_persisted_settings;
static bool autosave_pending;
static uint64_t last_settings_change_ms;
static bool flash_written_this_boot;
static uint64_t last_flash_write_ms;
static bool autosave_attempted;
static uint64_t last_autosave_attempt_ms;

// Automatic clock events are enabled on boot, as in MicroPython v3.
// 'auto off' temporarily disables them until next boot or 'auto on'.
// This runtime switch is deliberately not persisted in flash.
static bool auto_enabled = true;
// Consume the first observed RTC boundary on boot. Otherwise a reboot
// during second 0..5 of :00/:30 could replay an old automatic sound.
static bool auto_boot_seeded;
static bool auto_seen_key_valid;
static uint64_t auto_seen_minute_key;
static bool auto_last_observed_valid;
static uint64_t auto_last_observed_minute_key;
// Diagnostic only: emit at most one duplicate-key note per boundary minute.
static bool auto_duplicate_logged_valid;
static uint64_t auto_duplicate_logged_key;
static uint32_t auto_played, auto_skipped;

typedef enum {
    AUTO_NONE,
    AUTO_HOUR,
    AUTO_CHIME,
    AUTO_HALF,
    AUTO_DISABLED,
    AUTO_QUIET,
    AUTO_SOUND_OFF,
    AUTO_TIMER,
    AUTO_MENU,
    AUTO_BUSY,
    AUTO_LATE
} auto_decision_t;

static const char *auto_reason(auto_decision_t d) {
    switch (d) {
        case AUTO_HOUR: return "HOURLY_VOICE";
        case AUTO_CHIME: return "HOURLY_CHIME";
        case AUTO_HALF: return "HALF_HOUR_BEEP";
        case AUTO_DISABLED: return "AUTO_DISABLED";
        case AUTO_QUIET: return "QUIET_HOURS";
        case AUTO_SOUND_OFF: return "SOUND_OFF";
        case AUTO_TIMER: return "TIMER_PRIORITY";
        case AUTO_MENU: return "MENU_OPEN";
        case AUTO_BUSY: return "AUDIO_BUSY";
        case AUTO_LATE: return "MISSED_WINDOW";
        default: return "NO_EVENT";
    }
}

// Use year, month, day and hour-minute; does not depend on milliseconds,
// and does not reset on RTC errors. A given minute is processed once.
static uint64_t auto_minute_key(const ds1302_datetime_t *dt) {
    return (((((uint64_t)dt->year * 13u + dt->month) * 32u + dt->day) *
               24u + dt->hour) * 60u + dt->minute);
}

static bool auto_quiet_at(uint8_t hour) {
    if (!quiet_enabled) return false;
    if (quiet_start < quiet_end) return hour >= quiet_start && hour < quiet_end;
    // Equal start/end means quiet all day (matches source/v3/main.py).
    return hour >= quiet_start || hour < quiet_end;
}

static auto_decision_t auto_evaluate(uint8_t hour, uint8_t minute,
                                      uint8_t second, bool require_enabled) {
    if (minute != 0u && minute != 30u) return AUTO_NONE;
    if (minute == 30u && !half_hour_enabled) return AUTO_NONE;
    if (require_enabled && !auto_enabled) return AUTO_DISABLED;
    if (!sound_enabled) return AUTO_SOUND_OFF;
    if (auto_quiet_at(hour)) return AUTO_QUIET;
    if (alarm_active_index >= 0) return AUTO_BUSY;
    if (timer.running ||
        (finished_screen_until_ms &&
         to_ms_since_boot(get_absolute_time()) < finished_screen_until_ms))
        return AUTO_TIMER;
    if (menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open)
        return AUTO_MENU;
    if (!audio_queue_idle(&audio_queue) || dfplayer_busy()) return AUTO_BUSY;
#ifdef CLOCK_ENABLE_A2DP
    // Never begin an automatic announcement in the middle of a BT phrase.
    if (bt_speech_running || bt_timer_running || bt_alarm_running || mp3_play_active() ||
        mp3_probe_active() || routed_test_pending ||
        sd_fat32_stream_active()) return AUTO_BUSY;
#endif
    // If boot/RTC recovers in the middle of an hour/half-hour minute,
    // do not announce stale time. A 5-second window is deliberate.
    if (second > 5u) return AUTO_LATE;
    return minute == 0u ? (clock_mode ? AUTO_CHIME : AUTO_HOUR) : AUTO_HALF;
}

static void auto_status(void) {
    printf("[AUTO] enabled=%s quiet=%s %02u-%02u half_hour=%s sound=%s played=%lu skipped=%lu\n",
           auto_enabled ? "ON" : "OFF", quiet_enabled ? "ON" : "OFF",
           (unsigned)quiet_start, (unsigned)quiet_end,
           half_hour_enabled ? "ON" : "OFF", sound_enabled ? "ON" : "OFF",
           (unsigned long)auto_played, (unsigned long)auto_skipped);
    printf("[AUTO] clock_mode=%s (folder 18, 1..12 strikes)\n",
           clock_mode ? "CHIME" : "VOICE");
#ifdef CLOCK_ENABLE_A2DP
    printf("[AUTO] voice_output=%s (Flash v4; chime/half stay LOCAL)\n",
           auto_voice_jbl_enabled ? "JBL_PREFERRED" : "LOCAL");
#endif
}

// Read-only snapshot: useful when the RTC is manually adjusted for tests.
static void auto_debug(void) {
    ds1302_datetime_t dt = {0};
    bool rtc_ok = ds1302_read_datetime(&dt);
    printf("[AUTO-DEBUG] enabled=%s seeded=%s seen=%s last_key=%llu duplicate_logged=%s played=%lu skipped=%lu\n",
           auto_enabled ? "ON" : "OFF",
           auto_boot_seeded ? "YES" : "NO",
           auto_seen_key_valid ? "YES" : "NO",
           (unsigned long long)(auto_seen_key_valid ? auto_seen_minute_key : 0u),
           auto_duplicate_logged_valid ? "YES" : "NO",
           (unsigned long)auto_played, (unsigned long)auto_skipped);
    printf("[AUTO-DEBUG] timer=%s menu=%s sound=%s quiet=%s half_hour=%s audio_idle=%s busy=%s\n",
           timer.running ? "RUNNING" : "IDLE",
           menu_state == MENU_CLOSED ? "CLOSED" : "OPEN",
           sound_enabled ? "ON" : "OFF",
           rtc_ok && auto_quiet_at(dt.hour) ? "NOW" : "NO",
           half_hour_enabled ? "ON" : "OFF",
           audio_queue_idle(&audio_queue) ? "YES" : "NO",
           dfplayer_busy() ? "YES" : "NO");
    if (rtc_ok) {
        uint64_t key = auto_minute_key(&dt);
        printf("[AUTO-DEBUG] rtc=%02u:%02u:%02u current_key=%llu same_as_last=%s\n",
               (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second,
               (unsigned long long)key,
               auto_seen_key_valid && auto_seen_minute_key == key ? "YES" : "NO");
    } else {
        puts("[AUTO-DEBUG] RTC read failed; no state altered");
    }
}

static void auto_test(uint8_t hour, uint8_t minute) {
    auto_decision_t d = auto_evaluate(hour, minute, 0u, false);
    printf("[AUTO-TEST] %02u:%02u => %s (simulation: no audio, no RTC write, auto gate ignored)\n",
           (unsigned)hour, (unsigned)minute, auto_reason(d));
    if (d == AUTO_CHIME) {
        unsigned strikes = (unsigned)(hour % 12u);
        if (!strikes) strikes = 12u;
        printf("[AUTO-TEST] CHIME would queue folder=18 track=%03u (%u strikes)\n",
               strikes, strikes);
    } else if (d == AUTO_HALF) {
        puts("[AUTO-TEST] HALF would queue folder=07 track=015");
    }
}

static void auto_service_rtc(const ds1302_datetime_t *dt) {
    if (!dt) return;
    uint64_t observed_key = auto_minute_key(dt);
    bool reentered_minute = auto_last_observed_valid &&
                             auto_last_observed_minute_key != observed_key;
    auto_last_observed_valid = true;
    auto_last_observed_minute_key = observed_key;
    // The first valid RTC reading establishes the current boundary without
    // announcing it. Fresh 00/30 boundaries encountered later play normally.
    if (!auto_boot_seeded) {
        auto_boot_seeded = true;
        if (dt->minute == 0u || dt->minute == 30u) {
            auto_seen_key_valid = true;
            auto_seen_minute_key = auto_minute_key(dt);
            printf("[AUTO] boot: current %02u:%02u boundary consumed (no replay)\n",
                   (unsigned)dt->hour, (unsigned)dt->minute);
        } else {
            puts("[AUTO] boot: scheduler armed for next boundary");
        }
        return;
    }
    if (dt->minute != 0u && dt->minute != 30u) return;
    uint64_t key = auto_minute_key(dt);
    if (auto_seen_key_valid && auto_seen_minute_key == key) {
        // Normally silent deduplication. Report only on a real RTC
        // transition BACK into an already-processed boundary minute,
        // not on the ordinary subsequent seconds of the same minute.
        if (reentered_minute &&
            (!auto_duplicate_logged_valid || auto_duplicate_logged_key != key)) {
            auto_duplicate_logged_valid = true;
            auto_duplicate_logged_key = key;
            printf("[AUTO-DEBUG] %02u:%02u suppressed=ALREADY_PROCESSED key=%llu\n",
                   (unsigned)dt->hour, (unsigned)dt->minute,
                   (unsigned long long)key);
        }
        return;
    }
    auto_seen_minute_key = key;
    auto_seen_key_valid = true;
    auto_decision_t d = auto_evaluate(dt->hour, dt->minute, dt->second, true);
    if (d != AUTO_HOUR && d != AUTO_CHIME && d != AUTO_HALF) {
        ++auto_skipped;
        printf("[AUTO] %02u:%02u skipped=%s\n",
               (unsigned)dt->hour, (unsigned)dt->minute, auto_reason(d));
        return;
    }
    bool queued;
    if (d == AUTO_HALF) {
        queued = audio_queue_enqueue(&audio_queue, 7u, 15u);
    } else if (d == AUTO_CHIME) {
        uint8_t strikes = (uint8_t)(dt->hour % 12u);
        if (strikes == 0u) strikes = 12u;
        queued = audio_queue_enqueue(&audio_queue, 18u, strikes);
    } else {
#ifdef CLOCK_ENABLE_A2DP
        // Only hourly VOICE announcements may use JBL, and only after
        // explicit auto-audio jbl opt-in. Keep chime/half-hour LOCAL.
        if (auto_voice_jbl_enabled && clock_a2dp_stream_active() &&
            clock_a2dp_current_rate() == 44100u) {
            queued = bt_speech_start(dt->hour, 0u, true);
            if (queued)
                puts("[AUTO-AUDIO] HOURLY_VOICE routed=JBL");
            else
                puts("[AUTO-AUDIO] JBL start failed; attempting LOCAL");
        } else {
            queued = false;
            if (auto_voice_jbl_enabled)
                puts("[AUTO-AUDIO] JBL unavailable; hourly voice LOCAL");
        }
        if (!queued)
#endif
            queued = speech_enqueue_time(&audio_queue, language, dt->hour, 0u, exact_hour_phrase);
    }
    if (queued) {
        ++auto_played;
        printf("[AUTO] %02u:%02u queued=%s\n",
               (unsigned)dt->hour, (unsigned)dt->minute, auto_reason(d));
    } else {
        ++auto_skipped;
        puts("[AUTO] skipped=QUEUE_FULL");
    }
}



static void print_timer_status(uint64_t now_ms) {
    uint32_t remaining = countdown_remaining(&timer, now_ms);
    printf("[TIMER] %s duration=%lus remaining=%lus\n",
           timer.running ? "RUNNING" : "STOPPED",
           (unsigned long)timer.duration_seconds, (unsigned long)remaining);
}

static void timer_audio_start(void) {
#ifdef CLOCK_ENABLE_A2DP
    bt_timer_cancel("TIMER_RESTART");
    // Timer starting overrides any optional manual/hourly BT speech.
    if (bt_speech_running) bt_speech_cancel("TIMER_PRIORITY");
    if (routed_test_pending && mp3_play_active()) {
        mp3_play_stop();
        routed_test_pending = false;
    }
#endif
    // Timer owns the local sound queue even when its own output is JBL.
    audio_queue_clear(&audio_queue, true);
    if (!sound_enabled) return;
#ifdef CLOCK_ENABLE_A2DP
    audio_queue_t plan = {0};
    speech_enqueue_timer_start(&plan, language, timer.duration_seconds);
    if (bt_timer_prepare(&plan, "START")) return;
#endif
    speech_enqueue_timer_start(&audio_queue, language, timer.duration_seconds);
}

static void timer_audio_cancel(void) {
#ifdef CLOCK_ENABLE_A2DP
    bt_timer_cancel("TIMER_CANCEL");
#endif
    audio_queue_clear(&audio_queue, true);
    if (!sound_enabled) return;
#ifdef CLOCK_ENABLE_A2DP
    audio_queue_t plan = {0};
    speech_enqueue_timer_cancel(&plan, language);
    if (bt_timer_prepare(&plan, "CANCEL")) return;
#endif
    speech_enqueue_timer_cancel(&audio_queue, language);
}

static void timer_audio_finished(void) {
#ifdef CLOCK_ENABLE_A2DP
    bt_timer_cancel("TIMER_FINISH_PRIORITY");
#endif
    audio_queue_clear(&audio_queue, true);
    if (!sound_enabled) return;
#ifdef CLOCK_ENABLE_A2DP
    audio_queue_t plan = {0};
    speech_enqueue_timer_finish(&plan, language);
    if (bt_timer_prepare(&plan, "FINISH")) return;
#endif
    speech_enqueue_timer_finish(&audio_queue, language);
}

#ifdef CLOCK_ENABLE_A2DP
// Timer event audio uses the original speech planners unchanged; no timer
// event ever depends solely on Bluetooth, since any failure requeues locally.
// Shared MP3 sequence executor. Owners retain priority/fallback decisions.
// Returns WAIT, STARTED, COMPLETE or FAILED; all disk/decoder work is in
// the cooperative main loop, never in the A2DP callback.
typedef enum {
    BT_SEQUENCE_WAIT = 0,
    BT_SEQUENCE_STARTED,
    BT_SEQUENCE_COMPLETE,
    BT_SEQUENCE_FAILED
} bt_sequence_result_t;
static bt_sequence_result_t bt_sequence_step(const audio_track_t *tracks,
                                               uint8_t count, uint8_t *index,
                                               bool *track_started,
                                               const char *log_tag) {
    if (*track_started) {
        if (mp3_play_active()) return BT_SEQUENCE_WAIT;
        ++*index;
        *track_started = false;
    }
    if (*index >= count) return BT_SEQUENCE_COMPLETE;
    audio_track_t track = tracks[*index];
    char path[20];
    snprintf(path, sizeof(path), "%02u/%03u.mp3",
             (unsigned)track.folder, (unsigned)track.track);
    if (!mp3_play_start(path, clock_a2dp_current_rate()))
        return BT_SEQUENCE_FAILED;
    *track_started = true;
    printf("[%s] SEGMENT %u/%u %s\n",
           log_tag, (unsigned)(*index + 1u), (unsigned)count, path);
    return BT_SEQUENCE_STARTED;
}
static void bt_timer_cancel(const char *reason) {
    if (!bt_timer_running) return;
    if (bt_timer_started && mp3_play_active()) mp3_play_stop();
    bt_timer_running = false;
    bt_timer_started = false;
    printf("[BT-TIMER] STOP reason=%s\n", reason);
}
static bool bt_timer_prepare(const audio_queue_t *plan, const char *event) {
    if (!timer_audio_jbl_enabled || !sound_enabled || !plan ||
        plan->size == 0u || plan->size > AUDIO_QUEUE_CAPACITY ||
        !clock_a2dp_stream_active() || clock_a2dp_current_rate() != 44100u ||
        alarm_active_index >= 0 || music_ui_open ||
        mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active() ||
        bt_speech_running || routed_test_pending || bt_timer_running) {
        if (timer_audio_jbl_enabled)
            printf("[BT-TIMER] %s output=LOCAL (BT unavailable or audio busy)\n", event);
        return false;
    }
    for (unsigned i = 0; i < plan->size; ++i)
        bt_timer_tracks[i] = plan->items[(plan->head + i) % AUDIO_QUEUE_CAPACITY];
    bt_timer_count = plan->size;
    bt_timer_index = 0u;
    bt_timer_started = false;
    bt_timer_running = true;
    printf("[BT-TIMER] %s output=JBL segments=%u gain=53.3%%\n",
           event, (unsigned)bt_timer_count);
    return true;
}
static void bt_timer_fallback(const char *reason) {
    if (!bt_timer_running) return;
    if (bt_timer_started && mp3_play_active()) mp3_play_stop();
    // Alarm always takes priority over a superseded timer message.
    if (alarm_active_index >= 0 || !sound_enabled) {
        printf("[BT-TIMER] LOCAL fallback suppressed by alarm/mute: %s\n", reason);
        bt_timer_running = false;
        bt_timer_started = false;
        return;
    }
    unsigned queued = 0u;
    for (unsigned i = bt_timer_index; i < bt_timer_count; ++i) {
        audio_track_t t = bt_timer_tracks[i];
        if (!audio_queue_enqueue(&audio_queue, t.folder, t.track)) break;
        ++queued;
    }
    bt_timer_running = false;
    bt_timer_started = false;
    printf("[BT-TIMER] LOCAL fallback reason=%s queued=%u\n",
           reason, queued);
}
static void bt_timer_service(void) {
    if (!bt_timer_running) return;
    if (alarm_active_index >= 0 || !sound_enabled || music_ui_open) {
        bt_timer_cancel("HIGHER_PRIORITY");
        return;
    }
    if (!clock_a2dp_stream_active() || clock_a2dp_current_rate() != 44100u) {
        bt_timer_fallback("A2DP_UNAVAILABLE");
        return;
    }
    // Allow the UART pause command (sent when a timer event superseded the
    // previous local voice) to complete before starting another decoder.
    if (audio_queue.pause_pending || dfplayer_busy()) return;
    if (audio_queue.size || audio_queue.track_started) {
        bt_timer_fallback("LOCAL_AUDIO_PRIORITY");
        return;
    }
    bt_sequence_result_t step = bt_sequence_step(bt_timer_tracks, bt_timer_count,
                                                  &bt_timer_index, &bt_timer_started,
                                                  "BT-TIMER");
    if (step == BT_SEQUENCE_COMPLETE) {
        bt_timer_running = false;
        puts("[BT-TIMER] COMPLETE");
    } else if (step == BT_SEQUENCE_FAILED) {
        bt_timer_fallback("MP3_OPEN_FAILED");
    }
}
static void bt_speech_cancel(const char *reason) {
    if (!bt_speech_running) return;
    if (bt_speech_track_started && mp3_play_active()) mp3_play_stop();
    bt_speech_running = false;
    bt_speech_track_started = false;
    printf("[BT-SPEECH] Cancelled: %s\n", reason);
}
// Only use local fallback when no higher-priority audio owner is present.
// Replay the interrupted phrase from the beginning, then each remaining phrase.
static void bt_speech_fallback(const char *reason) {
    if (!bt_speech_running) return;
    if (bt_speech_track_started && mp3_play_active()) mp3_play_stop();
    bool safe = alarm_active_index < 0 && !timer.running && !music_ui_open &&
                sound_enabled && audio_queue_idle(&audio_queue) && !dfplayer_busy();
    if (!safe) {
        bt_speech_running = false;
        bt_speech_track_started = false;
        printf("[BT-SPEECH] Local fallback suppressed: %s; higher priority audio active\n",
               reason);
        return;
    }
    unsigned queued = 0u;
    for (unsigned i = bt_speech_index; i < bt_speech_count; ++i) {
        if (!audio_queue_enqueue(&audio_queue, bt_speech_tracks[i].folder,
                                 bt_speech_tracks[i].track)) break;
        ++queued;
    }
    bt_speech_running = false;
    bt_speech_track_started = false;
    printf("[BT-SPEECH] LOCAL fallback reason=%s queued=%u\n", reason, queued);
}
static bool bt_speech_start(uint8_t hour, uint8_t minute, bool automatic) {
    if (bt_speech_running || routed_test_pending || mp3_play_active() ||
        mp3_probe_active() || sd_fat32_stream_active() ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy() ||
        timer.running || alarm_active_index >= 0 || music_ui_open) {
        puts("[BT-SPEECH] Busy; manual speech refused");
        return false;
    }
    // Use original speech planner so RU/DE and exact-hour mappings agree.
    audio_queue_t scratch = {0};
    if (!speech_enqueue_time(&scratch, language, hour, minute, exact_hour_phrase) ||
        scratch.size == 0u || scratch.size > 2u) {
        puts("[BT-SPEECH] Failed to build speech sequence");
        return false;
    }
    for (unsigned i = 0; i < scratch.size; ++i)
        bt_speech_tracks[i] = scratch.items[(scratch.head + i) % AUDIO_QUEUE_CAPACITY];
    bt_speech_count = scratch.size;
    bt_speech_index = 0u;
    bt_speech_track_started = false;
    bt_speech_running = true;
    printf("[BT-SPEECH] START %02u:%02u segments=%u mode=%s gain=53.3%%\n",
           (unsigned)hour, (unsigned)minute, (unsigned)bt_speech_count,
           automatic ? "AUTO_HOUR" : "MANUAL");
    return true;
}
static void bt_speech_service(void) {
    if (!bt_speech_running) return;
    if (timer.running || alarm_active_index >= 0 || music_ui_open ||
        !sound_enabled || !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        bt_speech_cancel("HIGHER_PRIORITY");
        return;
    }
    if (!clock_a2dp_stream_active() || clock_a2dp_current_rate() != 44100u) {
        bt_speech_fallback("A2DP_UNAVAILABLE");
        return;
    }
    bt_sequence_result_t step = bt_sequence_step(bt_speech_tracks, bt_speech_count,
                                                  &bt_speech_index, &bt_speech_track_started,
                                                  "BT-SPEECH");
    if (step == BT_SEQUENCE_COMPLETE) {
        bt_speech_running = false;
        puts("[BT-SPEECH] COMPLETE, all segments delivered");
    } else if (step == BT_SEQUENCE_FAILED) {
        bt_speech_fallback("MP3_OPEN_FAILED");
    }
}
#endif

// Use the same hour/minute file mapping as MicroPython enqueue_clock_time.
// Never announce automatically: only on user command or MODE long press.
static void announce_time(uint8_t hour, uint8_t minute) {
    if (alarm_active_index >= 0) {
        puts("[SPEECH] blocked: alarm ringing");
        return;
    }
    if (!sound_enabled) {
        puts("[SPEECH] skipped: sound OFF");
        return;
    }
    if (hour > 23u || minute > 59u) {
        puts("[SPEECH] invalid time");
        return;
    }
#ifdef CLOCK_ENABLE_A2DP
    // Only manually requested speech uses the selected Bluetooth output.
    // Automatic clock announcements and alarms retain their DFPlayer path.
    bool use_jbl = audio_router.requested == AUDIO_OUTPUT_BT_JBL;
    if (use_jbl) {
        audio_router_bluetooth_state(&audio_router, clock_a2dp_stream_active() &&
                                     clock_a2dp_current_rate() == 44100u);
        if (audio_router.effective == AUDIO_OUTPUT_BT_JBL) {
            if (!bt_speech_start(hour, minute, false)) return;
        } else {
            puts("[BT-SPEECH] JBL not ready; announcing via LOCAL");
            audio_queue_clear(&audio_queue, true);
            if (!speech_enqueue_time(&audio_queue, language, hour, minute,
                                     exact_hour_phrase)) return;
        }
    } else
#endif
    {
        audio_queue_clear(&audio_queue, true);
        if (!speech_enqueue_time(&audio_queue, language, hour, minute, exact_hour_phrase)) {
            puts("[SPEECH] failed to queue time");
            return;
        }
    }
    snprintf(speech_overlay_time, sizeof(speech_overlay_time), "%02u:%02u",
             (unsigned)hour, (unsigned)minute);
    speech_overlay_until_ms = to_ms_since_boot(get_absolute_time()) + 2000u;
    printf("[SPEECH] language=%s time=%s exact_hour=%s\n",
           speech_language_name(language), speech_overlay_time,
           exact_hour_phrase ? "ON" : "OFF");
}

static void announce_rtc_time(void) {
    ds1302_datetime_t dt = {0};
    if (!ds1302_read_datetime(&dt)) {
        puts("[SPEECH] RTC unavailable; try say HH MM");
        return;
    }
    announce_time(dt.hour, dt.minute);
}

static void timer_start_now(uint64_t now) {
    if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open) {
        puts("[TIMER] start blocked: close UI or stop alarm first");
        return;
    }
    countdown_start(&timer, now);
    timer_screen = true;
    finished_screen_until_ms = 0;
    timer_audio_start();
}

static void timer_cancel_now(void) {
    countdown_cancel(&timer);
    timer_screen = false;
    finished_screen_until_ms = 0;
    timer_audio_cancel();
}


static clock_settings_t settings_snapshot(void) {
    clock_settings_t cfg = {
        .language = (uint8_t)language,
        .volume = volume,
        .exact_hour = exact_hour_phrase,
        .quiet_enabled = quiet_enabled,
        .quiet_start = quiet_start,
        .quiet_end = quiet_end,
        .half_hour_enabled = half_hour_enabled,
        .clock_mode = clock_mode,
#ifdef CLOCK_ENABLE_A2DP
        .audio_manual_jbl = audio_router.requested == AUDIO_OUTPUT_BT_JBL ? 1u : 0u,
        .audio_hourly_jbl = auto_voice_jbl_enabled ? 1u : 0u,
        .audio_timer_jbl = timer_audio_jbl_enabled ? 1u : 0u,
        .audio_alarm_jbl = alarm_audio_jbl_enabled ? 1u : 0u
#else
        .audio_manual_jbl = saved_audio_manual,
        .audio_hourly_jbl = saved_audio_hourly,
        .audio_timer_jbl = saved_audio_timer,
        .audio_alarm_jbl = saved_audio_alarm
#endif
    };
    memcpy(cfg.alarms, alarms, sizeof(cfg.alarms));
    return cfg;
}

static bool settings_equal(clock_settings_t a, clock_settings_t b) {
    // Compare fields explicitly, not struct padding.
    if (a.language != b.language || a.volume != b.volume ||
        a.exact_hour != b.exact_hour || a.quiet_enabled != b.quiet_enabled ||
        a.quiet_start != b.quiet_start || a.quiet_end != b.quiet_end ||
        a.half_hour_enabled != b.half_hour_enabled ||
        a.clock_mode != b.clock_mode ||
        a.audio_manual_jbl != b.audio_manual_jbl ||
        a.audio_hourly_jbl != b.audio_hourly_jbl ||
        a.audio_timer_jbl != b.audio_timer_jbl ||
        a.audio_alarm_jbl != b.audio_alarm_jbl) return false;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *x = &a.alarms[i], *y = &b.alarms[i];
        if (x->enabled != y->enabled || x->hour != y->hour ||
            x->minute != y->minute || x->sound != y->sound ||
            x->track != y->track) return false;
    }
    return true;
}

static void settings_mark_changed(void) {
    clock_settings_t current = settings_snapshot();
    autosave_pending = !settings_equal(current, last_persisted_settings);
    last_settings_change_ms = to_ms_since_boot(get_absolute_time());
    if (autosave_pending) {
        puts("[FLASH] settings changed; autosave pending after 5s");
    } else {
        puts("[FLASH] settings match saved values; no write needed");
    }
}

static void autosave_service(uint64_t now_ms) {
    if (!autosave_pending) return;
    if (now_ms - last_settings_change_ms < AUTOSAVE_DEBOUNCE_MS) return;
    if (flash_written_this_boot &&
        now_ms - last_flash_write_ms < AUTOSAVE_MIN_WRITE_INTERVAL_MS) return;
    if (autosave_attempted &&
        now_ms - last_autosave_attempt_ms < AUTOSAVE_ERROR_RETRY_MS) return;
    // Handle timer FINISHED first. Block flash through the final display
    // window, queued audio and physical DFPlayer BUSY (even for clones).
    if (timer.running || alarm_active_index >= 0 || menu_state != MENU_CLOSED ||
        alarm_ui_state != ALARM_UI_CLOSED || music_ui_open ||
        (finished_screen_until_ms && now_ms < finished_screen_until_ms) ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy()
#ifdef CLOCK_ENABLE_A2DP
        || bt_speech_running || bt_timer_running || bt_alarm_running ||
        routed_test_pending || mp3_play_active() || sd_fat32_stream_active()
#endif
       ) return;

    clock_settings_t cfg = settings_snapshot();
    if (settings_equal(cfg, last_persisted_settings)) {
        autosave_pending = false;
        return;
    }
    autosave_attempted = true;
    last_autosave_attempt_ms = now_ms;
    settings_store_result_t result = settings_store_save(&cfg);
    printf("[FLASH] autosave=%s lang=%s volume=%u exact=%s\n",
           settings_store_result_name(result),
           cfg.language ? "de" : "ru",
           (unsigned)cfg.volume, cfg.exact_hour ? "ON" : "OFF");
    if (result == SETTINGS_STORE_OK) {
        last_persisted_settings = cfg;
        autosave_pending = false;
        flash_written_this_boot = true;
        last_flash_write_ms = to_ms_since_boot(get_absolute_time());
        autosave_attempted = false;
    } else {
        puts("[FLASH] autosave failed; retry no sooner than 60s");
    }
}

static void print_autosave_status(uint64_t now_ms) {
    clock_settings_t cfg = settings_snapshot();
    uint64_t debounce = autosave_pending &&
        now_ms - last_settings_change_ms < AUTOSAVE_DEBOUNCE_MS
        ? AUTOSAVE_DEBOUNCE_MS - (now_ms - last_settings_change_ms) : 0u;
    uint64_t cooldown = flash_written_this_boot &&
        now_ms - last_flash_write_ms < AUTOSAVE_MIN_WRITE_INTERVAL_MS
        ? AUTOSAVE_MIN_WRITE_INTERVAL_MS - (now_ms - last_flash_write_ms) : 0u;
    uint64_t retry = autosave_attempted &&
        now_ms - last_autosave_attempt_ms < AUTOSAVE_ERROR_RETRY_MS
        ? AUTOSAVE_ERROR_RETRY_MS - (now_ms - last_autosave_attempt_ms) : 0u;
    printf("[FLASH] autosave pending=%s current=%s,%u,%s debounce=%lums cooldown=%lums retry=%lums\n",
           autosave_pending ? "yes" : "no", cfg.language ? "de" : "ru",
           (unsigned)cfg.volume, cfg.exact_hour ? "ON" : "OFF",
           (unsigned long)debounce, (unsigned long)cooldown, (unsigned long)retry);
}

static void set_clock_mode(uint8_t requested, uint64_t now) {
    if (requested > 1u) return;
    if (timer.running || alarm_active_index >= 0 || menu_state != MENU_CLOSED ||
        alarm_ui_state != ALARM_UI_CLOSED) {
        puts("[MODE] Change blocked: timer/alarm active or menu open");
        return;
    }
    if (clock_mode != requested) {
        clock_mode = requested;
        settings_mark_changed();
    }
    mode_overlay_until_ms = now + 1800u;
    printf("[MODE] clock_mode=%s\n", clock_mode ? "CHIME" : "VOICE");
}

static void change_volume(int direction) {
    int requested = (int)volume + direction;
    if (requested < 0) requested = 0;
    if (requested > 30) requested = 30;
    if (requested == volume) return;
    volume = (uint8_t)requested;
    settings_mark_changed();
    audio_queue_set_volume(&audio_queue, volume);
    volume_overlay_until_ms = to_ms_since_boot(get_absolute_time()) + VOLUME_OVERLAY_MS;
    printf("[AUDIO] volume=%u/30 sound=%s\n", (unsigned)volume,
           sound_enabled ? "ON" : "OFF");
}

static void toggle_sound(void) {
    sound_enabled = !sound_enabled;
    volume_overlay_until_ms = to_ms_since_boot(get_absolute_time()) + VOLUME_OVERLAY_MS;
    if (!sound_enabled) {
        audio_queue_clear(&audio_queue, true);
        if (music_ui_open) music_ui_playing = false;
    }
    printf("[AUDIO] sound=%s\n", sound_enabled ? "ON" : "OFF");
}

static uint8_t menu_month_days(uint16_t y, uint8_t m) {
    static const uint8_t md[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m < 1u || m > 12u) return 0u;
    bool leap = y % 4u == 0u && (y % 100u != 0u || y % 400u == 0u);
    return (uint8_t)(md[m - 1u] + (m == 2u && leap));
}

static uint8_t menu_weekday(uint16_t year, uint8_t month, uint8_t day) {
    // Sakamoto, Sunday=0; convert to Monday=1..Sunday=7.
    static const int offsets[12] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int y = year - (month < 3u ? 1 : 0);
    int sun0 = (y + y/4 - y/100 + y/400 +
                offsets[month - 1u] + day) % 7;
    return sun0 == 0 ? 7u : (uint8_t)sun0;
}

static uint8_t wrap_u8(uint8_t v, uint8_t low, uint8_t high, int direction) {
    if (direction > 0) return v >= high ? low : (uint8_t)(v + 1u);
    return v <= low ? high : (uint8_t)(v - 1u);
}

static bool menu_is_time_edit(void) {
    return menu_state >= MENU_EDIT_TIME_H && menu_state <= MENU_EDIT_TIME_S;
}

static bool menu_is_date_edit(void) {
    return menu_state >= MENU_EDIT_DATE_D && menu_state <= MENU_EDIT_DATE_Y;
}

static bool menu_write_rtc(bool date_only) {
    ds1302_datetime_t fresh = {0};
    if (!ds1302_read_datetime(&fresh)) {
        puts("[RTC] Save refused: RTC read invalid; no registers written");
        return false;
    }
    if (date_only) {
        fresh.year = menu_rtc_choice.year;
        fresh.month = menu_rtc_choice.month;
        fresh.day = menu_rtc_choice.day;
        fresh.weekday = menu_weekday(fresh.year, fresh.month, fresh.day);
    } else {
        fresh.hour = menu_rtc_choice.hour;
        fresh.minute = menu_rtc_choice.minute;
        fresh.second = menu_rtc_choice.second;
    }
    if (!ds1302_datetime_valid(&fresh)) {
        puts("[RTC] Save refused: invalid datetime");
        return false;
    }
    if (!ds1302_write_datetime(&fresh)) {
        puts("[RTC] RTC write/readback failed; inspect rtc-debug");
        return false;
    }
    printf("[RTC] %s saved %02u.%02u.%04u %02u:%02u:%02u weekday=%u\n",
           date_only ? "DATE" : "TIME",
           (unsigned)fresh.day, (unsigned)fresh.month, (unsigned)fresh.year,
           (unsigned)fresh.hour, (unsigned)fresh.minute, (unsigned)fresh.second,
           (unsigned)fresh.weekday);
    return true;
}

static void alarm_preview_stop(const char *reason) {
    if (!alarm_preview_active) return;
    audio_queue_clear(&audio_queue, true);
    alarm_preview_active = false;
    printf("[ALARM-PREVIEW] stopped=%s\n", reason);
}

static void alarm_preview_toggle(void) {
    if (alarm_ui_state != ALARM_UI_TRACK) return;
    if (alarm_preview_active) {
        alarm_preview_stop("BUTTON");
        return;
    }
    if (!sound_enabled || alarm_active_index >= 0 ||
        timer.running || !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        puts("[ALARM-PREVIEW] blocked: audio busy, sound off or timer active");
        return;
    }
    if (!audio_queue_enqueue(&audio_queue, 8u, alarm_ui_draft.track)) {
        puts("[ALARM-PREVIEW] failed: audio queue full");
        return;
    }
    alarm_preview_active = true;
    printf("[ALARM-PREVIEW] playing folder=08 track=%03u (SETTINGS=STOP)\n",
           (unsigned)alarm_ui_draft.track);
}

static void alarm_ui_close(const char *reason) {
    if (alarm_ui_state == ALARM_UI_CLOSED) return;
    alarm_preview_stop(reason);
    alarm_ui_state = ALARM_UI_CLOSED;
    timer_screen = false;
    printf("[ALARM-UI] closed=%s\n", reason);
}

// Fisher-Yates; a shuffle bag avoids repeats until every track is played.
// The last seven songs of the previous cycle are postponed on the next cycle.
static void music_shuffle_mix(unsigned first, unsigned end) {
    if (end <= first + 1u) return;
    for (unsigned i = end - 1u; i > first; --i) {
        unsigned j = first + (unsigned)(rand() % (int)(i - first + 1u));
        uint8_t t = music_shuffle[i];
        music_shuffle[i] = music_shuffle[j];
        music_shuffle[j] = t;
    }
}

static bool music_was_recent(uint8_t track) {
    for (unsigned i = 0; i < music_recent_size; ++i)
        if (music_recent[i] == track) return true;
    return false;
}

static void music_shuffle_refill(void) {
    // Generate an entire shuffled playlist, navigable before playback.
    unsigned safe = 0u, delayed = 0u;
    for (unsigned i = 1; i <= music_track_count; ++i) {
        if (!music_was_recent((uint8_t)i))
            music_shuffle[safe++] = (uint8_t)i;
    }
    // Shuffle safe tracks first, then recent tracks to prevent repeats at cycle edge.
    music_shuffle_mix(0u, safe);
    unsigned delayed_start = safe;
    for (unsigned i = 1; i <= music_track_count; ++i) {
        if (music_was_recent((uint8_t)i))
            music_shuffle[safe + delayed++] = (uint8_t)i;
    }
    music_shuffle_mix(delayed_start, safe + delayed);
    music_shuffle_size = safe + delayed;
    music_shuffle_next = 0u;
}

static void music_remember(uint8_t track) {
    if (music_recent_size >= sizeof(music_recent)) {
        memmove(&music_recent[0], &music_recent[1], sizeof(music_recent) - 1u);
        music_recent_size = sizeof(music_recent) - 1u;
    }
    music_recent[music_recent_size++] = track;
}

static void music_ui_stop(const char *reason) {
    if (!music_ui_playing) return;
    audio_queue_clear(&audio_queue, true);
    music_ui_playing = false;
    printf("[MUSIC-UI] stopped=%s\n", reason);
}

static void music_ui_close(const char *reason) {
    if (!music_ui_open) return;
    music_ui_stop(reason);
    music_ui_open = false;
    timer_screen = false;
    printf("[MUSIC-UI] closed=%s\n", reason);
}

static bool music_play_track(uint8_t track) {
    if (!audio_queue_enqueue(&audio_queue, 8u, track)) {
        puts("[MUSIC-UI] queue full");
        return false;
    }
    music_ui_track = track;
    music_ui_playing = true;
    music_remember(track);
    printf("[MUSIC-UI] playing folder=08 track=%03u\n", (unsigned)track);
    return true;
}

static void music_ui_start(void) {
    if (!music_ui_open) return;
    if (!sound_enabled || alarm_active_index >= 0 || timer.running ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        puts("[MUSIC-UI] playback blocked: sound off or audio busy");
        return;
    }
    // Play the selected entry in the already shuffled playlist.
    if (music_shuffle_next >= music_shuffle_size) return;
    (void)music_play_track(music_shuffle[music_shuffle_next]);
}

static void music_ui_toggle(void) {
    if (!music_ui_open) return;
    if (music_ui_playing) music_ui_stop("BUTTON");
    else music_ui_start();
}

static void music_ui_rotate(int direction) {
    if (!music_ui_open) return;
    music_ui_stop("TRACK_CHANGED");
    if (!music_shuffle_size) return;
    if (direction > 0)
        music_shuffle_next = (music_shuffle_next + 1u) % music_shuffle_size;
    else if (direction < 0)
        music_shuffle_next = (music_shuffle_next + music_shuffle_size - 1u) % music_shuffle_size;
    music_ui_track = music_shuffle[music_shuffle_next];
    printf("[MUSIC-UI] selected position=%u/%u track=%u\n",
           (unsigned)(music_shuffle_next + 1u), (unsigned)music_shuffle_size,
           (unsigned)music_ui_track);
}

static void music_ui_enter(void) {
    if (timer.running || alarm_active_index >= 0 ||
        menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        puts("[MUSIC-UI] open blocked: timer/alarm/menu/audio busy");
        return;
    }
    if (!music_random_seeded) {
        srand((unsigned)time_us_64());
        music_random_seeded = true;
    }
    uint8_t detected = dfplayer_folder_count(8u, to_ms_since_boot(get_absolute_time()));
    music_track_count = detected ? detected : 45u;
    printf("[MUSIC-UI] folder=08 files=%u source=%s\n",
           (unsigned)music_track_count, detected ? "DFPLAYER" : "FALLBACK");
    music_recent_size = 0u;
    music_shuffle_refill();
    music_ui_track = music_shuffle[0];
    music_ui_open = true;
    music_ui_playing = false;
    timer_screen = false;
    printf("[MUSIC-UI] playlist ready tracks=%u; position=1/%u track=%u\n",
           (unsigned)music_track_count, (unsigned)music_shuffle_size,
           (unsigned)music_ui_track);
    printf("[MUSIC-UI] opened track=%u TIMER=PLAY/STOP MINUS=EXIT\n",
           (unsigned)music_ui_track);
}

static void music_ui_service(void) {
    if (!music_ui_open || !music_ui_playing ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy()) return;
    // Advance through the existing playlist; reshuffle only at cycle end.
    ++music_shuffle_next;
    if (music_shuffle_next >= music_shuffle_size) {
        music_shuffle_refill();
        puts("[MUSIC-UI] next shuffled cycle");
    }
    if (music_shuffle_next < music_shuffle_size)
        (void)music_play_track(music_shuffle[music_shuffle_next]);
}

static void alarm_ui_open(uint64_t now) {
    if (timer.running || alarm_active_index >= 0 ||
        menu_state != MENU_CLOSED || music_ui_open) {
        puts("[ALARM-UI] busy: timer/alarm/settings/music active");
        return;
    }
    alarm_ui_index = 0u;
    alarm_ui_state = ALARM_UI_LIST;
    alarm_ui_last_input_ms = now;
    timer_screen = false;
    puts("[ALARM-UI] list opened; encoder=select TIMER=edit MINUS=exit");
}

static void alarm_ui_begin_edit(uint64_t now) {
    if (alarm_ui_state != ALARM_UI_LIST) return;
    alarm_ui_draft = alarms[alarm_ui_index];
    alarm_ui_state = ALARM_UI_ENABLED;
    alarm_ui_last_input_ms = now;
    printf("[ALARM-UI] editing alarm %u (draft; no flash write)\n",
           (unsigned)(alarm_ui_index + 1u));
}

static void alarm_ui_next(uint64_t now) {
    if (alarm_ui_state == ALARM_UI_CLOSED) return;
    alarm_ui_last_input_ms = now;
    if (alarm_ui_state == ALARM_UI_LIST) {
        alarm_ui_begin_edit(now);
        return;
    }
    switch (alarm_ui_state) {
        case ALARM_UI_ENABLED: alarm_ui_state = ALARM_UI_HOUR; break;
        case ALARM_UI_HOUR: alarm_ui_state = ALARM_UI_MINUTE; break;
        case ALARM_UI_MINUTE: alarm_ui_state = ALARM_UI_SOUND; break;
        case ALARM_UI_SOUND:
            if (alarm_ui_draft.sound == ALARM_SOUND_MUSIC) {
                alarm_ui_state = ALARM_UI_TRACK;
                break;
            }
            // Signal has no track editing; commit on next press.
            goto commit;
        case ALARM_UI_TRACK:
commit:
            alarm_preview_stop("COMMIT");
            if (!alarm_entry_valid(&alarm_ui_draft)) {
                puts("[ALARM-UI] invalid draft; refused to save");
                return;
            }
            {
                alarm_entry_t *saved = &alarms[alarm_ui_index];
                bool changed = saved->enabled != alarm_ui_draft.enabled ||
                               saved->hour != alarm_ui_draft.hour ||
                               saved->minute != alarm_ui_draft.minute ||
                               saved->sound != alarm_ui_draft.sound ||
                               saved->track != alarm_ui_draft.track;
                *saved = alarm_ui_draft;
                if (changed) {
                    // Consume current minute if this alarm was just
                    // enabled or edited for the current RTC minute.
                    ds1302_datetime_t dt = {0};
                    if (saved->enabled && ds1302_read_datetime(&dt) &&
                        dt.hour == saved->hour && dt.minute == saved->minute) {
                        uint64_t key = auto_minute_key(&dt);
                        if (!alarm_scheduler.minute_valid ||
                            alarm_scheduler.minute_key != key) {
                            alarm_scheduler.minute_valid = true;
                            alarm_scheduler.minute_key = key;
                            alarm_scheduler.fired_mask = 0u;
                        }
                        alarm_scheduler.fired_mask |=
                            (uint8_t)(1u << alarm_ui_index);
                    }
                    settings_mark_changed();
                }
                printf("[ALARM-UI] saved #%u %s %02u:%02u %s track=%u changed=%s\n",
                       (unsigned)(alarm_ui_index + 1u),
                       saved->enabled ? "ON" : "OFF",
                       (unsigned)saved->hour, (unsigned)saved->minute,
                       saved->sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL",
                       (unsigned)saved->track, changed ? "YES" : "NO");
            }
            alarm_ui_state = ALARM_UI_LIST;
            return;
        default: return;
    }
    printf("[ALARM-UI] field=%u\n", (unsigned)alarm_ui_state);
}

static void alarm_ui_rotate(int direction, uint64_t now) {
    if (alarm_ui_state == ALARM_UI_CLOSED) return;
    alarm_ui_last_input_ms = now;
    if (alarm_ui_state == ALARM_UI_TRACK) alarm_preview_stop("TRACK_CHANGED");
    switch (alarm_ui_state) {
        case ALARM_UI_LIST:
            alarm_ui_index = wrap_u8(alarm_ui_index, 0u,
                                     CLOCK_ALARM_COUNT - 1u, direction);
            printf("[ALARM-UI] selected #%u\n", (unsigned)(alarm_ui_index + 1u));
            return;
        case ALARM_UI_ENABLED:
            alarm_ui_draft.enabled = !alarm_ui_draft.enabled;
            break;
        case ALARM_UI_HOUR:
            alarm_ui_draft.hour = wrap_u8(alarm_ui_draft.hour, 0u, 23u, direction);
            break;
        case ALARM_UI_MINUTE:
            alarm_ui_draft.minute = wrap_u8(alarm_ui_draft.minute, 0u, 59u, direction);
            break;
        case ALARM_UI_SOUND:
            alarm_ui_draft.sound =
                alarm_ui_draft.sound == ALARM_SOUND_SIGNAL ?
                ALARM_SOUND_MUSIC : ALARM_SOUND_SIGNAL;
            break;
        case ALARM_UI_TRACK:
            alarm_ui_draft.track = wrap_u8(alarm_ui_draft.track, 1u,
                                            CLOCK_ALARM_MAX_TRACK, direction);
            break;
        default: return;
    }
    printf("[ALARM-UI] draft #%u %s %02u:%02u %s track=%u\n",
           (unsigned)(alarm_ui_index + 1u),
           alarm_ui_draft.enabled ? "ON" : "OFF",
           (unsigned)alarm_ui_draft.hour, (unsigned)alarm_ui_draft.minute,
           alarm_ui_draft.sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL",
           (unsigned)alarm_ui_draft.track);
}

static void alarm_ui_back(uint64_t now) {
    if (alarm_ui_state == ALARM_UI_CLOSED) return;
    alarm_ui_last_input_ms = now;
    alarm_preview_stop("BACK");
    if (alarm_ui_state == ALARM_UI_LIST) {
        alarm_ui_close("MINUS");
    } else {
        // Never apply an incomplete draft.
        alarm_ui_state = ALARM_UI_LIST;
        printf("[ALARM-UI] edit cancelled #%u (RAM/flash unchanged)\n",
               (unsigned)(alarm_ui_index + 1u));
    }
}

static const char *menu_item_name(unsigned index) {
    static const char *names[SETTINGS_MENU_ITEMS] = {
        "Language", "Time", "Date", "Quiet mode",
        "Half-hour beep", "Exact-hour word", "Volume"
    };
    return index < SETTINGS_MENU_ITEMS ? names[index] : "?";
}

static void menu_close(void) {
    if (menu_state == MENU_CLOSED) return;
    menu_state = MENU_CLOSED;
    timer_screen = false;
    puts("[MENU] Closed");
}

static void menu_open(uint64_t now) {
    if (timer.running) {
        puts("[MENU] Settings unavailable during countdown");
        return;
    }
    menu_index = 0;
    menu_state = MENU_LIST;
    menu_last_input_ms = now;
    printf("[MENU] Open: %s\n", menu_item_name(menu_index));
}

static void menu_begin_edit(uint64_t now) {
    menu_last_input_ms = now;
    if (menu_state != MENU_LIST) return;
    switch (menu_index) {
        case 0:
            menu_language_choice = language;
            menu_state = MENU_EDIT_LANGUAGE;
            break;
        case 1:
        case 2:
            if (!ds1302_read_datetime(&menu_rtc_choice)) {
                puts("[MENU] Cannot edit time/date: RTC read failed");
                return;
            }
            menu_state = menu_index == 1u ? MENU_EDIT_TIME_H : MENU_EDIT_DATE_D;
            break;
        case 3:
            menu_quiet_enabled_choice = quiet_enabled;
            menu_quiet_start_choice = quiet_start;
            menu_quiet_end_choice = quiet_end;
            menu_state = MENU_EDIT_QUIET_ENABLED;
            break;
        case 4:
            menu_half_hour_choice = half_hour_enabled;
            menu_state = MENU_EDIT_HALF_HOUR;
            break;
        case 5:
            menu_exact_choice = exact_hour_phrase;
            menu_state = MENU_EDIT_EXACT;
            break;
        case 6:
            menu_volume_choice = volume;
            menu_state = MENU_EDIT_VOLUME;
            break;
        default:
            return;
    }
    printf("[MENU] Edit %s\n", menu_item_name(menu_index));
}

static void menu_commit(uint64_t now) {
    menu_last_input_ms = now;
    switch (menu_state) {
        case MENU_EDIT_LANGUAGE:
            if (language != menu_language_choice) {
                language = menu_language_choice;
                settings_mark_changed();
            }
            printf("[MENU] Language selected: %s\n", speech_language_name(language));
            break;
        case MENU_EDIT_VOLUME:
            if (volume != menu_volume_choice) {
                volume = menu_volume_choice;
                audio_queue_set_volume(&audio_queue, volume);
                settings_mark_changed();
            }
            printf("[MENU] Volume selected: %u/30\n", (unsigned)volume);
            break;
        case MENU_EDIT_TIME_H:
            menu_state = MENU_EDIT_TIME_M;
            puts("[MENU] Set minutes");
            return;
        case MENU_EDIT_TIME_M:
            menu_state = MENU_EDIT_TIME_S;
            puts("[MENU] Set seconds");
            return;
        case MENU_EDIT_TIME_S:
            if (!menu_write_rtc(false)) return;
            break;
        case MENU_EDIT_DATE_D:
            menu_state = MENU_EDIT_DATE_M;
            puts("[MENU] Set month");
            return;
        case MENU_EDIT_DATE_M:
            menu_state = MENU_EDIT_DATE_Y;
            puts("[MENU] Set year");
            return;
        case MENU_EDIT_DATE_Y:
            if (!menu_write_rtc(true)) return;
            break;
        case MENU_EDIT_QUIET_ENABLED:
            menu_state = MENU_EDIT_QUIET_START;
            puts("[MENU] Set quiet start hour");
            return;
        case MENU_EDIT_QUIET_START:
            menu_state = MENU_EDIT_QUIET_END;
            puts("[MENU] Set quiet end hour");
            return;
        case MENU_EDIT_QUIET_END:
            if (quiet_enabled != menu_quiet_enabled_choice ||
                quiet_start != menu_quiet_start_choice ||
                quiet_end != menu_quiet_end_choice) {
                quiet_enabled = menu_quiet_enabled_choice;
                quiet_start = menu_quiet_start_choice;
                quiet_end = menu_quiet_end_choice;
                settings_mark_changed();
            }
            printf("[MENU] Quiet mode selected: %s %02u-%02u\n",
                   quiet_enabled ? "ON" : "OFF",
                   (unsigned)quiet_start, (unsigned)quiet_end);
            break;
        case MENU_EDIT_HALF_HOUR:
            if (half_hour_enabled != menu_half_hour_choice) {
                half_hour_enabled = menu_half_hour_choice;
                settings_mark_changed();
            }
            printf("[MENU] Half-hour beep selected: %s (playback not enabled yet)\n",
                   half_hour_enabled ? "ON" : "OFF");
            break;
        case MENU_EDIT_EXACT:
            if (exact_hour_phrase != menu_exact_choice) {
                exact_hour_phrase = menu_exact_choice;
                settings_mark_changed();
            }
            printf("[MENU] Exact-hour selected: %s\n", exact_hour_phrase ? "ON" : "OFF");
            break;
        default:
            return;
    }
    // Match MicroPython: commit returns to the settings list.
    menu_state = MENU_LIST;
}

static void menu_back(uint64_t now) {
    if (menu_state == MENU_CLOSED) return;
    menu_last_input_ms = now;
    if (menu_state == MENU_LIST) {
        menu_close();
    } else {
        printf("[MENU] Cancelled edit: %s\n", menu_item_name(menu_index));
        menu_state = MENU_LIST;
    }
}

static void menu_rotate(int direction, uint64_t now) {
    if (menu_state == MENU_CLOSED) return;
    menu_last_input_ms = now;
    switch (menu_state) {
        case MENU_LIST:
            menu_index = (uint8_t)((menu_index + SETTINGS_MENU_ITEMS +
                                  (direction > 0 ? 1u : SETTINGS_MENU_ITEMS - 1u)) %
                                  SETTINGS_MENU_ITEMS);
            printf("[MENU] Item: %s\n", menu_item_name(menu_index));
            break;
        case MENU_EDIT_LANGUAGE:
            menu_language_choice = menu_language_choice == SPEECH_RU ? SPEECH_DE : SPEECH_RU;
            printf("[MENU] Language candidate: %s\n", speech_language_name(menu_language_choice));
            break;
        case MENU_EDIT_TIME_H:
            menu_rtc_choice.hour = wrap_u8(menu_rtc_choice.hour, 0, 23, direction);
            break;
        case MENU_EDIT_TIME_M:
            menu_rtc_choice.minute = wrap_u8(menu_rtc_choice.minute, 0, 59, direction);
            break;
        case MENU_EDIT_TIME_S:
            menu_rtc_choice.second = wrap_u8(menu_rtc_choice.second, 0, 59, direction);
            break;
        case MENU_EDIT_DATE_D:
            menu_rtc_choice.day = wrap_u8(menu_rtc_choice.day, 1,
                menu_month_days(menu_rtc_choice.year, menu_rtc_choice.month), direction);
            break;
        case MENU_EDIT_DATE_M:
            menu_rtc_choice.month = wrap_u8(menu_rtc_choice.month, 1, 12, direction);
            if (menu_rtc_choice.day > menu_month_days(menu_rtc_choice.year, menu_rtc_choice.month))
                menu_rtc_choice.day = menu_month_days(menu_rtc_choice.year, menu_rtc_choice.month);
            break;
        case MENU_EDIT_DATE_Y:
            menu_rtc_choice.year = direction > 0 ?
                (menu_rtc_choice.year >= 2099u ? 2000u : menu_rtc_choice.year + 1u) :
                (menu_rtc_choice.year <= 2000u ? 2099u : menu_rtc_choice.year - 1u);
            if (menu_rtc_choice.day > menu_month_days(menu_rtc_choice.year, menu_rtc_choice.month))
                menu_rtc_choice.day = menu_month_days(menu_rtc_choice.year, menu_rtc_choice.month);
            break;
        case MENU_EDIT_VOLUME: {
            int choice = (int)menu_volume_choice + direction;
            if (choice < 0) choice = 0;
            if (choice > 30) choice = 30;
            menu_volume_choice = (uint8_t)choice;
            printf("[MENU] Volume candidate: %u/30\n", (unsigned)menu_volume_choice);
            break;
        }
        case MENU_EDIT_QUIET_ENABLED:
            menu_quiet_enabled_choice = !menu_quiet_enabled_choice;
            printf("[MENU] Quiet candidate: %s\n", menu_quiet_enabled_choice ? "ON" : "OFF");
            break;
        case MENU_EDIT_QUIET_START:
            menu_quiet_start_choice = wrap_u8(menu_quiet_start_choice, 0, 23, direction);
            printf("[MENU] Quiet start candidate: %02u\n", (unsigned)menu_quiet_start_choice);
            break;
        case MENU_EDIT_QUIET_END:
            menu_quiet_end_choice = wrap_u8(menu_quiet_end_choice, 0, 23, direction);
            printf("[MENU] Quiet end candidate: %02u\n", (unsigned)menu_quiet_end_choice);
            break;
        case MENU_EDIT_HALF_HOUR:
            menu_half_hour_choice = !menu_half_hour_choice;
            printf("[MENU] Half-hour candidate: %s\n", menu_half_hour_choice ? "ON" : "OFF");
            break;
        case MENU_EDIT_EXACT:
            menu_exact_choice = !menu_exact_choice;
            printf("[MENU] Exact-hour candidate: %s\n", menu_exact_choice ? "ON" : "OFF");
            break;
        default:
            break;
    }
    if (menu_is_time_edit()) {
        printf("[MENU] Time candidate %02u:%02u:%02u\n",
               (unsigned)menu_rtc_choice.hour, (unsigned)menu_rtc_choice.minute,
               (unsigned)menu_rtc_choice.second);
    } else if (menu_is_date_edit()) {
        printf("[MENU] Date candidate %02u.%02u.%04u\n",
               (unsigned)menu_rtc_choice.day, (unsigned)menu_rtc_choice.month,
               (unsigned)menu_rtc_choice.year);
    }
}

static void save_settings_now(void) {
    clock_settings_t cfg = settings_snapshot();
    settings_store_result_t result = settings_store_save(&cfg);
    printf("[FLASH] save=%s\n", settings_store_result_name(result));
    if (result == SETTINGS_STORE_OK) {
        last_persisted_settings = cfg;
        autosave_pending = false;
        flash_written_this_boot = true;
        last_flash_write_ms = to_ms_since_boot(get_absolute_time());
        autosave_attempted = false;
    } else if (result == SETTINGS_STORE_OCCUPIED) {
        puts("[FLASH] Refusing to erase unknown flash contents.");
    }
}

static void load_settings_at_boot(void) {
    clock_settings_t saved = {0};
    settings_store_result_t result = settings_store_load(&saved);
    if (result == SETTINGS_STORE_OK) {
        language = saved.language == 1u ? SPEECH_DE : SPEECH_RU;
        volume = saved.volume;
        exact_hour_phrase = saved.exact_hour;
        quiet_enabled = saved.quiet_enabled;
        quiet_start = saved.quiet_start;
        quiet_end = saved.quiet_end;
        half_hour_enabled = saved.half_hour_enabled;
        clock_mode = saved.clock_mode;
        memcpy(alarms, saved.alarms, sizeof(alarms));
        printf("[FLASH] audio preferences loaded manual=%u hourly=%u timer=%u alarm=%u\n",
               (unsigned)saved.audio_manual_jbl, (unsigned)saved.audio_hourly_jbl,
               (unsigned)saved.audio_timer_jbl, (unsigned)saved.audio_alarm_jbl);
#ifdef CLOCK_ENABLE_A2DP
        auto_voice_jbl_enabled = saved.audio_hourly_jbl != 0u;
        timer_audio_jbl_enabled = saved.audio_timer_jbl != 0u;
        alarm_audio_jbl_enabled = saved.audio_alarm_jbl != 0u;
        audio_router_select(&audio_router,
            saved.audio_manual_jbl ? AUDIO_OUTPUT_BT_JBL : AUDIO_OUTPUT_LOCAL);
        audio_router_bluetooth_state(&audio_router, clock_a2dp_stream_active());
#else
        saved_audio_manual = saved.audio_manual_jbl;
        saved_audio_hourly = saved.audio_hourly_jbl;
        saved_audio_timer = saved.audio_timer_jbl;
        saved_audio_alarm = saved.audio_alarm_jbl;
#endif
        printf("[FLASH] loaded lang=%s volume=%u exact=%s quiet=%s %02u-%02u half_hour=%s mode=%s\n",
               speech_language_name(language), (unsigned)volume,
               exact_hour_phrase ? "ON" : "OFF",
               quiet_enabled ? "ON" : "OFF", (unsigned)quiet_start,
               (unsigned)quiet_end, half_hour_enabled ? "ON" : "OFF",
                clock_mode ? "CHIME" : "VOICE");
    } else {
        printf("[FLASH] load=%s; using defaults (RU, volume=10)\n",
               settings_store_result_name(result));
    }
    // Boot never triggers a write just because records are absent.
    last_persisted_settings = settings_snapshot();
    autosave_pending = false;
}

static void print_rtc_raw(const char *label, const uint8_t raw[7]) {
    printf("[RTC-DEBUG] %s sec=%02x min=%02x hour=%02x day=%02x month=%02x weekday=%02x year=%02x\n",
           label, (unsigned)raw[0], (unsigned)raw[1], (unsigned)raw[2],
           (unsigned)raw[3], (unsigned)raw[4], (unsigned)raw[5],
           (unsigned)raw[6]);
}

static void print_rtc_debug(void) {
    ds1302_diagnostics_t d;
    ds1302_get_diagnostics(&d);
    printf("[RTC-DEBUG] ok=%lu failed=%lu consecutive=%lu max_consecutive=%lu\n",
           (unsigned long)d.reads_ok, (unsigned long)d.reads_failed,
           (unsigned long)d.consecutive_failures,
           (unsigned long)d.max_consecutive_failures);
    print_rtc_raw("last_read", d.last_raw);
    if (!d.have_last_failure) {
        puts("[RTC-DEBUG] No failures captured since boot/reset");
        return;
    }
    print_rtc_raw("last_error", d.last_failed_raw);
    printf("[RTC-DEBUG] error_flags=0x%03lx reasons:",
           (unsigned long)d.last_failure_flags);
    const struct { uint32_t mask; const char *name; } reasons[] = {
        { DS1302_ERR_CH, "CH" },
        { DS1302_ERR_12H_MODE, "12H_MODE" },
        { DS1302_ERR_SECONDS, "SECONDS_RANGE" },
        { DS1302_ERR_MINUTES, "MINUTES_RANGE" },
        { DS1302_ERR_HOURS, "HOURS_RANGE" },
        { DS1302_ERR_DAY, "DAY_RANGE" },
        { DS1302_ERR_MONTH, "MONTH_RANGE" },
        { DS1302_ERR_WEEKDAY, "WEEKDAY_RANGE" },
        { DS1302_ERR_YEAR_BCD, "YEAR_BCD" },
        { DS1302_ERR_INVALID_BCD, "INVALID_BCD" }
    };
    for (unsigned i = 0; i < sizeof(reasons) / sizeof(reasons[0]); ++i) {
        if (d.last_failure_flags & reasons[i].mask) {
            printf(" %s", reasons[i].name);
        }
    }
    putchar('\n');
}

static void alarm_status(void) {
    printf("[ALARM] flash v3 capable; active=%d (0=none)\n",
           alarm_active_index + 1);
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        printf("[ALARM] %u enabled=%s time=%02u:%02u sound=%s track=%u valid=%s\n",
               i + 1u, a->enabled ? "ON" : "OFF",
               (unsigned)a->hour, (unsigned)a->minute,
               a->sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL",
               (unsigned)a->track, alarm_entry_valid(a) ? "YES" : "NO");
    }
}

#ifdef CLOCK_ENABLE_A2DP
// Alarm owns the MP3 playback while active. Playback state does not alter
// the one-shot RTC scheduler or the persisted settings.
static void bt_alarm_stop(const char *reason) {
    if (!bt_alarm_running) return;
    if (bt_alarm_started && mp3_play_active()) mp3_play_stop();
    bt_alarm_running = false;
    bt_alarm_started = false;
    printf("[BT-ALARM] STOP reason=%s\n", reason);
}
static bool bt_alarm_prepare(const audio_queue_t *plan) {
    if (!alarm_audio_jbl_enabled || !plan || !plan->size ||
        plan->size > AUDIO_QUEUE_CAPACITY ||
        !clock_a2dp_stream_active() || clock_a2dp_current_rate() != 44100u ||
        mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active()) {
        if (alarm_audio_jbl_enabled)
            puts("[BT-ALARM] JBL unavailable/busy; choosing LOCAL");
        return false;
    }
    for (unsigned i = 0; i < plan->size; ++i)
        bt_alarm_tracks[i] = plan->items[(plan->head + i) % AUDIO_QUEUE_CAPACITY];
    bt_alarm_count = plan->size;
    bt_alarm_index = 0u;
    bt_alarm_started = false;
    bt_alarm_running = true;
    printf("[BT-ALARM] START segments=%u gain=53.3%%\n",
           (unsigned)bt_alarm_count);
    return true;
}
static void bt_alarm_fallback(const char *reason) {
    if (!bt_alarm_running) return;
    if (bt_alarm_started && mp3_play_active()) mp3_play_stop();
    unsigned queued = 0u;
    for (unsigned i = bt_alarm_index; i < bt_alarm_count; ++i) {
        audio_track_t t = bt_alarm_tracks[i];
        if (!audio_queue_enqueue(&audio_queue, t.folder, t.track)) break;
        ++queued;
    }
    bt_alarm_running = false;
    bt_alarm_started = false;
    printf("[BT-ALARM] LOCAL fallback reason=%s queued=%u/%u\n",
           reason, queued, (unsigned)(bt_alarm_count - bt_alarm_index));
}
static void bt_alarm_service(void) {
    if (!bt_alarm_running) return;
    if (alarm_active_index < 0 || !sound_enabled) {
        bt_alarm_stop("ALARM_STOPPED_OR_MUTED");
        return;
    }
    if (!clock_a2dp_stream_active() || clock_a2dp_current_rate() != 44100u) {
        bt_alarm_fallback("A2DP_UNAVAILABLE");
        return;
    }
    if (audio_queue.pause_pending || dfplayer_busy()) return;
    if (audio_queue.size || audio_queue.track_started) {
        bt_alarm_fallback("LOCAL_AUDIO_PRIORITY");
        return;
    }
    bt_sequence_result_t step = bt_sequence_step(bt_alarm_tracks, bt_alarm_count,
                                                  &bt_alarm_index, &bt_alarm_started,
                                                  "BT-ALARM");
    if (step == BT_SEQUENCE_COMPLETE) {
        bt_alarm_running = false;
        puts("[BT-ALARM] COMPLETE");
    } else if (step == BT_SEQUENCE_FAILED) {
        bt_alarm_fallback("MP3_OPEN_FAILED");
    }
}
#endif

static void alarm_start_audio(unsigned index, uint8_t hour, uint8_t minute,
                              uint64_t now, bool test) {
#ifdef CLOCK_ENABLE_A2DP
    bt_speech_cancel("ALARM_PRIORITY");
    bt_timer_cancel("ALARM_PRIORITY");
    if (routed_test_pending) {
        if (mp3_play_active()) mp3_play_stop();
        routed_test_pending = false;
    }
    if (mp3_play_active()) mp3_play_stop();
#endif
    audio_queue_clear(&audio_queue, true);
    audio_queue_t plan = {0};
    bool queued = true;
    if (language == SPEECH_RU)
        queued = audio_queue_enqueue(&plan, 7u, (uint8_t)(index + 1u));
    else
        puts("[ALARM] DE number phrase unavailable; skipping RU number");
    if (queued)
        queued = speech_enqueue_time(&plan, language, hour, minute, exact_hour_phrase);
    if (queued) {
        if (alarms[index].sound == ALARM_SOUND_MUSIC)
            queued = audio_queue_enqueue(&plan, 8u, alarms[index].track);
        else
            queued = audio_queue_enqueue(&plan, 7u, 13u);
    }
    if (!queued || !plan.size) {
        puts("[ALARM] skipped=QUEUE_FULL or empty plan");
        return;
    }
    alarm_active_index = (int)index;
    alarm_deadline_ms = now + ALARM_SAFETY_TIMEOUT_MS;
    alarm_audio_armed = true;
#ifdef CLOCK_ENABLE_A2DP
    if (bt_alarm_prepare(&plan)) {
        printf("[ALARM-AUDIO] output=JBL%s\n", test ? " TEST" : "");
    } else
#endif
    {
        for (unsigned i = 0; i < plan.size; ++i) {
            audio_track_t t = plan.items[(plan.head + i) % AUDIO_QUEUE_CAPACITY];
            if (!audio_queue_enqueue(&audio_queue, t.folder, t.track))
                puts("[ALARM] CRITICAL: local enqueue failed");
        }
        printf("[ALARM-AUDIO] output=LOCAL%s\n", test ? " TEST" : "");
    }
    printf("[ALARM] %u RINGING %02u:%02u sound=%s track=%u%s\n",
           index + 1u, (unsigned)hour, (unsigned)minute,
           alarms[index].sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL",
           (unsigned)alarms[index].track, test ? " TEST" : "");
}

static void alarm_stop(const char *reason) {
    if (alarm_active_index < 0) {
        puts("[ALARM] no active alarm");
        return;
    }
    int number = alarm_active_index + 1;
#ifdef CLOCK_ENABLE_A2DP
    bt_alarm_stop(reason);
#endif
    audio_queue_clear(&audio_queue, true);
    alarm_active_index = -1;
    alarm_audio_armed = false;
    alarm_deadline_ms = 0u;
    printf("[ALARM] %d stopped=%s\n", number, reason);
}

static void alarm_service_playback(uint64_t now) {
    if (alarm_active_index < 0) return;
    if (now >= alarm_deadline_ms) {
        alarm_stop("SAFETY_TIMEOUT");
        return;
    }
    // Do not assume playback is complete before the queued DFPlayer
    // command actually starts; BUSY must also return inactive.
    if (alarm_audio_armed && audio_queue_idle(&audio_queue) && !dfplayer_busy()
#ifdef CLOCK_ENABLE_A2DP
        && !bt_alarm_running
#endif
       ) {
        alarm_active_index = -1;
        alarm_audio_armed = false;
        alarm_deadline_ms = 0u;
        puts("[ALARM] finished=AUDIO_COMPLETED");
    }
}

static void alarm_service_rtc(const ds1302_datetime_t *dt, uint64_t now) {
    if (!dt) return;
    if (!alarm_boot_seeded) {
        // First RTC minute after power-up is only observed, never fired.
        // Avoid a surprise alarm when rebooting right at its scheduled time.
        alarm_boot_seeded = true;
        alarm_scheduler.minute_valid = true;
        alarm_scheduler.minute_key = auto_minute_key(dt);
        alarm_scheduler.fired_mask = 0u;
        for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
            if (alarms[i].enabled && alarms[i].hour == dt->hour &&
                alarms[i].minute == dt->minute) {
                alarm_scheduler.fired_mask |= (uint8_t)(1u << i);
            }
        }
        printf("[ALARM] boot: %02u:%02u consumed, fired_mask=0x%02x; next minute armed\n",
               (unsigned)dt->hour, (unsigned)dt->minute,
               (unsigned)alarm_scheduler.fired_mask);
        return;
    }
    if (dt->second > 5u || alarm_active_index >= 0) return;
    // Atomically claim every match for the minute. If two or more alarms
    // coincide, only the lowest-numbered slot may play. Mark others
    // consumed so stopping the winner never triggers a surprise second alarm.
    uint8_t other_mask = 0u;
    int index = alarm_scheduler_claim_due(&alarm_scheduler, alarms, dt->year,
                         dt->month, dt->day, dt->hour, dt->minute, &other_mask);
    if (index < 0) return;
    if (other_mask) {
        printf("[ALARM] collision %02u:%02u winner=%d skipped_mask=0x%02x (once per minute)\n",
               (unsigned)dt->hour, (unsigned)dt->minute, index + 1,
               (unsigned)other_mask);
    }
    if (timer.running || (finished_screen_until_ms &&
        now < finished_screen_until_ms)) {
        printf("[ALARM] %d skipped=TIMER_PRIORITY\n", index + 1);
        return;
    }
    if (menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED) {
        printf("[ALARM] %d skipped=MENU_OPEN\n", index + 1);
        return;
    }
    // Scheduled alarms override the standalone music player.
    // Close it before queueing alarm speech, preserving ALARM button priority.
    if (music_ui_open) music_ui_close("ALARM_PRIORITY");
    // An alarm owns its minute: prevent normal xx:00/xx:30 announcements.
    if (dt->minute == 0u || dt->minute == 30u) {
        auto_seen_key_valid = true;
        auto_seen_minute_key = auto_minute_key(dt);
    }
    if (!sound_enabled) {
        printf("[ALARM] %d skipped=SOUND_OFF\n", index + 1);
        return;
    }
#ifdef CLOCK_ENABLE_A2DP
    bt_timer_cancel("ALARM_PRIORITY");
#endif
    alarm_start_audio((unsigned)index, dt->hour, dt->minute, now, false);
}

static void alarm_set(char *args) {
    unsigned hh = 0u, mm = 0u, track = 1u;
    char kind[12] = {0};
    char extra = 0;
    int fields = sscanf(args, " %u %u %11s %u %c",
                        &hh, &mm, kind, &track, &extra);
    bool signal = fields == 3 && strcmp(kind, "signal") == 0;
    bool music = fields == 4 && strcmp(kind, "music") == 0 &&
                 track >= 1u && track <= CLOCK_ALARM_MAX_TRACK;
    if (hh > 23u || mm > 59u || (!signal && !music)) {
        puts("[ALARM] Use alarm-set HH MM signal OR alarm-set HH MM music TRACK(1..45)");
        return;
    }
    if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open) {
        puts("[ALARM] Stop ringing alarm or close editor/player before configuration");
        return;
    }
    alarms[0].enabled = true;
    alarms[0].hour = (uint8_t)hh;
    alarms[0].minute = (uint8_t)mm;
    alarms[0].sound = music ? ALARM_SOUND_MUSIC : ALARM_SOUND_SIGNAL;
    alarms[0].track = music ? (uint8_t)track : 1u;
    // Reset one-shot state for a newly configured test alarm;
    // never replay an already active current minute immediately.
    alarm_scheduler_init(&alarm_scheduler);
    ds1302_datetime_t current = {0};
    if (ds1302_read_datetime(&current)) {
        (void)alarm_scheduler_pick(&alarm_scheduler, alarms,
                   current.year, current.month, current.day,
                   current.hour, current.minute, false);
    }
    settings_mark_changed();
    printf("[ALARM] 1 enabled=ON time=%02u:%02u sound=%s track=%u\n",
           hh, mm, music ? "MUSIC" : "SIGNAL", music ? track : 1u);
}

// New syntax for all five alarm slots; keep alarm-set as a slot-1 alias.
static void alarm_config(char *args) {
    unsigned slot = 0, hh = 0, mm = 0, track = 1;
    char kind[12] = {0}, extra = 0;
    int fields = sscanf(args, " %u %u %u %11s %u %c",
                        &slot, &hh, &mm, kind, &track, &extra);
    bool signal = fields == 4 && strcmp(kind, "signal") == 0;
    bool music = fields == 5 && strcmp(kind, "music") == 0 &&
                 track >= 1u && track <= CLOCK_ALARM_MAX_TRACK;
    if (slot < 1u || slot > CLOCK_ALARM_COUNT || hh > 23u || mm > 59u ||
        (!signal && !music)) {
        puts("[ALARM] Use alarm-config N HH MM signal OR alarm-config N HH MM music TRACK");
        return;
    }
    if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open) {
        puts("[ALARM] config blocked: alarm ringing or editor/player open");
        return;
    }
    alarm_entry_t *a = &alarms[slot - 1u];
    a->enabled = true;
    a->hour = (uint8_t)hh;
    a->minute = (uint8_t)mm;
    a->sound = music ? ALARM_SOUND_MUSIC : ALARM_SOUND_SIGNAL;
    a->track = music ? (uint8_t)track : 1u;

    // Do not trigger retroactively if configured in the very same minute.
    ds1302_datetime_t dt = {0};
    if (ds1302_read_datetime(&dt) && dt.hour == hh && dt.minute == mm) {
        uint64_t key = auto_minute_key(&dt);
        if (!alarm_scheduler.minute_valid || alarm_scheduler.minute_key != key) {
            alarm_scheduler.minute_valid = true;
            alarm_scheduler.minute_key = key;
            alarm_scheduler.fired_mask = 0u;
        }
        alarm_scheduler.fired_mask |= (uint8_t)(1u << (slot - 1u));
    }
    settings_mark_changed();
    printf("[ALARM] %u enabled=ON time=%02u:%02u sound=%s track=%u\n",
           slot, hh, mm, music ? "MUSIC" : "SIGNAL", music ? track : 1u);
}

static void alarm_enable(char *args) {
    unsigned slot = 0;
    char action[8] = {0}, extra = 0;
    if (sscanf(args, " %u %7s %c", &slot, action, &extra) != 2 ||
        slot < 1u || slot > CLOCK_ALARM_COUNT ||
        (strcmp(action, "on") && strcmp(action, "off"))) {
        puts("[ALARM] Use alarm-enable N on|off (N=1..5)");
        return;
    }
    if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open ||
        (alarm_active_index >= 0 && (unsigned)(alarm_active_index + 1) == slot)) {
        puts("[ALARM] close alarm editor or stop ringing alarm first");
        return;
    }
    alarm_entry_t *a = &alarms[slot - 1u];
    a->enabled = strcmp(action, "on") == 0;
    if (a->enabled) {
        // Do not retroactively trigger during the currently active minute.
        ds1302_datetime_t dt = {0};
        if (ds1302_read_datetime(&dt) && dt.hour == a->hour &&
            dt.minute == a->minute) {
            uint64_t key = auto_minute_key(&dt);
            if (!alarm_scheduler.minute_valid || alarm_scheduler.minute_key != key) {
                alarm_scheduler.minute_valid = true;
                alarm_scheduler.minute_key = key;
                alarm_scheduler.fired_mask = 0u;
            }
            alarm_scheduler.fired_mask |= (uint8_t)(1u << (slot - 1u));
        }
    }
    settings_mark_changed();
    printf("[ALARM] %u enabled=%s\n", slot, a->enabled ? "ON" : "OFF");
}

static void alarm_test(char *args) {
    unsigned hh = 0u, mm = 0u, slot = 0u;
    char extra = 0;
    int fields = sscanf(args, " %u %u %u %c", &hh, &mm, &slot, &extra);
    if ((fields != 2 && fields != 3) || hh > 23u || mm > 59u ||
        (fields == 3 && (slot < 1u || slot > CLOCK_ALARM_COUNT))) {
        puts("[ALARM-TEST] Usage: alarm-test HH MM [slot 1..5]");
        puts("[ALARM-TEST] Optional slot simulates enabling that alarm at HH:MM; RAM/flash/RTC unchanged");
        return;
    }
    alarm_entry_t simulated[CLOCK_ALARM_COUNT];
    memcpy(simulated, alarms, sizeof(simulated));
    if (fields == 3) {
        simulated[slot - 1u].enabled = true;
        simulated[slot - 1u].hour = (uint8_t)hh;
        simulated[slot - 1u].minute = (uint8_t)mm;
    }
    ds1302_datetime_t dt = {0};
    if (!ds1302_read_datetime(&dt)) {
        puts("[ALARM-TEST] RTC unavailable; no state altered");
        return;
    }
    alarm_scheduler_t snapshot;
    alarm_scheduler_init(&snapshot);
    int selected = alarm_scheduler_pick(&snapshot, simulated, dt.year, dt.month,
                                        dt.day, (uint8_t)hh, (uint8_t)mm, false);
    printf("[ALARM-TEST] %02u:%02u virtual_slot=%u first=%d (dry-run: no playback/no writes)\n",
           hh, mm, fields == 3 ? slot : 0u,
           selected < 0 ? 0 : selected + 1);
    if (selected >= 0) {
        int blocked = alarm_scheduler_pick(&snapshot, simulated, dt.year, dt.month,
                                           dt.day, (uint8_t)hh, (uint8_t)mm, true);
        // With an active alarm, no second alarm can be scheduled.
        int following = alarm_scheduler_pick(&snapshot, simulated, dt.year, dt.month,
                                             dt.day, (uint8_t)hh, (uint8_t)mm, false);
        printf("[ALARM-TEST] while_ringing=%s after_stop=%d fired_mask=0x%02x\n",
               blocked < 0 ? "BLOCKED" : "ERROR",
               following < 0 ? 0 : following + 1,
               (unsigned)snapshot.fired_mask);
    }
}

static bool bootsel_safe(uint64_t now) {
    if (alarm_active_index >= 0 || timer.running ||
        (finished_screen_until_ms && now < finished_screen_until_ms) ||
        menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open || alarm_preview_active || music_ui_playing) {
        puts("[BOOTSEL] REFUSED: timer, alarm, menu, or music is active");
        return false;
    }
    if (autosave_pending || autosave_attempted) {
        puts("[BOOTSEL] REFUSED: flash settings save pending/retrying; wait for autosave=OK");
        return false;
    }
    if (!audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        puts("[BOOTSEL] REFUSED: DFPlayer/queue is busy");
        return false;
    }
    return true;
}

static void bootsel_command(const char *args, uint64_t now) {
    if (strcmp(args, "arm") == 0) {
        bootsel_armed = false;
        if (!bootsel_safe(now)) return;
        bootsel_armed = true;
        bootsel_armed_ms = now;
        puts("[BOOTSEL] ARMED for 15s; send: bootsel confirm");
        return;
    }
    if (strcmp(args, "cancel") == 0) {
        bootsel_armed = false;
        puts("[BOOTSEL] CANCELLED");
        return;
    }
    if (strcmp(args, "confirm") == 0) {
        if (!bootsel_armed || now - bootsel_armed_ms > BOOTSEL_CONFIRM_WINDOW_MS) {
            bootsel_armed = false;
            puts("[BOOTSEL] NOT ARMED or EXPIRED; send: bootsel arm");
            return;
        }
        bootsel_armed = false;
        if (!bootsel_safe(now)) return;
        puts("[BOOTSEL] ENTERING USB BOOTSEL; COM port will disconnect.");
        puts("[BOOTSEL] Copy speaking_clock_c.uf2 to RPI-RP2; existing flash settings normally remain.");
        fflush(stdout);
        sleep_ms(250u); // Give the USB serial host time to receive diagnostics.
        rom_reset_usb_boot(0u, 0u); // Both USB mass-storage and PICOBOOT enabled.
    }
    puts("[BOOTSEL] Usage: bootsel arm | bootsel confirm | bootsel cancel");
}

static void handle_command(char *line) {
    while (isspace((unsigned char)*line)) ++line;
    size_t len = strlen(line);
    while (len && isspace((unsigned char)line[len - 1])) line[--len] = 0;
    // FAT32 diagnostics share one synchronous sector buffer with Helix.
    // Never change the SD card's state or FAT geometry during MP3 playback.
    if (mp3_play_active() &&
        (strncmp(line, "sd-", 3) == 0 ||
         strncmp(line, "mp3-test ", 9) == 0)) {
        puts("[MP3-PLAY] SD diagnostics blocked while MP3 playback is active");
        return;
    }
    // A ringtone must never be interrupted by a diagnostic PCM command.
    // alarm-stop, physical ALARM button, and switching to LOCAL remain valid.
#ifdef CLOCK_ENABLE_A2DP
    if (alarm_active_index >= 0 &&
        (strcmp(line, "bt-tone") == 0 || strcmp(line, "bt-clip") == 0 ||
         strcmp(line, "bt-silence") == 0 ||
         strcmp(line, "mp3-stop") == 0 || strcmp(line, "mp3-pause") == 0 ||
         strcmp(line, "mp3-resume") == 0)) {
        puts("[BT-ALARM] Diagnostic playback blocked while alarm rings; use alarm-stop");
        return;
    }
#endif
    uint64_t now = to_ms_since_boot(get_absolute_time());
    if (strcmp(line, "bootsel arm") == 0) {
        bootsel_command("arm", now);
    } else if (strcmp(line, "bootsel confirm") == 0) {
        bootsel_command("confirm", now);
    } else if (strcmp(line, "bootsel cancel") == 0) {
        bootsel_command("cancel", now);
    } else if (strcmp(line, "bootsel") == 0) {
        puts("[BOOTSEL] Usage: bootsel arm | bootsel confirm | bootsel cancel");
#ifdef CLOCK_ENABLE_A2DP
    } else if (strncmp(line, "bt-connect ", 11) == 0) {
        const char *address = line + 11;
        if (!clock_a2dp_connect(address))
            puts("[BT] Direct connect rejected; check address/connection status");
    } else if (strcmp(line, "bt-status") == 0) {
        clock_a2dp_print_status();
    } else if (strcmp(line, "bt-tone") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        if (bt_timer_running) bt_timer_fallback("BT_TONE_OVERRIDE");
        if (bt_speech_running) bt_speech_cancel("BT_TONE_OVERRIDE");
#endif
        if (mp3_play_active()) mp3_play_stop();
        if (!clock_a2dp_play_tone())
            puts("[BT] No active A2DP stream");
    } else if (strcmp(line, "bt-clip") == 0) {
        if (bt_timer_running) bt_timer_fallback("BT_CLIP_OVERRIDE");
        if (bt_speech_running) bt_speech_cancel("BT_CLIP_OVERRIDE");
        if (mp3_play_active()) mp3_play_stop();
        if (!clock_a2dp_play_clip())
            puts("[BT] PCM clip unavailable (A2DP not active)");
    } else if (strcmp(line, "bt-silence") == 0) {
        if (bt_timer_running) bt_timer_fallback("BT_SILENCE_OVERRIDE");
        if (bt_speech_running) bt_speech_cancel("BT_SILENCE_OVERRIDE");
        if (mp3_play_active()) mp3_play_stop();
        clock_a2dp_stop_tone();
#endif
    } else if (strncmp(line, "mp3-pause-test ", 15) == 0) {
#ifdef CLOCK_ENABLE_A2DP
        if (!clock_a2dp_stream_active() || !sound_enabled ||
            timer.running || alarm_active_index >= 0 || music_ui_open ||
            sd_fat32_stream_active() || mp3_probe_active() ||
            bt_speech_running || bt_timer_running || bt_alarm_running ||
            !audio_queue_idle(&audio_queue)) {
            puts("[MP3-TEST] Refused: A2DP not ready or clock/audio busy");
        } else if (!mp3_play_pause_test_start(line + 15, clock_a2dp_current_rate())) {
            puts("[MP3-TEST] Start failed");
        }
#else
        puts("[MP3-TEST] Requires Bluetooth A2DP");
#endif
    } else if (strncmp(line, "mp3-play ", 9) == 0) {
#ifdef CLOCK_ENABLE_A2DP
        if (!clock_a2dp_stream_active() || !sound_enabled ||
            timer.running || alarm_active_index >= 0 || music_ui_open ||
            sd_fat32_stream_active() || mp3_probe_active() ||
            bt_speech_running || !audio_queue_idle(&audio_queue)) {
            puts("[MP3-PLAY] Refused: A2DP not ready, sound muted or timer/alarm/audio busy");
        } else if (!mp3_play_start(line + 9, clock_a2dp_current_rate())) {
            puts("[MP3-PLAY] Start failed");
        }
#else
        puts("[MP3-PLAY] Requires Bluetooth A2DP");
#endif
    } else if (strcmp(line, "mp3-stop") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        bt_speech_cancel("USER_STOP");
        bt_timer_fallback("MP3_STOP_LOCAL_FALLBACK");
#endif
        mp3_play_stop();
#ifdef CLOCK_ENABLE_A2DP
        routed_test_pending = false;
#endif
    } else if (strcmp(line, "mp3-pause") == 0) {
        if (!mp3_play_pause()) puts("[MP3-PLAY] Cannot pause: no active playback or already paused");
    } else if (strcmp(line, "mp3-resume") == 0) {
        if (!mp3_play_resume()) puts("[MP3-PLAY] Cannot resume: no paused playback");
    } else if (strcmp(line, "mp3-status") == 0) {
        mp3_play_status();
    } else if (strncmp(line, "mp3-test ", 9) == 0) {
        if (timer.running || alarm_active_index >= 0 || music_ui_open ||
            sd_fat32_stream_active() || !audio_queue_idle(&audio_queue)) {
            puts("[MP3] Refused while timer/alarm/music/audio/SD test is active");
        } else if (!mp3_probe_start(line + 9)) {
            puts("[MP3] Probe not started");
        }
    } else if (strncmp(line, "sd-stream ", 10) == 0) {
        if (timer.running || alarm_active_index >= 0 || music_ui_open ||
            !audio_queue_idle(&audio_queue)) {
            puts("[SD-STREAM] Refused: timer/alarm/music/audio active");
        } else if (!sd_fat32_stream_start(line + 10)) {
            puts("[SD-STREAM] FAILED to start");
        }
    } else if (strcmp(line, "sd-stop") == 0) {
        sd_fat32_stream_stop();
    } else if (strcmp(line, "sd-list") == 0) {
        if (!sd_fat32_list("")) puts("[SD-LIST] FAILED");
    } else if (strncmp(line, "sd-list ", 8) == 0) {
        if (!sd_fat32_list(line + 8)) puts("[SD-LIST] FAILED");
    } else if (strncmp(line, "sd-file ", 8) == 0) {
        if (!sd_fat32_file(line + 8)) puts("[SD-FILE] FAILED");
    } else if (strcmp(line, "sd-info") == 0) {
        const sd_spi_info_t *sd = sd_spi_info();
        printf("[SD] initialized=%s card=%s filesystem=%s\n",
               sd->ready ? "YES" : "NO",
               sd->high_capacity ? "SDHC/SDXC" : "SDSC/unknown",
               sd->fat_type == 32 ? "FAT32" :
               (sd->fat_type == 16 ? "FAT16" : "not probed/other"));
    } else if (strcmp(line, "sd-test") == 0) {
        if (sd_spi_init_card()) {
            if (sd_spi_probe_fat())
                puts("[SD] PASS: SPI sector read + FAT32 boot sector");
            else
                puts("[SD] WARNING: SPI card init succeeded; FAT32 probe failed");
        } else {
            puts("[SD] FAIL: check GP8/9/10/11, 3V3, GND and card seating");
        }
    } else if (strcmp(line, "audio-health") == 0) {
        // Read-only snapshot for hardware priority/collision regression tests.
        // Never changes clock, audio owner, saved preferences or Flash.
        printf("[AUDIO-HEALTH] timer=%s alarm=%d sound=%s local_queue=%u local_started=%s df_busy=%s\\n",
               timer.running ? "RUNNING" : "IDLE", alarm_active_index + 1,
               sound_enabled ? "ON" : "OFF", (unsigned)audio_queue.size,
               audio_queue.track_started ? "YES" : "NO",
               dfplayer_busy() ? "YES" : "NO");
        printf("[AUDIO-HEALTH] auto=%s clock_mode=%s saved_pending=%s menu=%s music=%s\\n",
               auto_enabled ? "ON" : "OFF", clock_mode ? "CHIME" : "VOICE",
               autosave_pending ? "YES" : "NO",
               menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED
                 ? "OPEN" : "CLOSED", music_ui_open ? "OPEN" : "CLOSED");
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUDIO-HEALTH] BT stream=%s rate=%lu MP3=%s speech=%s (%u/%u) timer=%s (%u/%u) alarm=%s (%u/%u) route_test=%s\\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               (unsigned long)clock_a2dp_current_rate(),
               mp3_play_active() ? "YES" : "NO",
               bt_speech_running ? "YES" : "NO",
               (unsigned)(bt_speech_index + 1u), (unsigned)bt_speech_count,
               bt_timer_running ? "YES" : "NO",
               (unsigned)(bt_timer_index + 1u), (unsigned)bt_timer_count,
               bt_alarm_running ? "YES" : "NO",
               (unsigned)(bt_alarm_index + 1u), (unsigned)bt_alarm_count,
               routed_test_pending ? "YES" : "NO");
#endif
    } else if (strcmp(line, "audio-settings") == 0) {
        printf("[AUDIO-SETTINGS] manual=%s hourly=%s timer=%s alarm=%s\n",
               audio_router.requested == AUDIO_OUTPUT_BT_JBL ? "JBL" : "LOCAL",
#ifdef CLOCK_ENABLE_A2DP
               auto_voice_jbl_enabled ? "JBL" : "LOCAL",
               timer_audio_jbl_enabled ? "JBL" : "LOCAL",
               alarm_audio_jbl_enabled ? "JBL" : "LOCAL"
#else
               "LOCAL", "LOCAL", "LOCAL"
#endif
        );
        printf("[AUDIO-SETTINGS] output=preferred, effective JBL requires A2DP; autosave=%s\n",
               autosave_pending ? "PENDING" : "IDLE");
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUDIO-SETTINGS] Bluetooth stream=%s rate=%luHz; LOCAL fallback enabled\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               (unsigned long)clock_a2dp_current_rate());
#endif
    } else if (strcmp(line, "timer-audio jbl") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        timer_audio_jbl_enabled = true;
        settings_mark_changed();
        printf("[TIMER-AUDIO] output=JBL_PREFERRED BT=%s LOCAL fallback (Flash autosave)\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE");
#else
        puts("[TIMER-AUDIO] Bluetooth support disabled; LOCAL only");
#endif
    } else if (strcmp(line, "timer-audio local") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        timer_audio_jbl_enabled = false;
        settings_mark_changed();
        if (bt_timer_running) bt_timer_fallback("SELECT_LOCAL");
#endif
        puts("[TIMER-AUDIO] output=LOCAL (Flash autosave)");
    } else if (strcmp(line, "timer-audio") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        printf("[TIMER-AUDIO] preferred=%s active=%s BT=%s (Flash autosave)\n",
               timer_audio_jbl_enabled ? "JBL" : "LOCAL",
               bt_timer_running ? "YES" : "NO",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE");
#else
        puts("[TIMER-AUDIO] preferred=LOCAL (Bluetooth disabled)");
#endif
    } else if (strncmp(line, "auto-audio-test ", 16) == 0) {
        unsigned int hour = 0u;
        char extra = 0;
        if (sscanf(line + 16, "%u %c", &hour, &extra) != 1 || hour > 23u) {
            puts("[AUTO-AUDIO] Usage: auto-audio-test <hour 0..23>");
        } else if (!sound_enabled || timer.running || alarm_active_index >= 0 ||
                   music_ui_open || !audio_queue_idle(&audio_queue) ||
                   dfplayer_busy() || mp3_play_active() ||
#ifdef CLOCK_ENABLE_A2DP
                   bt_speech_running || bt_timer_running || bt_alarm_running ||
                   routed_test_pending ||
#endif
                   mp3_probe_active() || sd_fat32_stream_active()) {
            puts("[AUTO-AUDIO] Test refused: clock/audio busy");
        } else {
            bool started_on_bt = false;
#ifdef CLOCK_ENABLE_A2DP
            if (auto_voice_jbl_enabled && clock_a2dp_stream_active() &&
                clock_a2dp_current_rate() == 44100u)
                started_on_bt = bt_speech_start((uint8_t)hour, 0u, true);
#endif
            if (started_on_bt) {
                printf("[AUTO-AUDIO] TEST %02u:00 via JBL (scheduler unchanged)\n",
                       hour);
            } else if (speech_enqueue_time(&audio_queue, language, (uint8_t)hour,
                                           0u, exact_hour_phrase)) {
                printf("[AUTO-AUDIO] TEST %02u:00 via LOCAL (scheduler unchanged)\n",
                       hour);
            } else {
                puts("[AUTO-AUDIO] Test failed: local speech queue full");
            }
        }
    } else if (strcmp(line, "auto-audio local") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        auto_voice_jbl_enabled = false;
        settings_mark_changed();
        puts("[AUTO-AUDIO] Hourly VOICE output=LOCAL (Flash autosave; timers/alarms unchanged)");
#else
        puts("[AUTO-AUDIO] Bluetooth disabled; hourly announcements always LOCAL");
#endif
    } else if (strcmp(line, "auto-audio jbl") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        auto_voice_jbl_enabled = true;
        settings_mark_changed();
        printf("[AUTO-AUDIO] Hourly VOICE output=JBL_PREFERRED, stream=%s; "
               "LOCAL fallback (Flash autosave)\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE");
        puts("[AUTO-AUDIO] Chime/half-hour/music stay LOCAL; timer/alarm have independent opt-in");
#else
        puts("[AUTO-AUDIO] Requires Bluetooth A2DP");
#endif
    } else if (strcmp(line, "auto-audio") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUTO-AUDIO] hourly_voice=%s stream=%s "
               "(Flash autosave; other automatic events LOCAL)\n",
               auto_voice_jbl_enabled ? "JBL_PREFERRED" : "LOCAL",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE");
#else
        puts("[AUTO-AUDIO] Hourly voice LOCAL (Bluetooth unavailable)");
#endif
    } else if (strcmp(line, "audio-select local") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        // Explicit switch to LOCAL replays remaining manual speech locally.
        if (bt_speech_running) bt_speech_fallback("SELECT_LOCAL");
        if (routed_test_pending && mp3_play_active()) {
            mp3_play_stop();
            routed_test_pending = false;
            puts("[AUDIO-ROUTE] Switched to LOCAL; stopped current BT test");
        }
#endif
        audio_router_select(&audio_router, AUDIO_OUTPUT_LOCAL);
        settings_mark_changed();
        puts("[AUDIO-ROUTE] Requested LOCAL (audio-route-test uses DFPlayer; normal clock output unchanged)");
#ifdef CLOCK_ENABLE_A2DP
    } else if (strcmp(line, "audio-select jbl") == 0) {
        audio_router_select(&audio_router, AUDIO_OUTPUT_BT_JBL);
        audio_router_bluetooth_state(&audio_router, clock_a2dp_stream_active());
        settings_mark_changed();
        puts("[AUDIO-ROUTE] Requested BT_JBL for manual say and route-test only (alarm-audio separate)");
#endif
    } else if (strncmp(line, "audio-route-test ", 17) == 0) {
        // Explicit test of one queued track through the selected audio output.
        // Automatic alarms and the normal sound queue remain DFPlayer-only.
        unsigned int folder = 0, track = 0;
        char extra = 0;
        if (sscanf(line + 17, "%u %u %c", &folder, &track, &extra) != 2 ||
            folder < 1u || folder > 99u || track < 1u || track > 255u) {
            puts("[AUDIO-ROUTE] Usage: audio-route-test <folder 1..99> <track 1..255>");
        } else if (!sound_enabled || timer.running || alarm_active_index >= 0 ||
                   music_ui_open || mp3_play_active() || mp3_probe_active() ||
#ifdef CLOCK_ENABLE_A2DP
                   bt_speech_running || bt_timer_running || bt_alarm_running ||
#endif
                   sd_fat32_stream_active() || !audio_queue_idle(&audio_queue) ||
                   dfplayer_busy()) {
            puts("[AUDIO-ROUTE] Test refused: sound disabled or clock/audio busy");
        } else {
#ifdef CLOCK_ENABLE_A2DP
            bool ready = clock_a2dp_stream_active() &&
                         clock_a2dp_current_rate() == 44100u;
            audio_router_bluetooth_state(&audio_router, ready);
            bool to_jbl = audio_router.requested == AUDIO_OUTPUT_BT_JBL &&
                          audio_router.effective == AUDIO_OUTPUT_BT_JBL;
            if (to_jbl) {
                char mp3_path[20];
                snprintf(mp3_path, sizeof(mp3_path), "%02u/%03u.mp3", folder, track);
                if (mp3_play_start(mp3_path, clock_a2dp_current_rate())) {
                    routed_test_pending = true;
                    routed_test_folder = (uint8_t)folder;
                    routed_test_track = (uint8_t)track;
                    printf("[AUDIO-ROUTE] TEST BT_JBL %s (automatic events still LOCAL)\n",
                           mp3_path);
                } else {
                    puts("[AUDIO-ROUTE] Bluetooth source failed; replaying test track on LOCAL");
                    if (!audio_queue_enqueue(&audio_queue, (uint8_t)folder, (uint8_t)track))
                        puts("[AUDIO-ROUTE] LOCAL enqueue failed");
                }
            } else
#endif
            {
                if (audio_queue_enqueue(&audio_queue, (uint8_t)folder, (uint8_t)track))
                    printf("[AUDIO-ROUTE] TEST LOCAL %02u/%03u (BT unavailable or not selected)\n",
                           folder, track);
                else puts("[AUDIO-ROUTE] LOCAL enqueue failed");
            }
        }
    } else if (strcmp(line, "audio-route") == 0) {
        printf("[AUDIO-ROUTE] requested=%s effective=%s bluetooth=%s\n",
               audio_router_name(audio_router.requested),
               audio_router_name(audio_router.effective),
               audio_router.bt_connected ? "STREAM_ACTIVE" : "OFFLINE");
        puts("[AUDIO-ROUTE] NOTE: manual say + route-test use this preference; auto/timer/alarm have independent settings");
#ifdef CLOCK_ENABLE_A2DP
        printf("[BT-ALARM] preferred=%s running=%s segment=%u/%u\n",
               alarm_audio_jbl_enabled ? "JBL" : "LOCAL",
               bt_alarm_running ? "YES" : "NO",
               (unsigned)(bt_alarm_running ? bt_alarm_index + 1u : 0u),
               (unsigned)(bt_alarm_running ? bt_alarm_count : 0u));
        printf("[BT-TIMER] preferred=%s running=%s segment=%u/%u\n",
               timer_audio_jbl_enabled ? "JBL" : "LOCAL",
               bt_timer_running ? "YES" : "NO",
               (unsigned)(bt_timer_running ? bt_timer_index + 1u : 0u),
               (unsigned)(bt_timer_running ? bt_timer_count : 0u));
        printf("[BT-SPEECH] running=%s segment=%u/%u\n",
               bt_speech_running ? "YES" : "NO",
               (unsigned)(bt_speech_running ? bt_speech_index + 1u : 0u),
               (unsigned)(bt_speech_running ? bt_speech_count : 0u));
#endif
    } else if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
        puts("[SD] sd-test - initialize/read SD card read-only; sd-info - last detection result");
        puts("[SD] sd-list [folder] - show FAT32 8.3 names; sd-file 07/001.mp3 - read first bytes");
        puts("[SD] sd-stream 07/001.mp3 - nonblocking complete file CRC32; sd-stop - cancel");
        puts("[MP3] mp3-test 07/001.mp3 - experimental decode to RAM (no JBL output)");
        puts("[MP3] mp3-play 07/001.mp3 - full file on JBL; mp3-pause; mp3-resume; mp3-stop; mp3-status");
        puts("[MP3] mp3-pause-test 07/001.mp3 - auto pause at 300ms, auto resume after 1s");
        puts("[AUDIO-ROUTE] audio-select local|jbl; audio-route; audio-route-test 7 1; say 08 25");
        puts("[AUTO-AUDIO] auto-audio jbl|local; auto-audio (only automatic hourly VOICE)");
        puts("[AUTO-AUDIO] auto-audio-test 8 - play simulated 08:00 hourly voice, no RTC change");
        puts("[TIMER-AUDIO] timer-audio jbl|local; timer-audio - timer start/cancel/finish routing");
        puts("[ALARM-AUDIO] alarm-audio jbl|local; alarm-audio; alarm-audio-test 1; alarm-stop");
        puts("[AUDIO-SETTINGS] audio-settings shows persisted preferences; change with audio-select, auto-audio, timer-audio, alarm-audio; save");
        puts("[AUDIO-HEALTH] audio-health shows read-only owner, queue and Bluetooth status");
        puts("[CMD] t <seconds>, start, cancel, status, say [HH MM], lang ru|de, exact on|off, play F T, pause, audio, vol N, sound, menu, flash-info, flash-inspect, flash-tail, autosave-status, quiet-status, mode-status, mode voice|chime, alarm-status, alarm-set HH MM signal|music [track], alarm-config N HH MM signal|music [track], alarm-enable N on|off, alarm-off, alarm-stop, alarm-test HH MM [slot], auto-status, auto-debug, auto-test HH MM, auto on|off, rtc-debug, rtc-reset-stats, save, help");
        puts("[INPUT] ALARM short=alarm editor; ALARM long >=900ms=music player; MUSIC TIMER=play/stop, encoder=track, MINUS=exit");
        puts("[BOOTSEL] bootsel arm -> bootsel confirm within 15s (USB required; refuses during busy/autosave)");
    } else if (strcmp(line, "rtc-debug") == 0) {
        print_rtc_debug();
    } else if (strcmp(line, "rtc-reset-stats") == 0) {
        ds1302_reset_diagnostics();
        puts("[RTC-DEBUG] counters and last failure reset; RTC registers unchanged");
    } else if (strcmp(line, "flash-info") == 0) {
        settings_store_print_info();
    } else if (strcmp(line, "flash-inspect") == 0) {
        settings_store_inspect();
    } else if (strcmp(line, "flash-tail") == 0) {
        settings_store_dump_tail();
    } else if (strcmp(line, "save") == 0) {
        if (alarm_ui_state != ALARM_UI_CLOSED || alarm_active_index >= 0 ||
            music_ui_open || timer.running || !audio_queue_idle(&audio_queue) ||
            dfplayer_busy()
#ifdef CLOCK_ENABLE_A2DP
            || bt_speech_running || bt_timer_running || bt_alarm_running ||
            routed_test_pending || mp3_play_active() || sd_fat32_stream_active()
#endif
           ) {
            puts("[FLASH] save blocked: wait for audio/timer/alarm to finish");
        } else save_settings_now();
    } else if (strcmp(line, "autosave-status") == 0) {
        print_autosave_status(now);
    } else if (strcmp(line, "auto-status") == 0) {
        auto_status();
    } else if (strcmp(line, "alarm-status") == 0) {
        alarm_status();
    } else if (strncmp(line, "alarm-config ", 13) == 0) {
        alarm_config(line + 13);
    } else if (strncmp(line, "alarm-enable ", 13) == 0) {
        alarm_enable(line + 13);
    } else if (strcmp(line, "alarm-off") == 0) {
        if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open) {
            puts("[ALARM] close editor/player before changing alarm 1");
        } else {
            alarms[0].enabled = false;
            settings_mark_changed();
            puts("[ALARM] 1 enabled=OFF");
        }
    } else if (strcmp(line, "alarm-audio") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        printf("[ALARM-AUDIO] preferred=%s BT=%s running=%s (Flash autosave)\n",
               alarm_audio_jbl_enabled ? "JBL" : "LOCAL",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               bt_alarm_running ? "YES" : "NO");
#else
        puts("[ALARM-AUDIO] LOCAL (Bluetooth disabled)");
#endif
    } else if (strcmp(line, "alarm-audio jbl") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        alarm_audio_jbl_enabled = true;
        settings_mark_changed();
        puts("[ALARM-AUDIO] preferred=JBL, LOCAL fallback; RAM-only");
#else
        puts("[ALARM-AUDIO] Bluetooth unavailable");
#endif
    } else if (strcmp(line, "alarm-audio local") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        alarm_audio_jbl_enabled = false;
        settings_mark_changed();
        if (bt_alarm_running) bt_alarm_fallback("SELECT_LOCAL");
#endif
        puts("[ALARM-AUDIO] preferred=LOCAL, Flash autosave");
    } else if (strncmp(line, "alarm-audio-test ", 17) == 0) {
        unsigned int slot = 0u;
        char extra = 0;
        if (sscanf(line + 17, "%u %c", &slot, &extra) != 1 ||
            slot < 1u || slot > CLOCK_ALARM_COUNT) {
            puts("[ALARM-AUDIO] Usage: alarm-audio-test <slot 1..5>");
        } else if (alarm_active_index >= 0 || timer.running ||
                   music_ui_open || alarm_ui_state != ALARM_UI_CLOSED ||
                   !sound_enabled || mp3_play_active() || mp3_probe_active() ||
                   sd_fat32_stream_active() ||
#ifdef CLOCK_ENABLE_A2DP
                   bt_speech_running || bt_timer_running || routed_test_pending ||
#endif
                   !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
            puts("[ALARM-AUDIO] TEST refused: clock/audio busy");
        } else {
            ds1302_datetime_t dt = {0};
            if (!ds1302_read_datetime(&dt))
                puts("[ALARM-AUDIO] TEST refused: RTC unavailable");
            else
                alarm_start_audio(slot - 1u, dt.hour, dt.minute, now, true);
        }
    } else if (strcmp(line, "alarm-stop") == 0) {
        alarm_stop("SERIAL");
    } else if (strncmp(line, "alarm-set ", 10) == 0) {
        alarm_set(line + 10);
    } else if (strncmp(line, "alarm-test", 10) == 0 &&
               (line[10] == ' ' || line[10] == '\0')) {
        alarm_test(line + 10);
    } else if (strcmp(line, "auto-debug") == 0) {
        auto_debug();
    } else if (strcmp(line, "mode-status") == 0) {
        printf("[MODE] clock_mode=%s\n", clock_mode ? "CHIME" : "VOICE");
    } else if (strcmp(line, "mode voice") == 0 || strcmp(line, "mode chime") == 0) {
        set_clock_mode(strcmp(line, "mode chime") == 0 ? 1u : 0u, now);
    } else if (strcmp(line, "auto on") == 0 || strcmp(line, "auto off") == 0) {
        auto_enabled = strcmp(line, "auto on") == 0;
        // Do not replay the current boundary after toggling: a fresh
        // event is eligible only at the next 00/30-minute boundary.
        ds1302_datetime_t current = {0};
        if (ds1302_read_datetime(&current) &&
            (current.minute == 0u || current.minute == 30u)) {
            auto_seen_key_valid = true;
            auto_seen_minute_key = auto_minute_key(&current);
        }
        printf("[AUTO] runtime=%s (not stored in flash; next boundary only)\n",
               auto_enabled ? "ON" : "OFF");
    } else if (strncmp(line, "auto-test", 9) == 0) {
        unsigned int hour = 0, minute = 0;
        char extra = 0;
        if (sscanf(line + 9, " %u %u %c", &hour, &minute, &extra) != 2 ||
            hour > 23u || minute > 59u) {
            puts("[AUTO-TEST] Use auto-test <hour 0..23> <minute 0..59>");
        } else {
            auto_test((uint8_t)hour, (uint8_t)minute);
        }
    } else if (strcmp(line, "quiet-status") == 0) {
        printf("[QUIET] enabled=%s start=%02u end=%02u half_hour=%s auto_runtime=%s\n",
               quiet_enabled ? "ON" : "OFF", (unsigned)quiet_start,
               (unsigned)quiet_end, half_hour_enabled ? "ON" : "OFF",
                auto_enabled ? "ON" : "OFF");
    } else if (strcmp(line, "menu") == 0) {
        if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
            music_ui_open) {
            puts("[MENU] blocked: alarm ringing or music/alarm editor open");
        } else if (menu_state != MENU_CLOSED) menu_close();
        else menu_open(now);
    } else if (strcmp(line, "start") == 0) {
        if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
            music_ui_open) {
            puts("[TIMER] start blocked: alarm ringing or UI open");
        } else {
            if (menu_state != MENU_CLOSED) menu_close();
            timer_start_now(now);
            print_timer_status(now);
        }
    } else if (strcmp(line, "cancel") == 0) {
        timer_cancel_now();
        print_timer_status(now);
    } else if (strcmp(line, "status") == 0) {
        print_timer_status(now);
    } else if (strcmp(line, "audio") == 0) {
        printf("[AUDIO] DFPlayer BUSY=%s queue=%u idle=%s\n",
               dfplayer_busy() ? "LOW (playing)" : "HIGH (idle)",
               (unsigned)audio_queue.size,
               audio_queue_idle(&audio_queue) ? "yes" : "no");
        printf("[AUDIO] volume=%u/30 sound=%s\n", (unsigned)volume, sound_enabled ? "ON" : "OFF");
    } else if (strcmp(line, "sound") == 0) {
        toggle_sound();
    } else if (strcmp(line, "say") == 0) {
        announce_rtc_time();
    } else if (strncmp(line, "say", 3) == 0 && isspace((unsigned char)line[3])) {
        unsigned int hour = 0, minute = 0;
        char extra = 0;
        if (sscanf(line + 3, " %u %u %c", &hour, &minute, &extra) != 2 ||
            hour > 23u || minute > 59u) {
            puts("[SPEECH] Use say or say <hour 0..23> <minute 0..59>");
        } else {
            announce_time((uint8_t)hour, (uint8_t)minute);
        }
    } else if (strcmp(line, "lang") == 0) {
        printf("[SPEECH] language=%s\n", speech_language_name(language));
    } else if (strcmp(line, "lang ru") == 0 || strcmp(line, "lang de") == 0) {
        speech_language_t previous = language;
        language = line[5] == 'd' ? SPEECH_DE : SPEECH_RU;
        if (previous != language) settings_mark_changed();
        if (menu_state == MENU_EDIT_LANGUAGE) menu_language_choice = language;
        printf("[SPEECH] language=%s\n", speech_language_name(language));
    } else if (strcmp(line, "exact on") == 0 || strcmp(line, "exact off") == 0) {
        bool previous = exact_hour_phrase;
        exact_hour_phrase = line[6] == 'o' && line[7] == 'n';
        if (previous != exact_hour_phrase) settings_mark_changed();
        printf("[SPEECH] exact_hour=%s\n", exact_hour_phrase ? "ON" : "OFF");
    } else if (strncmp(line, "vol", 3) == 0 && isspace((unsigned char)line[3])) {
        unsigned int requested = 0;
        char extra = 0;
        if (sscanf(line + 3, " %u %c", &requested, &extra) != 1 || requested > 30) {
            puts("[AUDIO] Use vol <0..30>");
        } else {
            if (volume != (uint8_t)requested) {
                volume = (uint8_t)requested;
                settings_mark_changed();
            }
            audio_queue_set_volume(&audio_queue, volume);
            volume_overlay_until_ms = now + VOLUME_OVERLAY_MS;
            printf("[AUDIO] volume=%u/30\n", (unsigned)volume);
        }
    } else if (strcmp(line, "pause") == 0) {
        if (music_ui_open) {
            music_ui_stop("SERIAL_PAUSE");
        } else if (alarm_active_index >= 0) {
            puts("[AUDIO] pause blocked: use alarm-stop");
        } else {
            audio_queue_clear(&audio_queue, true);
            puts("[AUDIO] Pause requested");
        }
    } else if (strncmp(line, "play", 4) == 0 && isspace((unsigned char)line[4])) {
        unsigned int folder = 0, track = 0;
        char extra = 0;
        if (sscanf(line + 4, " %u %u %c", &folder, &track, &extra) != 2 ||
            folder < 1 || folder > 99 || track < 1 || track > 255) {
            puts("[AUDIO] Use play <folder 1..99> <track 1..255>");
        } else if (!sound_enabled) {
            puts("[AUDIO] Sound OFF; press volume button to enable");
        } else if (alarm_active_index >= 0 || music_ui_open ||
                   alarm_ui_state != ALARM_UI_CLOSED) {
            puts("[AUDIO] play blocked: alarm/editor/music active");
        } else if (audio_queue_enqueue(&audio_queue, (uint8_t)folder, (uint8_t)track)) {
            printf("[AUDIO] Queued folder=%u track=%u\n", folder, track);
        } else {
            puts("[AUDIO] Queue full");
        }
    } else if (line[0] == 't' && isspace((unsigned char)line[1])) {
        char *arg = line + 2;
        while (isspace((unsigned char)*arg)) ++arg;
        errno = 0;
        char *end = NULL;
        unsigned long requested = strtoul(arg, &end, 10);
        while (end && isspace((unsigned char)*end)) ++end;
        if (!*arg || end == arg || !end || *end || errno == ERANGE || requested > 14400ul || requested < 1ul) {
            puts("[CMD] Invalid duration. Use: t 10 (1..14400 seconds)");
        } else if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open) {
            puts("[TIMER] duration change blocked: editor/music open");
        } else {
            countdown_set_duration(&timer, (uint32_t)requested);
            if (!timer.running) timer_screen = true;
            finished_screen_until_ms = 0;
            printf("[TIMER] duration set to %lus (start to begin)\n", requested);
        }
    } else if (*line) {
        printf("[CMD] Unknown command: %s (help for options)\n", line);
    }
}

static void poll_serial(void) {
    int ch;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            if (command_length) {
                command_line[command_length] = 0;
                handle_command(command_line);
                command_length = 0;
            }
        } else if (ch == 8 || ch == 127) {
            if (command_length) --command_length;
        } else if (ch >= 32 && ch < 127) {
            if (command_length < sizeof(command_line) - 1) {
                command_line[command_length++] = (char)ch;
            } else {
                command_length = 0;
                puts("[CMD] Input too long");
            }
        }
    }
}


static void on_control_event(const control_event_t *event, void *context) {
    (void)context;
    uint64_t now = to_ms_since_boot(get_absolute_time());
    if (event->type == CONTROL_VOLUME_ROTATE) {
        printf("[INPUT] encoder VOLUME %s\n", event->direction > 0 ? "CW" : "CCW");
        change_volume(event->direction);
    } else if (event->type == CONTROL_TIMER_ROTATE) {
        printf("[INPUT] encoder TIMER %s\n", event->direction > 0 ? "CW" : "CCW");
        if (music_ui_open) {
            music_ui_rotate(event->direction);
            return;
        }
        if (alarm_ui_state != ALARM_UI_CLOSED) {
            alarm_ui_rotate(event->direction, now);
            return;
        }
        if (menu_state != MENU_CLOSED) {
            menu_rotate(event->direction, now);
            return;
        }
        // One minute per encoder event; don't edit a running timer.
        if (!timer.running) {
            int32_t duration = (int32_t)timer.duration_seconds + (int32_t)event->direction * 60;
            if (duration < 1) duration = 1;
            if (duration > 14400) duration = 14400;
            countdown_set_duration(&timer, (uint32_t)duration);
            timer_screen = true;
            finished_screen_until_ms = 0;
            printf("[TIMER] selected=%lus\n", (unsigned long)timer.duration_seconds);
        }
    } else {
        printf("[INPUT] button %s %s\n",
               controls_button_name(event->button),
               event->type == CONTROL_BUTTON_PRESS ? "PRESS" : "RELEASE");
        // Press/release separation prevents a long ALARM hold from
        // briefly opening the short-press alarm editor.
        if (event->button == BUTTON_ALARM) {
            if (event->type == CONTROL_BUTTON_PRESS) {
                alarm_button_pressed = true;
                alarm_button_pressed_at_ms = now;
                alarm_button_consumed = alarm_active_index >= 0;
                if (alarm_active_index >= 0) alarm_stop("BUTTON");
            } else if (event->type == CONTROL_BUTTON_RELEASE &&
                       alarm_button_pressed) {
                alarm_button_pressed = false;
                if (!alarm_button_consumed && alarm_active_index < 0) {
                    if (music_ui_open) {
                        music_ui_close("ALARM_BUTTON");
                    } else if (now - alarm_button_pressed_at_ms >=
                               ALARM_LONG_PRESS_MS) {
                        if (alarm_ui_state == ALARM_UI_CLOSED) music_ui_enter();
                        else alarm_ui_close("ALARM_LONG");
                    } else if (alarm_ui_state != ALARM_UI_CLOSED) {
                        alarm_ui_close("ALARM_SHORT");
                    } else if (menu_state == MENU_CLOSED) {
                        alarm_ui_open(now);
                    }
                }
                alarm_button_consumed = false;
            }
            return;
        }
        // During ringing, only ALARM stop and VOLUME remain interactive.
        if (alarm_active_index >= 0) return;
        if (music_ui_open) {
            if (event->type == CONTROL_BUTTON_PRESS) {
                if (event->button == BUTTON_TIMER ||
                    event->button == BUTTON_SETTINGS) music_ui_toggle();
                else if (event->button == BUTTON_MINUS) music_ui_close("MINUS");
            }
            return;
        }
        if (alarm_ui_state != ALARM_UI_CLOSED) {
            if (event->type == CONTROL_BUTTON_PRESS) {
                if (event->button == BUTTON_SETTINGS &&
                    alarm_ui_state == ALARM_UI_TRACK) {
                    alarm_ui_last_input_ms = now;
                    alarm_preview_toggle();
                } else if (event->button == BUTTON_TIMER ||
                           event->button == BUTTON_SETTINGS) alarm_ui_next(now);
                else if (event->button == BUTTON_MINUS) alarm_ui_back(now);
            }
            return;
        }
        if (event->type == CONTROL_BUTTON_PRESS && event->button == BUTTON_SETTINGS) {
            if (menu_state == MENU_CLOSED) menu_open(now);
            else if (menu_state == MENU_LIST) menu_begin_edit(now);
            else menu_commit(now);
            return;
        }
        if (event->type == CONTROL_BUTTON_PRESS && event->button == BUTTON_MINUS &&
            menu_state != MENU_CLOSED) {
            menu_back(now);
            return;
        }
        if (event->type == CONTROL_BUTTON_PRESS && event->button == BUTTON_TIMER &&
            menu_state != MENU_CLOSED) {
            if (menu_state == MENU_LIST) menu_begin_edit(now);
            else menu_commit(now);
            return;
        }
        if (event->button == BUTTON_MODE) {
            // MicroPython: short ST/MO switches voice/chime; long speaks time.
            if (event->type == CONTROL_BUTTON_PRESS) {
                mode_press_active = true;
                mode_pressed_at_ms = now;
            } else if (event->type == CONTROL_BUTTON_RELEASE && mode_press_active) {
                mode_press_active = false;
                if (menu_state == MENU_CLOSED && !timer.running) {
                    if (now - mode_pressed_at_ms >= 900u) {
                        announce_rtc_time();
                    } else {
                        set_clock_mode(clock_mode ? 0u : 1u, now);
                    }
                }
            }
            return;
        }
        if (event->button == BUTTON_VOLUME && event->type == CONTROL_BUTTON_PRESS) {
            toggle_sound();
        } else if (event->button == BUTTON_TIMER && event->type == CONTROL_BUTTON_PRESS) {
            if (timer.running) {
                timer_cancel_now();
                puts("[TIMER] CANCELLED by button");
            } else {
                timer_start_now(now);
                puts("[TIMER] STARTED by button");
                print_timer_status(now);
            }
        }
    }
}

int main(void) {
    stdio_init_all();
    sleep_ms(3000); // Give VS Code Serial Monitor time to attach.
    printf("\n[BOOT] Speaking Clock C bring-up 0.1 (Pico 2 W)\n");
#ifdef CLOCK_ENABLE_A2DP
    puts("[BOOT] EXPERIMENTAL A2DP active; LOCAL DFPlayer still primary output");
    int bt_error = cyw43_arch_init();
    printf("[BT] cyw43_arch_init=%d\n", bt_error);
    if (bt_error == PICO_OK) {
        int result = btstack_main(0, NULL);
        printf("[BT] btstack_main=%d\n", result);
    } else {
        puts("[BT] radio init failed, clock continues without remote audio");
    }
#else
    printf("[BOOT] DS1302 manual datetime writes only; LCD1602 scan; no Bluetooth\n");
#endif

    i2c_init(i2c0, 400 * 1000);
    gpio_set_function(LCD_SDA_GPIO, GPIO_FUNC_I2C);
    gpio_set_function(LCD_SCL_GPIO, GPIO_FUNC_I2C);
    gpio_pull_up(LCD_SDA_GPIO);
    gpio_pull_up(LCD_SCL_GPIO);

    int address = detect_lcd_address();
    bool lcd_ok = address >= 0 && lcd1602_init(i2c0, (uint8_t)address);
    if (address < 0) printf("[LCD] No PCF8574 detected on I2C0 (GP0/GP1)\n");
    else printf("[LCD] PCF8574 at 0x%02x; init %s\n", address, lcd_ok ? "OK" : "FAILED");

    ds1302_init(DS1302_CLK_GPIO, DS1302_DIO_GPIO, DS1302_CE_GPIO);
    printf("[RTC] DS1302 CLK=GP2 DIO=GP5 CE=GP4\n");

    controls_init();
    puts("[INPUT] GPIO controls initialized (2 encoders, 7 buttons)");
    dfplayer_init();
    audio_queue_init(&audio_queue);
    audio_router_init(&audio_router);
    alarm_defaults(alarms);
    alarm_scheduler_init(&alarm_scheduler);
    load_settings_at_boot();
    audio_queue_set_volume(&audio_queue, volume);
    puts("[AUDIO] DFPlayer UART0 GP16/17 BUSY GP18");
    puts("[AUTO] runtime=ON after boot; first observed RTC boundary will be consumed");
    countdown_init(&timer);
    puts("[ALARM] five slots loaded from flash (v1/v2 default OFF); confirm with alarm-status");
    puts("[CMD] Serial timer ready. Type help + Enter");
    char last_top[17] = {0};
    char last_bottom[17] = {0};
    char rtc_top[17] = "RTC unavailable";
    char rtc_bottom[17] = "Check GP2/5/4";
    uint64_t next_rtc_ms = 0;
    uint32_t previous_remaining = UINT32_MAX;
    while (true) {
#ifdef CLOCK_ENABLE_A2DP
        // Synchronize with actual media-stream status, including reconnect and loss.
        // Only the configured JBL test endpoint is supported in this stage.
        bool bt_ready = clock_a2dp_stream_active();
        if (audio_router.requested == AUDIO_OUTPUT_BT_JBL &&
            audio_router.bt_connected != bt_ready) {
            audio_router_bluetooth_state(&audio_router, bt_ready);
            printf("[AUDIO-ROUTE] bluetooth=%s effective=%s (test routing only)\n",
                   bt_ready ? "STREAM_ACTIVE" : "OFFLINE",
                   audio_router_name(audio_router.effective));
        }
#endif
        poll_serial();
        if (bootsel_armed &&
            to_ms_since_boot(get_absolute_time()) - bootsel_armed_ms >
                BOOTSEL_CONFIRM_WINDOW_MS) {
            bootsel_armed = false;
            puts("[BOOTSEL] confirmation window expired");
        }
        audio_queue_service(&audio_queue, to_ms_since_boot(get_absolute_time()));
        controls_poll(to_ms_since_boot(get_absolute_time()), on_control_event, NULL);
        uint64_t now = to_ms_since_boot(get_absolute_time());
        countdown_service(&timer, now);
        // countdown_service clears timer.running at zero; the finish
        // event and its audio must take priority over flash writes.
        if (countdown_consume_finished(&timer)) {
            puts("[TIMER] FINISHED");
            timer_audio_finished();
            timer_screen = false;
            finished_screen_until_ms = now + 3000u;
        }
        if (menu_state != MENU_CLOSED &&
            now - menu_last_input_ms >= SETTINGS_MENU_TIMEOUT_MS) {
            puts("[MENU] Timeout (20s); unsaved edit discarded");
            menu_close();
        }
        if (alarm_ui_state != ALARM_UI_CLOSED &&
            now - alarm_ui_last_input_ms >= SETTINGS_MENU_TIMEOUT_MS) {
            alarm_ui_close("TIMEOUT: draft discarded");
        }
        alarm_service_playback(now);
        music_ui_service();
        // One sector of read-only SD diagnostics per loop; do not decode in audio callbacks.
        sd_fat32_stream_service();
        // Test-route reliability: on Bluetooth loss, replay the current
        // test track locally, unless a higher-priority clock event intervened.
#ifdef CLOCK_ENABLE_A2DP
        if (routed_test_pending && mp3_play_active() &&
            !clock_a2dp_stream_active() &&
            !timer.running && alarm_active_index < 0 &&
            !music_ui_open && sound_enabled &&
            audio_queue_idle(&audio_queue)) {
            uint8_t folder = routed_test_folder, track = routed_test_track;
            mp3_play_stop();
            routed_test_pending = false;
            if (audio_queue_enqueue(&audio_queue, folder, track))
                printf("[AUDIO-ROUTE] BT lost; LOCAL replay folder=%02u track=%03u\n",
                       folder, track);
            else puts("[AUDIO-ROUTE] FAIL: local fallback queue full");
        }
        // Keep test fallback armed until the MP3 is truly complete.
        if (routed_test_pending && !mp3_play_active())
            routed_test_pending = false;
#endif
#ifdef CLOCK_ENABLE_A2DP
        // Bluetooth timer is an independent owner: allow timer.running while
        // its own start announcement plays. The upcoming generic MP3 guard
        // must not accidentally cancel this playback.
        bt_alarm_service();
        bt_timer_service();
        bt_speech_service();
#endif
        // Abort experimental playback when a higher-priority clock event needs audio.
        if (mp3_play_active() &&
            ((timer.running
#ifdef CLOCK_ENABLE_A2DP
              && !bt_timer_running
#endif
             ) || (alarm_active_index >= 0
#ifdef CLOCK_ENABLE_A2DP
              && !bt_alarm_running
#endif
             ) || music_ui_open ||
             !sound_enabled || !audio_queue_idle(&audio_queue)
#ifdef CLOCK_ENABLE_A2DP
             || !clock_a2dp_stream_active()
#endif
            )) {
            mp3_play_stop();
            puts("[MP3-PLAY] Interrupted by clock state/audio priority");
        }
        mp3_probe_service();
#ifdef CLOCK_ENABLE_A2DP
        if (routed_test_pending && !mp3_play_active())
            routed_test_pending = false; // ordinary EOF, no local replay
#endif
        autosave_service(now);
        if (timer.running) {
            uint32_t left = countdown_remaining(&timer, now);
            if (left != previous_remaining) {
                printf("[TIMER] remaining=%lus\n", (unsigned long)left);
                previous_remaining = left;
            }
        } else {
            previous_remaining = UINT32_MAX;
        }
        if (now >= next_rtc_ms) {
            next_rtc_ms = now + 1000u;
            ds1302_datetime_t dt = {0};
            if (ds1302_read_datetime(&dt)) {
                snprintf(rtc_top, sizeof(rtc_top), "%02u:%02u:%02u",
                         (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
                snprintf(rtc_bottom, sizeof(rtc_bottom), "%02u.%02u.%04u",
                         (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year);
                printf("[RTC] %s %s\n", rtc_top, rtc_bottom);
                alarm_service_rtc(&dt, now);
                auto_service_rtc(&dt);
            } else {
                snprintf(rtc_top, sizeof(rtc_top), "RTC unavailable");
                snprintf(rtc_bottom, sizeof(rtc_bottom), "Check GP2/5/4");
                // RTC is polled once a second, but missing RTC doesn't stop controls.
                puts("[RTC] Invalid/unavailable (run rtc-debug for raw bytes and error reasons)");
            }
        }

        char top[17], bottom[17];
        if (music_ui_open) {
            snprintf(top, sizeof(top), "MUSIC PLAYER");
            snprintf(bottom, sizeof(bottom), "T%02u %s",
                     (unsigned)music_ui_track,
                     music_ui_playing ? "PLAY" : "STOP");
        } else if (alarm_ui_state == ALARM_UI_LIST) {
            const alarm_entry_t *a = &alarms[alarm_ui_index];
            snprintf(top, sizeof(top), "ALARM %u/5 %s",
                     (unsigned)(alarm_ui_index + 1u), a->enabled ? "ON" : "OFF");
            snprintf(bottom, sizeof(bottom), "%02u:%02u %s",
                     (unsigned)a->hour, (unsigned)a->minute,
                     a->sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL");
        } else if (alarm_ui_state != ALARM_UI_CLOSED) {
            const char *field = alarm_ui_state == ALARM_UI_ENABLED ? "ON/OFF" :
                                alarm_ui_state == ALARM_UI_HOUR ? "HOUR" :
                                alarm_ui_state == ALARM_UI_MINUTE ? "MINUTE" :
                                alarm_ui_state == ALARM_UI_SOUND ? "SOUND" : "TRACK";
            snprintf(top, sizeof(top), "ALARM %u %s",
                     (unsigned)(alarm_ui_index + 1u), field);
            if (alarm_ui_state == ALARM_UI_ENABLED)
                snprintf(bottom, sizeof(bottom), "%s PRESS=NEXT",
                         alarm_ui_draft.enabled ? "ON" : "OFF");
            else if (alarm_ui_state == ALARM_UI_HOUR ||
                     alarm_ui_state == ALARM_UI_MINUTE)
                snprintf(bottom, sizeof(bottom), "%02u:%02u PRESS=NEXT",
                         (unsigned)alarm_ui_draft.hour,
                         (unsigned)alarm_ui_draft.minute);
            else if (alarm_ui_state == ALARM_UI_SOUND)
                snprintf(bottom, sizeof(bottom), "%s NEXT",
                         alarm_ui_draft.sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL");
            else
                snprintf(bottom, sizeof(bottom), "%u SET=PREV T=OK",
                         (unsigned)alarm_ui_draft.track);
        } else if (menu_state == MENU_LIST) {
            snprintf(top, sizeof(top), "SETTINGS %u/%u",
                     (unsigned)(menu_index + 1u), (unsigned)SETTINGS_MENU_ITEMS);
            snprintf(bottom, sizeof(bottom), "%s", menu_item_name(menu_index));
        } else if (menu_state == MENU_EDIT_LANGUAGE) {
            snprintf(top, sizeof(top), "LANGUAGE SETTING");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_language_choice == SPEECH_RU ? "RU" : "DE");
        } else if (menu_is_time_edit()) {
            snprintf(top, sizeof(top), "TIME SET %s",
                     menu_state == MENU_EDIT_TIME_H ? "HOUR" :
                     menu_state == MENU_EDIT_TIME_M ? "MIN" : "SEC");
            snprintf(bottom, sizeof(bottom), "%02u:%02u:%02u",
                     (unsigned)menu_rtc_choice.hour, (unsigned)menu_rtc_choice.minute,
                     (unsigned)menu_rtc_choice.second);
        } else if (menu_is_date_edit()) {
            snprintf(top, sizeof(top), "DATE SET %s",
                     menu_state == MENU_EDIT_DATE_D ? "DAY" :
                     menu_state == MENU_EDIT_DATE_M ? "MONTH" : "YEAR");
            snprintf(bottom, sizeof(bottom), "%02u.%02u.%04u",
                     (unsigned)menu_rtc_choice.day, (unsigned)menu_rtc_choice.month,
                     (unsigned)menu_rtc_choice.year);
        } else if (menu_state == MENU_EDIT_VOLUME) {
            snprintf(top, sizeof(top), "SET VOLUME");
            snprintf(bottom, sizeof(bottom), "%u / 30 PRESS=OK",
                     (unsigned)menu_volume_choice);
        } else if (menu_state == MENU_EDIT_QUIET_ENABLED) {
            snprintf(top, sizeof(top), "QUIET MODE");
            snprintf(bottom, sizeof(bottom), "%s PRESS=NEXT",
                     menu_quiet_enabled_choice ? "ON" : "OFF");
        } else if (menu_state == MENU_EDIT_QUIET_START) {
            snprintf(top, sizeof(top), "QUIET START");
            snprintf(bottom, sizeof(bottom), "%02u:00 PRESS=NEXT",
                     (unsigned)menu_quiet_start_choice);
        } else if (menu_state == MENU_EDIT_QUIET_END) {
            snprintf(top, sizeof(top), "QUIET END");
            snprintf(bottom, sizeof(bottom), "%02u:00 PRESS=SAVE",
                     (unsigned)menu_quiet_end_choice);
        } else if (menu_state == MENU_EDIT_HALF_HOUR) {
            snprintf(top, sizeof(top), "HALF-HOUR BEEP");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_half_hour_choice ? "ON" : "OFF");
        } else if (menu_state == MENU_EDIT_EXACT) {
            snprintf(top, sizeof(top), "EXACT HOUR WORD");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_exact_choice ? "ON" : "OFF");
        } else if (alarm_active_index >= 0) {
            snprintf(top, sizeof(top), "ALARM %d RINGING", alarm_active_index + 1);
            snprintf(bottom, sizeof(bottom), "ALARM BTN=STOP");
        } else if (mode_overlay_until_ms && now < mode_overlay_until_ms) {
            snprintf(top, sizeof(top), "CLOCK MODE");
            snprintf(bottom, sizeof(bottom), "%s", clock_mode ? "CHIME" : "VOICE");
        } else if (speech_overlay_until_ms && now < speech_overlay_until_ms) {
            snprintf(top, sizeof(top), "SPEAK TIME %s", speech_language_name(language));
            snprintf(bottom, sizeof(bottom), "%s", speech_overlay_time);
        } else if (volume_overlay_until_ms && now < volume_overlay_until_ms) {
            snprintf(top, sizeof(top), "VOLUME %s", sound_enabled ? "ON" : "OFF");
            snprintf(bottom, sizeof(bottom), "%u / 30", (unsigned)volume);
        } else if (finished_screen_until_ms && now < finished_screen_until_ms) {
            snprintf(top, sizeof(top), "TIMER FINISHED");
            snprintf(bottom, sizeof(bottom), "Ready");
        } else if (timer.running || timer_screen) {
            uint32_t remaining = timer.running
                ? countdown_remaining(&timer, now)
                : timer.duration_seconds;
            snprintf(top, sizeof(top), "%s", timer.running ? "TIMER RUNNING" : "TIMER SET");
            snprintf(bottom, sizeof(bottom), "%02lu:%02lu:%02lu",
                     (unsigned long)(remaining / 3600u),
                     (unsigned long)((remaining / 60u) % 60u),
                     (unsigned long)(remaining % 60u));
        } else {
            snprintf(top, sizeof(top), "%s", rtc_top);
            snprintf(bottom, sizeof(bottom), "%s", rtc_bottom);
        }

        if (lcd_ok && strcmp(top, last_top) != 0) {
            lcd1602_write_line(0, top);
            snprintf(last_top, sizeof(last_top), "%s", top);
        }
        if (lcd_ok && strcmp(bottom, last_bottom) != 0) {
            lcd1602_write_line(1, bottom);
            snprintf(last_bottom, sizeof(last_bottom), "%s", bottom);
        }
        sleep_ms(1);
    }
}
