// Speaking-clock C firmware with protected flash settings and optional A2DP/MP3.
// DS1302 RTC supports explicit manual time/date writes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "ds1302.h"
#include "lcd1602_i2c.h"
#include "countdown.h"
#include "timer_controls.h"
#include "music_playlist.h"
#include "music_sleep.h"
#include "birthday.h"
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
#include "clock_time.h"
#include "firmware_version.h"
#if defined(CLOCK_ENABLE_A2DP) || defined(CLOCK_ENABLE_WIFI)
#include "pico/cyw43_arch.h"
static bool radio_ready;
#endif
#ifdef CLOCK_ENABLE_WIFI
#include "network_time.h"
#endif
#ifdef CLOCK_ENABLE_PHONE_WEB
#include "phone_web.h"
#endif
#ifdef CLOCK_ENABLE_A2DP
#include "pico/cyw43_arch.h"
#include "clock_a2dp_control.h"
#include "bt_catalog.h"
int btstack_main(int argc, const char *argv[]);
#endif

#define LCD_SDA_GPIO 0
#define LCD_SCL_GPIO 1
#define DS1302_CLK_GPIO 2
#define DS1302_DIO_GPIO 5
#define DS1302_CE_GPIO 4
#define DISPLAY_LIGHT_GPIO 6
#define DISPLAY_LIGHT_PWM_COUNTS 1000u

static display_light_t display_light = {
    DISPLAY_LIGHT_OFF, DISPLAY_LIGHT_DAY_DEFAULT, DISPLAY_LIGHT_NIGHT_DEFAULT
};
static uint8_t display_light_hour = 24u; // RTC unavailable until the first valid read.
static uint8_t display_light_applied;

static void display_light_init(void) {
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, DISPLAY_LIGHT_PWM_COUNTS - 1u);
    pwm_config_set_clkdiv(&config, 125.0f); // About 1.2 kHz at the Pico 2 W default clock.
    pwm_init(pwm_gpio_to_slice_num(DISPLAY_LIGHT_GPIO), &config, true);
    gpio_set_function(DISPLAY_LIGHT_GPIO, GPIO_FUNC_PWM);
}

static void display_light_service(void) {
    uint8_t level = display_light_level(display_light, display_light_hour);
    if (level == display_light_applied) return;
    pwm_set_gpio_level(DISPLAY_LIGHT_GPIO, level * (DISPLAY_LIGHT_PWM_COUNTS / 100u));
    display_light_applied = level;
}

static const char *display_light_mode_name(uint8_t mode) {
    static const char *const names[] = {"OFF", "ON", "AUTO"};
    return names[mode];
}

static void display_light_status(void) {
    printf("[DISPLAY-LIGHT] mode=%s day=%u%% night=%u%% applied=%u%%\n",
           display_light_mode_name(display_light.mode), (unsigned)display_light.day_percent,
           (unsigned)display_light.night_percent, (unsigned)display_light_applied);
}

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
static timer_editor_t timer_editor;
static timer_acceleration_t timer_acceleration;
// Five calendar alarms; legacy v1/v2 loads disabled defaults.
// v3 persists alarm entries alongside existing settings.
static alarm_entry_t alarms[CLOCK_ALARM_COUNT];
static alarm_scheduler_t alarm_scheduler;
static bool alarm_boot_seeded;
static int alarm_active_index = -1;
static uint64_t alarm_deadline_ms;
static bool alarm_audio_armed;
#define ALARM_SAFETY_TIMEOUT_MS (15u * 60u * 1000u)
// Runtime-only pending alarms: timer sound always plays first, alarm follows
// when the output is idle. Do not extend an overdue alarm beyond 30 minutes.
#define ALARM_DEFER_MAX_MS (30u * 60u * 1000u)
typedef struct {
    bool pending;
    bool test;
    bool snoozed;
    uint8_t hour;
    uint8_t minute;
    uint64_t queued_ms;
} deferred_alarm_t;
static deferred_alarm_t deferred_alarms[CLOCK_ALARM_COUNT];
static alarm_snooze_t alarm_snoozes[CLOCK_ALARM_COUNT];
static uint8_t active_alarm_hour, active_alarm_minute;
static bool active_alarm_test;
static uint64_t active_alarm_due_ms;
static void alarm_stop(const char *reason);
static void alarm_consume_current_minute(unsigned index);
static void alarm_preview_stop(const char *reason);
static void alarm_start_audio(unsigned index, uint8_t hour, uint8_t minute,
                              uint64_t now, bool test);
static void alarm_pending_service(uint64_t now);
static void music_ui_close(const char *reason);
static void music_ui_stop(const char *reason);
static void birthday_apply(unsigned index, birthday_t draft);
static char command_line[160];
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
static audio_track_t bt_speech_tracks[AUDIO_QUEUE_CAPACITY];
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
// Save selected Bluetooth sink MAC even in BT-free firmware builds.
static uint8_t saved_bt_target_addr[6];
static char saved_wifi_ssid[33], saved_wifi_password[65];
#ifndef CLOCK_ENABLE_PHONE_WEB
static phone_settings_t saved_phone;
#endif
static uint64_t rtc_replay_guard_until;
static uint8_t bt_known_count;
static clock_bt_known_t bt_known[CLOCK_BT_KNOWN_MAX];
#ifdef CLOCK_ENABLE_A2DP
static uint8_t bt_pending_target_addr[6];
static char bt_pending_target_name[CLOCK_BT_NAME_SIZE];
static bool bt_pending_target_valid, bt_pending_seen_disconnected;
#endif
static uint8_t volume = 10; // Local DFPlayer 0..30.
static uint8_t bt_volume = 16; // Independent Bluetooth output 0..30.
static bool volume_overlay_bluetooth;
static bool bt_volume_remote_synced;
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
    MENU_EDIT_QUIET_ENABLED,
    MENU_EDIT_QUIET_START,
    MENU_EDIT_QUIET_END,
    MENU_EDIT_HALF_HOUR,
    MENU_EDIT_EXACT,
    MENU_AUDIO_LIST,
    MENU_EDIT_AUDIO_OUTPUT,
    MENU_BT_LIST,
    MENU_BT_SCANNING,
    MENU_BT_CONFIRM,
    MENU_BIRTHDAY_LIST,
    MENU_BIRTHDAY_ENABLED,
    MENU_BIRTHDAY_MONTH,
    MENU_BIRTHDAY_DAY,
    MENU_BIRTHDAY_HOUR,
    MENU_BIRTHDAY_MINUTE,
    MENU_ABOUT,
    MENU_DISPLAY_LIGHT_MODE,
    MENU_DISPLAY_LIGHT_DAY,
    MENU_DISPLAY_LIGHT_NIGHT,
#ifdef CLOCK_ENABLE_PHONE_WEB
    MENU_PHONE_WEB,
    MENU_PHONE_PAIR_CODE,
    MENU_PHONE_CONFIRM,
#endif
    MENU_TIMER_EDITOR
} menu_state_t;
static menu_state_t menu_state;
static uint8_t menu_index;
static bool menu_about_version;
static display_light_t display_light_draft;
static speech_language_t menu_language_choice;
static uint8_t menu_audio_index; // 0=manual 1=hourly 2=timer 3=alarm
static bool menu_audio_jbl_choice; // draft, committed on button confirmation
static uint8_t menu_bt_index; // 0=scan, 1..N=discovered devices
static bool menu_exact_choice;
static bool menu_quiet_enabled_choice;
static uint8_t menu_quiet_start_choice;
static uint8_t menu_quiet_end_choice;
static bool menu_half_hour_choice;
static ds1302_datetime_t menu_rtc_choice;
static uint64_t menu_last_input_ms;
#define SETTINGS_MENU_TIMEOUT_MS 20000u
#ifdef CLOCK_ENABLE_PHONE_WEB
#define SETTINGS_MENU_ITEMS 12u
#define PHONE_ACCESS_PAGE 2u
#define PHONE_TRUST_PAGE 3u
#define PHONE_FORGET_PAGE 4u
#define PHONE_ROTATE_PAGE 5u
#define PHONE_PASSWORD_PAGE 6u
#define PHONE_PASSWORD_TIMEOUT_MS (SETTINGS_MENU_TIMEOUT_MS + 30000u)
static uint8_t phone_menu_page;
#else
#define SETTINGS_MENU_ITEMS 11u
#endif
#define SETTINGS_AUDIO_ITEMS 4u
// Alarm editor is separate from settings; changes stay in draft until DONE.
typedef enum {
    ALARM_UI_CLOSED = 0,
    ALARM_UI_LIST,
    ALARM_UI_ENABLED,
    ALARM_UI_HOUR,
    ALARM_UI_MINUTE,
    ALARM_UI_DAYS,
    ALARM_UI_DAY,
    ALARM_UI_SOUND,
    ALARM_UI_TRACK
} alarm_ui_state_t;
static alarm_ui_state_t alarm_ui_state;
static uint8_t alarm_ui_index;
static uint8_t alarm_ui_days_choice, alarm_ui_day_index, alarm_ui_custom_days;
static bool alarm_ui_days_invalid;
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
static bool music_ui_paused;
static music_sleep_t music_sleep;
// Output follows current manual LOCAL / BT sink preference. BT can only
// read music files present on the separate SPI1 FAT32 card.
static bool music_ui_bluetooth;
static bool music_ui_bt_started;
static uint8_t music_ui_track = 1u;
static uint8_t music_track_count = 45u; // Used only if DFPlayer folder count query fails.
static uint8_t alarm_track_count = 45u;
static music_playlist_t music_playlist;
static bool music_ui_pending;
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
static birthday_t birthdays[CLOCK_BIRTHDAY_COUNT], birthday_draft;
static uint8_t birthday_index, birthday_pending_mask;
static uint64_t birthday_queued_ms[CLOCK_BIRTHDAY_COUNT];
static bool birthday_rtc_primed;
static bool birthday_rtc_valid;
static ds1302_datetime_t birthday_now;
static bool info_press_active;
static uint64_t info_pressed_at_ms, info_overlay_until_ms;
static char info_overlay_top[17], info_overlay_bottom[17];
static uint64_t music_minus_pressed_at_ms;
static bool music_minus_pressed;
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
           auto_voice_jbl_enabled ? "BT_PREFERRED" : "LOCAL");
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
    uint64_t civil_epoch;
    if (rtc_replay_guard_until && clock_datetime_to_utc(dt,&civil_epoch) &&
        civil_epoch < rtc_replay_guard_until) return;
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
                puts("[AUTO-AUDIO] HOURLY_VOICE routed=BT");
            else
                puts("[AUTO-AUDIO] BT start failed; attempting LOCAL");
        } else {
            queued = false;
            if (auto_voice_jbl_enabled)
                puts("[AUTO-AUDIO] BT unavailable; hourly voice LOCAL");
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
    printf("[BT-TIMER] %s output=BT segments=%u gain=53.3%%\n",
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
static bool bt_speech_plan_start(const audio_queue_t *plan) {
    if (!plan || !plan->size || plan->size>AUDIO_QUEUE_CAPACITY ||
        bt_speech_running || bt_timer_running || bt_alarm_running ||
        routed_test_pending || mp3_play_active() || mp3_probe_active() ||
        sd_fat32_stream_active() || !audio_queue_idle(&audio_queue) ||
        dfplayer_busy() || timer.running || alarm_active_index>=0 || music_ui_open) return false;
    for (unsigned i=0;i<plan->size;++i)
        bt_speech_tracks[i]=plan->items[(plan->head+i)%AUDIO_QUEUE_CAPACITY];
    bt_speech_count=plan->size;
    bt_speech_index=0;
    bt_speech_track_started=false;
    bt_speech_running=true;
    printf("[BT-SPEECH] INFO START segments=%u\n",(unsigned)bt_speech_count);
    return true;
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
            puts("[BT-SPEECH] BT not ready; announcing via LOCAL");
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

static bool announce_info_plan(const audio_queue_t *plan) {
    if (!sound_enabled || !audio_queue_idle(&audio_queue) || dfplayer_busy() ||
        timer.running || alarm_active_index>=0 || music_ui_open ||
        menu_state!=MENU_CLOSED || alarm_ui_state!=ALARM_UI_CLOSED ||
        mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active()) {
        puts("[INFO] Speech skipped: mute or audio/UI busy; LCD information retained");
        return false;
    }
#ifdef CLOCK_ENABLE_A2DP
    if (bt_speech_running || bt_timer_running || bt_alarm_running || routed_test_pending) return false;
    if (audio_router.requested==AUDIO_OUTPUT_BT_JBL && clock_a2dp_stream_active() &&
        clock_a2dp_current_rate()==44100u) {
        return bt_speech_plan_start(plan);
    }
#endif
    for (unsigned i=0;i<plan->size;++i) {
        audio_track_t t=plan->items[(plan->head+i)%AUDIO_QUEUE_CAPACITY];
        audio_queue_enqueue(&audio_queue,t.folder,t.track);
    }
    return true;
}

static void announce_clock_info(bool nearest_alarm) {
    ds1302_datetime_t now_dt, date;
    uint64_t wall;
    if (!ds1302_read_datetime(&now_dt) || !clock_datetime_to_utc(&now_dt,&wall)) {
        puts("[INFO] RTC unavailable");
        return;
    }
    date=now_dt;
    uint64_t now_ms=to_ms_since_boot(get_absolute_time());
    audio_queue_t plan={0};
    int slot=-1;
    bool snooze=false;
    if (nearest_alarm) {
        slot=alarm_next_occurrence(alarms,&now_dt,&date);
        uint64_t best=UINT64_MAX;
        if (slot>=0) (void)clock_datetime_to_utc(&date,&best);
        for (unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
            if (!alarm_snoozes[i].pending) continue;
            uint64_t left=alarm_snoozes[i].deadline_ms>now_ms ?
                (alarm_snoozes[i].deadline_ms-now_ms+999u)/1000u : 0u;
            uint64_t due=wall+left;
            ds1302_datetime_t due_date;
            if (due<best && clock_utc_to_datetime(due,&due_date)) {
                best=due;
                date=due_date;
                slot=(int)i;
                snooze=true;
            }
        }
        if (slot<0) {
            snprintf(info_overlay_top,sizeof(info_overlay_top),"NEXT ALARM");
            snprintf(info_overlay_bottom,sizeof(info_overlay_bottom),"None enabled");
            info_overlay_until_ms=now_ms+4000u;
            puts("[INFO] NEXT ALARM: none enabled");
            return;
        }
        snprintf(info_overlay_top,sizeof(info_overlay_top),"%s A%u %02u:%02u",
            snooze?"SNOOZE":"NEXT",(unsigned)slot+1u,(unsigned)date.hour,(unsigned)date.minute);
        if (language==SPEECH_RU) audio_queue_enqueue(&plan,7u,(uint8_t)(slot+1));
    } else snprintf(info_overlay_top,sizeof(info_overlay_top),"DATE %s",speech_language_name(language));
    snprintf(info_overlay_bottom,sizeof(info_overlay_bottom),"%02u.%02u.%04u",
        (unsigned)date.day,(unsigned)date.month,(unsigned)date.year);
    info_overlay_until_ms=now_ms+4000u;
    if (!speech_enqueue_date(&plan,language,&date)) return;
    if (nearest_alarm && !speech_enqueue_time(&plan,language,date.hour,date.minute,exact_hour_phrase)) return;
    printf("[INFO] %s slot=%d date=%04u-%02u-%02u time=%02u:%02u%s\n",
        nearest_alarm?"NEXT_ALARM":"DATE",slot+1,(unsigned)date.year,(unsigned)date.month,
        (unsigned)date.day,(unsigned)date.hour,(unsigned)date.minute,snooze?" snooze":"");
    if (date.year<2023 || date.year>2029) puts("[INFO] No year recording; spoken year omitted, LCD keeps full year");
    announce_info_plan(&plan);
}

static void alarm_pending_add(unsigned index, uint8_t hour,
                              uint8_t minute, uint64_t due_ms, bool test) {
    if (index >= CLOCK_ALARM_COUNT) return;
    deferred_alarm_t *p = &deferred_alarms[index];
    if (p->pending) {
        printf("[ALARM-PENDING] %u already queued; no duplicate\n", index + 1u);
        return;
    }
    *p = (deferred_alarm_t){
        .pending = true, .test = test, .hour = hour, .minute = minute,
        .queued_ms = due_ms
    };
    printf("[ALARM-PENDING] queued=%u time=%02u:%02u ttl=30m%s\n",
           index + 1u, (unsigned)hour, (unsigned)minute,
           test ? " TEST" : "");
}
static void alarm_clear_waiting(unsigned index) {
    alarm_snoozes[index].pending=false;
    deferred_alarms[index].pending=false;
}
static int alarm_snooze_next(uint64_t now, uint64_t *remaining) {
    int first=-1;
    uint64_t deadline=UINT64_MAX;
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
        bool waiting=deferred_alarms[i].pending && deferred_alarms[i].snoozed;
        if(!alarm_snoozes[i].pending && !waiting) continue;
        uint64_t candidate=waiting ? now : alarm_snoozes[i].deadline_ms;
        if(first<0 || candidate<deadline) { first=(int)i; deadline=candidate; }
    }
    if(remaining) *remaining=first<0 || now>=deadline ? 0 : (deadline-now+999)/1000;
    return first;
}
static void alarm_snooze_cancel_all(void) {
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
        alarm_snoozes[i].pending=false;
        if(deferred_alarms[i].snoozed) deferred_alarms[i].pending=false;
    }
    puts("[SNOOZE] cancelled; daily schedules unchanged");
}
static void alarm_snooze_start(uint64_t now) {
    if(alarm_active_index<0) { puts("[SNOOZE] no ringing alarm"); return; }
    unsigned slot=(unsigned)alarm_active_index;
    alarm_snooze_schedule(&alarm_snoozes[slot],now,active_alarm_hour,
                           active_alarm_minute,active_alarm_test);
    alarm_stop("SNOOZE");
    printf("[SNOOZE] slot=%u delay=%lus (monotonic, RAM only)\n",slot+1,
           (unsigned long)(CLOCK_ALARM_SNOOZE_MS/1000u));
}
static void alarm_snooze_status(uint64_t now) {
    unsigned count=0;
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
        bool waiting=deferred_alarms[i].pending && deferred_alarms[i].snoozed;
        if(!alarm_snoozes[i].pending && !waiting) continue;
        uint64_t remaining=waiting || now>=alarm_snoozes[i].deadline_ms ? 0 :
            (alarm_snoozes[i].deadline_ms-now+999)/1000;
        printf("[SNOOZE] slot=%u remaining=%llus state=%s\n",i+1,
            (unsigned long long)remaining,waiting?"WAITING_AUDIO":"SLEEPING");
        ++count;
    }
    printf("[SNOOZE] count=%u delay=%lus; ALARM cancels on clock screen\n",count,
           (unsigned long)(CLOCK_ALARM_SNOOZE_MS/1000u));
}
static void alarm_snooze_service(uint64_t now) {
    for(unsigned i=0;i<CLOCK_ALARM_COUNT;++i) {
        alarm_snooze_t due;
        if(!alarm_snooze_take(&alarm_snoozes[i],now,&due)) continue;
        if(!deferred_alarms[i].pending) {
            alarm_pending_add(i,due.hour,due.minute,now,due.test);
            deferred_alarms[i].snoozed=true;
        }
        printf("[SNOOZE] due=%u; normal audio priority and 30m expiry apply\n",i+1);
    }
}
static void alarm_pending_status(uint64_t now) {
    unsigned count = 0u;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const deferred_alarm_t *p = &deferred_alarms[i];
        if (!p->pending) continue;
        ++count;
        uint64_t age = now >= p->queued_ms ? now - p->queued_ms : 0u;
        uint64_t left = age >= ALARM_DEFER_MAX_MS ? 0u :
                        (ALARM_DEFER_MAX_MS - age + 999u) / 1000u;
        printf("[ALARM-PENDING] slot=%u due=%02u:%02u expires_in=%llus%s\n",
               i + 1u, (unsigned)p->hour, (unsigned)p->minute,
               (unsigned long long)left, p->test ? " TEST" : "");
    }
    printf("[ALARM-PENDING] count=%u limit=30m (RAM only)\n", count);
}
static void alarm_pending_service(uint64_t now) {
    int first = -1;
    // Expiration applies even if the timer keeps running or sound is muted.
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        deferred_alarm_t *p = &deferred_alarms[i];
        if (!p->pending) continue;
        if (now < p->queued_ms || now - p->queued_ms >= ALARM_DEFER_MAX_MS) {
            printf("[ALARM-PENDING] expired=%u age_limit=30m\n", i + 1u);
            p->pending = false;
            continue;
        }
        if (first < 0 ||
            p->queued_ms < deferred_alarms[first].queued_ms)
            first = (int)i;
    }
    if (first >= 0 && !timer.running && alarm_active_index < 0 &&
        sound_enabled && menu_state == MENU_CLOSED &&
        alarm_ui_state == ALARM_UI_CLOSED && music_ui_open)
        music_ui_close("DEFERRED_ALARM_PRIORITY");
    if (first < 0 || timer.running || alarm_active_index >= 0 ||
        (finished_screen_until_ms && now < finished_screen_until_ms) ||
        menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED ||
        !sound_enabled || !audio_queue_idle(&audio_queue) || dfplayer_busy()
#ifdef CLOCK_ENABLE_A2DP
        || bt_timer_running || bt_speech_running || bt_alarm_running ||
           mp3_play_active() || mp3_probe_active() ||
           sd_fat32_stream_active() || routed_test_pending
#endif
       ) return;
    deferred_alarm_t pending = deferred_alarms[first];
    deferred_alarms[first].pending = false;
    printf("[ALARM-PENDING] dispatch=%d waited=%llus%s\n",
           first + 1, (unsigned long long)((now - pending.queued_ms) / 1000u),
           pending.test ? " TEST" : "");
    // Keep the original RTC due time rather than announcing the late wall time.
    active_alarm_due_ms = pending.queued_ms;
    alarm_start_audio((unsigned)first, pending.hour, pending.minute,
                      now, pending.test);
}

static void timer_start_now(uint64_t now) {
    if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open || menu_state==MENU_TIMER_EDITOR) {
        puts("[TIMER] start blocked: close UI/player first");
        return;
    }
    // An interactive timer preempts a ringing alarm, which resumes later
    // from the beginning if it is still within its 30-minute deadline.
    if (alarm_active_index >= 0) {
        unsigned interrupted = (unsigned)alarm_active_index;
        alarm_pending_add(interrupted, active_alarm_hour,
                          active_alarm_minute, active_alarm_due_ms,
                          active_alarm_test);
        alarm_stop("TIMER_PRIORITY_DEFERRED");
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
        .bt_volume = bt_volume,
        .exact_hour = exact_hour_phrase,
        .quiet_enabled = quiet_enabled,
        .quiet_start = quiet_start,
        .quiet_end = quiet_end,
        .half_hour_enabled = half_hour_enabled,
        .clock_mode = clock_mode,
        .display_light = display_light,
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
    memcpy(cfg.birthdays,birthdays,sizeof(birthdays));
    memcpy(cfg.bt_target_addr, saved_bt_target_addr, sizeof(cfg.bt_target_addr));
    cfg.bt_known_count = bt_known_count;
    memcpy(cfg.bt_known, bt_known, sizeof(cfg.bt_known));
    memcpy(cfg.wifi_ssid,saved_wifi_ssid,sizeof(cfg.wifi_ssid));
    memcpy(cfg.wifi_password,saved_wifi_password,sizeof(cfg.wifi_password));
#ifdef CLOCK_ENABLE_PHONE_WEB
    phone_web_get_settings(&cfg.phone);
#else
    cfg.phone = saved_phone;
#endif
    return cfg;
}

static bool settings_equal(const clock_settings_t *a, const clock_settings_t *b) {
    // Compare fields explicitly, not struct padding.
    if (a->language != b->language || a->volume != b->volume ||
        a->bt_volume != b->bt_volume ||
        a->exact_hour != b->exact_hour || a->quiet_enabled != b->quiet_enabled ||
        a->quiet_start != b->quiet_start || a->quiet_end != b->quiet_end ||
        a->half_hour_enabled != b->half_hour_enabled ||
        a->clock_mode != b->clock_mode ||
        a->display_light.mode != b->display_light.mode ||
        a->display_light.day_percent != b->display_light.day_percent ||
        a->display_light.night_percent != b->display_light.night_percent ||
        a->audio_manual_jbl != b->audio_manual_jbl ||
        a->audio_hourly_jbl != b->audio_hourly_jbl ||
        a->audio_timer_jbl != b->audio_timer_jbl ||
        a->audio_alarm_jbl != b->audio_alarm_jbl ||
         memcmp(a->bt_target_addr, b->bt_target_addr, sizeof(a->bt_target_addr)) != 0 ||
         a->bt_known_count != b->bt_known_count ||
         memcmp(a->bt_known, b->bt_known, sizeof(a->bt_known)) != 0 ||
         strcmp(a->wifi_ssid,b->wifi_ssid) || strcmp(a->wifi_password,b->wifi_password) ||
         memcmp(&a->phone, &b->phone, sizeof(a->phone)) != 0)
        return false;
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *x = &a->alarms[i], *y = &b->alarms[i];
        if (x->enabled != y->enabled || x->hour != y->hour ||
            x->minute != y->minute || x->sound != y->sound ||
            x->track != y->track || x->weekdays!=y->weekdays) return false;
    }
    for (unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i) {
        const birthday_t *x=&a->birthdays[i], *y=&b->birthdays[i];
        if (x->enabled!=y->enabled || x->month!=y->month || x->day!=y->day ||
            x->hour!=y->hour || x->minute!=y->minute || x->last_year!=y->last_year) return false;
    }
    return true;
}

static void settings_mark_changed(void) {
    clock_settings_t current = settings_snapshot();
    autosave_pending = !settings_equal(&current, &last_persisted_settings);
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
        routed_test_pending || mp3_play_active() || sd_fat32_stream_active() ||
        clock_a2dp_scan_active()
#endif
       ) return;

    clock_settings_t cfg = settings_snapshot();
    if (settings_equal(&cfg, &last_persisted_settings)) {
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

// Active audio owner takes priority. When idle, use current manual audio
// preference; when disconnected, use the LOCAL rotary level.
static bool volume_for_bluetooth(void) {
#ifdef CLOCK_ENABLE_A2DP
    if (!clock_a2dp_stream_active()) return false;
    // An active Bluetooth audio owner has priority over LOCAL standby
    // states. MUSIC-UI can use either BT (SPI card) or LOCAL (DFPlayer)
    // and its selected output must govern volume during playback.
    if (bt_timer_running || bt_alarm_running || bt_speech_running ||
        routed_test_pending || mp3_play_active()) return true;
    if (music_ui_open || music_ui_playing) return music_ui_bluetooth;
    // Other active DFPlayer output must not accidentally adjust a remote
    // speaker just because A2DP stays connected and idle.
    if (!audio_queue_idle(&audio_queue) || dfplayer_busy()) return false;
    if (timer.running && !timer_audio_jbl_enabled) return false;
    if (alarm_active_index >= 0 && !alarm_audio_jbl_enabled) return false;
    return audio_router.requested == AUDIO_OUTPUT_BT_JBL;
#else
    return false;
#endif
}

static void set_output_volume(bool bluetooth, uint8_t level) {
    if (level > 30u) level = 30u;
#ifdef CLOCK_ENABLE_A2DP
    if (bluetooth) {
        if (bt_volume == level && bt_volume_remote_synced) return;
        bt_volume = level;
        bt_volume_remote_synced = true;
        clock_a2dp_set_volume(bt_volume);
        settings_mark_changed();
        printf("[AUDIO] Bluetooth volume=%u/30 output=%s\n",
               (unsigned)bt_volume,
               clock_a2dp_pcm_volume_fallback() ? "PCM" : "AVRCP");
    } else
#endif
    {
        if (volume == level) return;
        volume = level;
        audio_queue_set_volume(&audio_queue, volume);
        settings_mark_changed();
        printf("[AUDIO] LOCAL volume=%u/30\n", (unsigned)volume);
    }
    volume_overlay_bluetooth = bluetooth;
    volume_overlay_until_ms = to_ms_since_boot(get_absolute_time()) + VOLUME_OVERLAY_MS;
}

static void change_volume(int direction) {
    bool bluetooth = volume_for_bluetooth();
    uint8_t current = bluetooth ? bt_volume : volume;
#ifdef CLOCK_ENABLE_A2DP
    // Adopt the remote speaker volume on the first encoder adjustment,
    // preventing a sudden jump from a stale saved level. After that,
    // the clock manages the 0..30 setting independently.
    if (bluetooth && clock_a2dp_avrcp_available()) {
        uint8_t remote = clock_a2dp_remote_volume();
        if (!bt_volume_remote_synced && remote <= 127u &&
            !clock_a2dp_pcm_volume_fallback())
            current = (uint8_t)(((unsigned)remote * 30u + 63u) / 127u);
    }
#endif
    int requested = (int)current + direction;
    if (requested < 0) requested = 0;
    if (requested > 30) requested = 30;
    set_output_volume(bluetooth, (uint8_t)requested);
}

static void toggle_sound(void) {
    sound_enabled = !sound_enabled;
    volume_overlay_until_ms = to_ms_since_boot(get_absolute_time()) + VOLUME_OVERLAY_MS;
    if (!sound_enabled) {
        if (music_ui_open) music_ui_stop("SOUND_OFF");
        alarm_preview_stop("SOUND_OFF");
        audio_queue_clear(&audio_queue, true);
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

static void music_ui_stop(const char *reason) {
    music_ui_pending=false;
    music_ui_paused=false;
    if (!music_ui_playing) return;
#ifdef CLOCK_ENABLE_A2DP
    if (music_ui_bluetooth && mp3_play_active()) mp3_play_stop();
#endif
    if (!music_ui_bluetooth) audio_queue_clear(&audio_queue, true);
    music_ui_bt_started = false;
    music_ui_playing = false;
    printf("[MUSIC-UI] stopped=%s\n", reason);
}

static void music_ui_close(const char *reason) {
    if (!music_ui_open) return;
    music_ui_stop(reason);
    music_ui_open = false;
    music_sleep=(music_sleep_t){0};
    mode_press_active=false;
    music_minus_pressed=false;
    timer_screen = false;
    printf("[MUSIC-UI] closed=%s\n", reason);
}

static bool music_play_track(uint8_t track) {
#ifdef CLOCK_ENABLE_A2DP
    if (music_ui_bluetooth) {
        char path[20];
        snprintf(path, sizeof(path), "08/%03u.mp3", (unsigned)track);
        if (clock_a2dp_stream_active() &&
            clock_a2dp_current_rate() == 44100u &&
            mp3_play_start(path, clock_a2dp_current_rate())) {
            music_ui_bt_started = true;
            music_ui_track = track;
            music_ui_playing = true;
            printf("[MUSIC-UI] playing BT folder=08 track=%03u\n",
                   (unsigned)track);
            return true;
        }
        // The DFPlayer's SD and the SPI FAT32 card are physically separate.
        // If no matching SPI MP3 exists, allow local playback rather than
        // silently claiming BT succeeded.
        printf("[MUSIC-UI] BT unavailable or SPI file %s absent; LOCAL fallback\n",
               path);
        music_ui_bluetooth = false;
    }
#endif
    if (!audio_queue_enqueue(&audio_queue, 8u, track)) {
        puts("[MUSIC-UI] queue full");
        return false;
    }
    music_ui_track = track;
    music_ui_playing = true;
    printf("[MUSIC-UI] playing LOCAL folder=08 track=%03u\n",
           (unsigned)track);
    return true;
}

static void music_ui_start(void) {
    if (!music_ui_open || !sound_enabled || timer.running || alarm_active_index>=0) return;
    // A track change may still have a queued UART pause. Start on the next
    // safe service tick instead of discarding the user's request.
    music_ui_pending=true;
}

static void music_ui_toggle(void) {
    if (!music_ui_open) return;
    if (music_ui_pending) { music_ui_stop("BUTTON_BEFORE_START"); return; }
    if (music_ui_paused) {
        bool resumed=false;
#ifdef CLOCK_ENABLE_A2DP
        if (music_ui_bluetooth) resumed=mp3_play_resume();
        else
#endif
            resumed=audio_queue_resume(&audio_queue);
        music_ui_paused=false;
        if (!resumed) {
            music_ui_stop("RESUME_UNAVAILABLE");
            music_ui_start();
        }
        printf("[MUSIC-UI] resume=%s\n",resumed?"POSITION":"RESTART");
    } else if (music_ui_playing) {
        bool paused=false;
#ifdef CLOCK_ENABLE_A2DP
        if (music_ui_bluetooth) paused=mp3_play_pause();
        else
#endif
            paused=audio_queue_pause(&audio_queue);
        music_ui_paused=paused;
        printf("[MUSIC-UI] pause=%s\n",paused?"POSITION":"AT_EOF");
    } else music_ui_start();
}

static void music_ui_rotate(int direction) {
    if (!music_ui_open || !direction) return;
    music_ui_stop("TRACK_CHANGED");
    music_ui_track=direction>0 ? music_playlist_next(&music_playlist,false) :
                               music_playlist_previous(&music_playlist);
    printf("[MUSIC-UI] selected track=%u/%u mode=%s history=%u/%u\n",
        (unsigned)music_ui_track,(unsigned)music_track_count,
        music_playlist_mode_name(music_playlist.mode),
        music_playlist.history_position+1,music_playlist.history_size);
    music_ui_start();
}

static void music_ui_cycle_mode(void) {
    music_playlist_cycle_mode(&music_playlist);
    printf("[MUSIC-UI] mode=%s (current track continues)\n",
           music_playlist_mode_name(music_playlist.mode));
}

static void music_ui_enter(void) {
    if (timer.running || alarm_active_index >= 0 ||
        menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED ||
        !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
        puts("[MUSIC-UI] open blocked: timer/alarm/menu/audio busy");
        return;
    }
    uint8_t detected = dfplayer_folder_count(8u, to_ms_since_boot(get_absolute_time()));
    music_track_count = detected ? detected : 45u;
    printf("[MUSIC-UI] folder=08 files=%u source=%s\n",
           (unsigned)music_track_count, detected ? "DFPLAYER" : "FALLBACK");
    music_playlist_init(&music_playlist,music_track_count,(uint32_t)time_us_64());
    music_ui_track=music_playlist.current;
    music_ui_pending=false;
    music_ui_open = true;
    music_ui_playing = false;
    music_ui_paused = false;
    music_sleep=(music_sleep_t){0};
    music_ui_bt_started = false;
#ifdef CLOCK_ENABLE_A2DP
    music_ui_bluetooth = audio_router.requested == AUDIO_OUTPUT_BT_JBL &&
                         clock_a2dp_stream_active();
#else
    music_ui_bluetooth = false;
#endif
    timer_screen = false;
    printf("[MUSIC-UI] output=%s, BT music requires /08/NNN.mp3 on SPI FAT32\n",
           music_ui_bluetooth ? "BT" : "LOCAL");
    printf("[MUSIC-UI] opened tracks=%u track=%u mode=SHUF; TIMER=PLAY/PAUSE SETTINGS=NEXT MINUS=PREV ALARM=MODE GP28=SLEEP; hold ALARM/MODE/MINUS=EXIT\n",
           (unsigned)music_track_count,(unsigned)music_ui_track);
    music_ui_start();
}

static void music_ui_service(void) {
    if (!music_ui_open) return;
    if (music_sleep_expired(&music_sleep,to_ms_since_boot(get_absolute_time()))) {
        music_ui_close("SLEEP_EXPIRED");
        return;
    }
    if (music_ui_pending) {
        if (!sound_enabled || timer.running || alarm_active_index>=0) {
            music_ui_pending=false;
            return;
        }
        if (!audio_queue_idle(&audio_queue) || dfplayer_busy() ||
            mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active()
#ifdef CLOCK_ENABLE_A2DP
            || bt_speech_running || bt_timer_running || bt_alarm_running || routed_test_pending
#endif
           ) return;
        music_ui_pending=false;
        (void)music_play_track(music_ui_track);
        return;
    }
    if (!music_ui_playing) return;
#ifdef CLOCK_ENABLE_A2DP
    if (music_ui_bluetooth) {
        if (!clock_a2dp_stream_active()) {
            bool was_paused=music_ui_paused;
            music_ui_stop("BT_LINK_LOST");
            music_ui_bluetooth=false;
            puts(was_paused ? "[MUSIC-UI] BT link lost while paused; TIMER restarts current track on LOCAL" :
                              "[MUSIC-UI] BT link lost; restarting current track on LOCAL");
            if (was_paused) music_ui_paused=true;
            else music_ui_start();
            return;
        }
        if (music_ui_paused || mp3_play_active() || !music_ui_bt_started) return;
        music_ui_bt_started=false;
    } else
#endif
    {
        if (music_ui_paused) return;
        if (!audio_queue_idle(&audio_queue) || dfplayer_busy()) return;
    }
    music_ui_playing=false;
    music_ui_track=music_playlist_next(&music_playlist,true);
    music_ui_start();
}

#ifdef CLOCK_ENABLE_PHONE_WEB
static bool service_phone_player(phone_action_t action) {
    if (action == PHONE_PLAYER_STOP) {
        music_ui_close("PHONE_STOP");
        return true;
    }
    if (timer.running || alarm_active_index >= 0 || menu_state != MENU_CLOSED ||
        alarm_ui_state != ALARM_UI_CLOSED || !sound_enabled) return false;
    if (!music_ui_open) {
        if (action != PHONE_PLAYER_PLAY) return false;
#ifdef CLOCK_ENABLE_A2DP
        if (bt_speech_running || bt_timer_running || bt_alarm_running || routed_test_pending ||
            mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active()) return false;
#endif
        music_ui_enter();
        return music_ui_open && music_ui_pending;
    }
    switch (action) {
        case PHONE_PLAYER_PLAY:
            if (music_ui_paused) music_ui_toggle();
            else if (!music_ui_playing && !music_ui_pending) music_ui_start();
            return music_ui_playing || music_ui_pending;
        case PHONE_PLAYER_PAUSE:
            if (!music_ui_paused && music_ui_playing && !music_ui_pending) music_ui_toggle();
            return music_ui_paused;
        case PHONE_PLAYER_RESUME:
            if (music_ui_paused) music_ui_toggle();
            return music_ui_playing || music_ui_pending;
        case PHONE_PLAYER_NEXT:
        case PHONE_PLAYER_PREVIOUS:
            music_ui_rotate(action == PHONE_PLAYER_NEXT ? 1 : -1);
            return music_ui_pending;
        default:
            return false;
    }
}

#endif

static void alarm_ui_open(uint64_t now) {
    if (timer.running || alarm_active_index >= 0 ||
        menu_state != MENU_CLOSED || music_ui_open) {
        puts("[ALARM-UI] busy: timer/alarm/settings/music active");
        return;
    }
    alarm_ui_index = 0u;
    bool can_query = audio_queue_idle(&audio_queue) && !dfplayer_busy() &&
                     !mp3_play_active() && !mp3_probe_active();
#ifdef CLOCK_ENABLE_A2DP
    can_query = can_query && !bt_speech_running && !bt_timer_running && !bt_alarm_running;
#endif
    uint8_t detected = can_query ? dfplayer_folder_count(8u,now) : 0u;
    alarm_track_count = detected ? detected : music_track_count;
    for (unsigned i=0;i<CLOCK_ALARM_COUNT;++i)
        if (alarms[i].track > alarm_track_count) alarm_track_count=alarms[i].track;
    alarm_ui_state = ALARM_UI_LIST;
    alarm_ui_last_input_ms = now;
    timer_screen = false;
    puts("[ALARM-UI] list opened; encoder=select TIMER=edit MINUS=exit");
}

static void alarm_ui_begin_edit(uint64_t now) {
    if (alarm_ui_state != ALARM_UI_LIST) return;
    alarm_ui_draft = alarms[alarm_ui_index];
    alarm_ui_custom_days=alarm_ui_draft.weekdays;
    alarm_ui_days_choice=alarm_ui_custom_days==CLOCK_ALARM_DAYS_ALL?0:
        alarm_ui_custom_days==CLOCK_ALARM_DAYS_WEEKDAYS?1:
        alarm_ui_custom_days==CLOCK_ALARM_DAYS_WEEKEND?2:3;
    alarm_ui_days_invalid=false;
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
        case ALARM_UI_MINUTE: alarm_ui_state = ALARM_UI_DAYS; break;
        case ALARM_UI_DAYS:
            alarm_ui_day_index=0; alarm_ui_days_invalid=false;
            alarm_ui_state=alarm_ui_days_choice==3 ? ALARM_UI_DAY : ALARM_UI_SOUND;
            break;
        case ALARM_UI_DAY:
            if(alarm_ui_day_index<6) { ++alarm_ui_day_index; break; }
            if(!alarm_ui_draft.weekdays) {
                alarm_ui_days_invalid=true;
                puts("[ALARM-UI] choose at least one day; MINUS goes to previous day");
                return;
            }
            alarm_ui_state=ALARM_UI_SOUND;
            break;
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
                               saved->track != alarm_ui_draft.track ||
                               saved->weekdays!=alarm_ui_draft.weekdays;
                *saved = alarm_ui_draft;
                if (changed) {
                    alarm_clear_waiting(alarm_ui_index);
                    alarm_consume_current_minute(alarm_ui_index);
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
        case ALARM_UI_DAYS:
            alarm_ui_days_choice=wrap_u8(alarm_ui_days_choice,0,3,direction);
            alarm_ui_draft.weekdays=alarm_ui_days_choice==0?CLOCK_ALARM_DAYS_ALL:
                alarm_ui_days_choice==1?CLOCK_ALARM_DAYS_WEEKDAYS:
                alarm_ui_days_choice==2?CLOCK_ALARM_DAYS_WEEKEND:alarm_ui_custom_days;
            break;
        case ALARM_UI_DAY:
            alarm_ui_draft.weekdays^=(uint8_t)(1u<<alarm_ui_day_index);
            alarm_ui_custom_days=alarm_ui_draft.weekdays;
            alarm_ui_days_invalid=false;
            break;
        case ALARM_UI_SOUND:
            alarm_ui_draft.sound =
                alarm_ui_draft.sound == ALARM_SOUND_SIGNAL ?
                ALARM_SOUND_MUSIC : ALARM_SOUND_SIGNAL;
            break;
        case ALARM_UI_TRACK:
            alarm_ui_draft.track = wrap_u8(alarm_ui_draft.track, 1u,
                                            alarm_track_count, direction);
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
    } else if(alarm_ui_state==ALARM_UI_DAY) {
        alarm_ui_days_invalid=false;
        if(alarm_ui_day_index) --alarm_ui_day_index;
        else alarm_ui_state=ALARM_UI_DAYS;
    } else {
        // Never apply an incomplete draft.
        alarm_ui_state = ALARM_UI_LIST;
        printf("[ALARM-UI] edit cancelled #%u (RAM/flash unchanged)\n",
               (unsigned)(alarm_ui_index + 1u));
    }
}

#ifdef CLOCK_ENABLE_A2DP
// Explicitly changing the Bluetooth sink is safe only when no audio owns
// the current stream. Selecting another sink does not alter LOCAL/JBL flags.
static bool bt_sink_change_allowed(void){
    return !timer.running && alarm_active_index < 0 &&
           !bt_speech_running && !bt_timer_running &&
           !bt_alarm_running && !mp3_play_active() &&
           !mp3_probe_active() && !routed_test_pending &&
           !sd_fat32_stream_active() && !music_ui_open &&
           audio_queue_idle(&audio_queue) && !dfplayer_busy();
}
static int bt_known_find(const uint8_t address[6]) {
    for (unsigned i = 0; i < bt_known_count; ++i)
        if (memcmp(bt_known[i].address, address, 6) == 0) return (int)i;
    return -1;
}
// Main-loop scratch storage avoids putting two device arrays on the MCU stack.
static clock_bt_device_t bt_catalog_cache[BT_CATALOG_CAPACITY];
static clock_bt_device_t bt_scan_cache[CLOCK_BT_MAX_DEVICES];
static unsigned bt_catalog_refresh(void) {
    unsigned count = 0;
    for (unsigned i = 0; i < clock_a2dp_scan_count() && count < CLOCK_BT_MAX_DEVICES; ++i)
        if (clock_a2dp_scan_get(i, &bt_scan_cache[count])) ++count;
    return (unsigned)bt_catalog_build(bt_known, bt_known_count, bt_scan_cache, count,
                                     bt_catalog_cache, BT_CATALOG_CAPACITY);
}
static bool bt_menu_device(unsigned index, clock_bt_device_t *out) {
    if (!index || !out) return false;
    unsigned count = bt_catalog_refresh();
    if (index > count) return false;
    *out = bt_catalog_cache[index - 1u];
    return true;
}
static unsigned bt_menu_count(void) {
    return 2u + bt_catalog_refresh(); // Scan, devices, Disconnect. Device indices remain 1-based.
}
static const char *bt_lcd_name(unsigned index, const clock_bt_device_t *device) {
    // Legacy saved entries and discovery fallbacks can contain a MAC.
    // Keep the original name/address for connection and USB diagnostics.
    if (bt_name_usable(device->name)) return device->name;
    return index <= bt_known_count ? "Saved speaker" : "Unknown device";
}
static void bt_persist_disconnected(const char *reason) {
    bt_pending_target_valid=false;
    bt_pending_seen_disconnected=false;
    memset(saved_bt_target_addr,0xff,sizeof(saved_bt_target_addr));
    settings_mark_changed();
    printf("[BT-POLICY] auto=OFF reason=%s; saved devices and pairing retained; Flash autosave pending\n",reason);
}
static bool bt_user_disconnect(void) {
    if (!bt_sink_change_allowed()) {
        puts("[BT] Disconnect refused: timer/alarm/audio busy; stop playback first");
        return false;
    }
    clock_a2dp_disconnect();
    bt_persist_disconnected("USER_DISCONNECT");
    return true;
}
static bool bt_user_scan(void) {
    if (!bt_sink_change_allowed()) return false;
    bool started=clock_a2dp_scan_start();
    // Scan may fail after the previous target was already disconnected.
    if (!clock_a2dp_autoconnect_enabled()) bt_persist_disconnected("SCAN");
    return started;
}
static bool bt_sink_select(const char *mac){
    unsigned b[6];
    char end;
    if (!mac || strlen(mac) != 17u ||
        sscanf(mac, "%2x:%2x:%2x:%2x:%2x:%2x%c",
               &b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&end) != 6)
        return false;
    if (!bt_sink_change_allowed()){
        puts("[BT-SELECT] Busy audio/timer; selection refused");
        return false;
    }
    bool was_active = clock_a2dp_stream_active();
    if (!clock_a2dp_select_target(mac)) return false;
    for (unsigned i=0;i<6u;i++)
        bt_pending_target_addr[i] = (uint8_t)b[i];
    snprintf(bt_pending_target_name, sizeof(bt_pending_target_name), "%s", mac);
    clock_bt_device_t discovered;
    for (unsigned i = 0; i < clock_a2dp_scan_count(); ++i) {
        unsigned found[6];
        if (clock_a2dp_scan_get(i, &discovered) &&
            sscanf(discovered.address, "%2x:%2x:%2x:%2x:%2x:%2x",
                   &found[0], &found[1], &found[2], &found[3],
                   &found[4], &found[5]) == 6 &&
            found[0] == b[0] && found[1] == b[1] && found[2] == b[2] &&
            found[3] == b[3] && found[4] == b[4] && found[5] == b[5]) {
            snprintf(bt_pending_target_name, sizeof(bt_pending_target_name), "%s",
                     discovered.name);
            break;
        }
    }
    int known = bt_known_find(bt_pending_target_addr);
    const char *preferred_name=bt_preferred_name(
        known>=0?bt_known[known].name:NULL,bt_pending_target_name,mac);
    if (preferred_name!=bt_pending_target_name)
        snprintf(bt_pending_target_name, sizeof(bt_pending_target_name), "%s",
                 preferred_name);
    bt_pending_target_valid = true;
    bt_pending_seen_disconnected = !was_active;
    // Do not persist a sink that never successfully negotiates A2DP.
    printf("[BT-SELECT] connecting=%s; save only after stream ACTIVE\n",
           clock_a2dp_target());
    return true;
}
static void bt_scan_list(void){
    printf("[BT-KNOWN] count=%u\n", (unsigned)bt_known_count);
    for (unsigned i = 0; i < bt_known_count; ++i) {
        clock_bt_device_t known;
        bt_menu_device(i + 1u, &known);
        printf("[BT-KNOWN] %u address=%s name=%s paired=%s\n",
               i + 1u, known.address, known.name,
               clock_a2dp_has_link_key(known.address) ? "YES" : "NO");
    }
    printf("[BT-SCAN] count=%u active=%s preferred=%s\n",
           clock_a2dp_scan_count(),
           clock_a2dp_scan_active() ? "YES" : "NO",
           clock_a2dp_target());
    clock_bt_device_t d;
    for (unsigned i=bt_known_count;i+2u<bt_menu_count();i++){
        if (bt_menu_device(i+1u,&d))
            printf("[BT-SCAN] %u address=%s name=%s rssi=%d\n",
                    i+1u,d.address,d.name,(int)d.rssi);
    }
}
#endif

static const char *menu_item_name(unsigned index) {
    static const char *names[SETTINGS_MENU_ITEMS] = {
        "Language", "Time", "Date", "Quiet mode",
        "Half-hour beep", "Exact-hour word", "Audio Output",
        "Bluetooth", "Birthdays", "Display Light",
#ifdef CLOCK_ENABLE_PHONE_WEB
        "Phone Control",
#endif
        "About"
    };
    return index < SETTINGS_MENU_ITEMS ? names[index] : "?";
}

static const char *menu_audio_name(unsigned index) {
    static const char *names[SETTINGS_AUDIO_ITEMS] = {
        "Manual", "Hourly voice", "Timer", "Alarm"
    };
    return index < SETTINGS_AUDIO_ITEMS ? names[index] : "?";
}

static bool menu_audio_selected_jbl(unsigned index) {
#ifdef CLOCK_ENABLE_A2DP
    switch (index) {
        case 0: return audio_router.requested == AUDIO_OUTPUT_BT_JBL;
        case 1: return auto_voice_jbl_enabled;
        case 2: return timer_audio_jbl_enabled;
        case 3: return alarm_audio_jbl_enabled;
        default: return false;
    }
#else
    (void)index;
    return false; // BT-free firmware exposes only LOCAL.
#endif
}

static void menu_audio_apply(unsigned index, bool jbl) {
#ifdef CLOCK_ENABLE_A2DP
    if (menu_audio_selected_jbl(index) == jbl) return;
    switch (index) {
        case 0:
            if (!jbl) {
                if (bt_speech_running) bt_speech_fallback("MENU_SELECT_LOCAL");
                if (routed_test_pending && mp3_play_active()) {
                    mp3_play_stop();
                    routed_test_pending = false;
                }
            }
            audio_router_select(&audio_router,
                                jbl ? AUDIO_OUTPUT_BT_JBL : AUDIO_OUTPUT_LOCAL);
            audio_router_bluetooth_state(&audio_router, clock_a2dp_stream_active());
            break;
        case 1:
            auto_voice_jbl_enabled = jbl;
            break;
        case 2:
            timer_audio_jbl_enabled = jbl;
            if (!jbl && bt_timer_running) bt_timer_fallback("MENU_SELECT_LOCAL");
            break;
        case 3:
            alarm_audio_jbl_enabled = jbl;
            if (!jbl && bt_alarm_running) bt_alarm_fallback("MENU_SELECT_LOCAL");
            break;
        default:
            return;
    }
    settings_mark_changed();
#else
    (void)index;
    (void)jbl;
    puts("[MENU-AUDIO] BT unavailable in this firmware; LOCAL only");
#endif
}

#ifdef CLOCK_ENABLE_PHONE_WEB
static bool phone_settings_idle(void) {
    bool idle = !timer.running && alarm_active_index < 0 && !music_ui_open &&
        menu_state == MENU_CLOSED && alarm_ui_state == ALARM_UI_CLOSED &&
        audio_queue_idle(&audio_queue) && !dfplayer_busy() &&
        !mp3_play_active() && !mp3_probe_active() && !sd_fat32_stream_active();
#ifdef CLOCK_ENABLE_A2DP
    idle = idle && !bt_speech_running && !bt_timer_running && !bt_alarm_running && !routed_test_pending;
#endif
    return idle;
}

static bool service_phone_settings(const phone_command_t *request) {
    if (menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED) return false;
    uint8_t value = (uint8_t)request->value;
    if (request->action == PHONE_SET_VOLUME_LOCAL || request->action == PHONE_SET_VOLUME_BT) {
#ifndef CLOCK_ENABLE_A2DP
        if (request->action == PHONE_SET_VOLUME_BT) return false;
#endif
        set_output_volume(request->action == PHONE_SET_VOLUME_BT, value);
        return true;
    }
    if (request->action >= PHONE_SET_LIGHT_MODE && request->action <= PHONE_SET_LIGHT_NIGHT) {
        display_light_t light = display_light;
        switch (request->action) {
            case PHONE_SET_LIGHT_MODE: light.mode = value; break;
            case PHONE_SET_LIGHT_DAY: light.day_percent = value; break;
            case PHONE_SET_LIGHT_NIGHT: light.night_percent = value; break;
            default: return false;
        }
        if (!display_light_valid(light)) return false;
        if (light.mode != display_light.mode || light.day_percent != display_light.day_percent ||
            light.night_percent != display_light.night_percent) {
            display_light = light;
            settings_mark_changed();
        }
        return true;
    }
    if (!phone_settings_idle()) return false;
    if (request->action == PHONE_SET_LANGUAGE) {
        if ((uint8_t)language != value) {
            language = (speech_language_t)value;
            settings_mark_changed();
        }
        return true;
    }
    if (request->action >= PHONE_SET_AUDIO_MANUAL && request->action <= PHONE_SET_AUDIO_ALARM) {
#ifndef CLOCK_ENABLE_A2DP
        if (value) return false;
#endif
        menu_audio_apply((unsigned)(request->action - PHONE_SET_AUDIO_MANUAL), value != 0);
        return true;
    }
    return false;
}

static phone_command_result_t service_phone_schedule(const phone_command_t *request) {
    if (!phone_settings_idle()) return PHONE_COMMAND_BLOCKED;
    unsigned slot = request->slot;
    if (request->action == PHONE_SET_ALARM) {
        if (phone_alarm_revision(&alarms[slot]) != request->expected) return PHONE_COMMAND_CONFLICT;
        if (phone_alarm_revision(&alarms[slot]) != phone_alarm_revision(&request->alarm)) {
            alarms[slot] = request->alarm;
            alarm_clear_waiting(slot);
            alarm_consume_current_minute(slot);
            settings_mark_changed();
        }
    } else {
        if (phone_birthday_revision(&birthdays[slot]) != request->expected) return PHONE_COMMAND_CONFLICT;
        if (phone_birthday_revision(&birthdays[slot]) != phone_birthday_revision(&request->birthday))
            birthday_apply(slot, request->birthday);
    }
    return PHONE_COMMAND_APPLIED;
}

static void service_phone_command(uint64_t now) {
    phone_command_t request;
    if (!phone_web_command_pending(&request)) return;
    if (request.action >= PHONE_SET_ALARM) {
        phone_web_command_complete(request.id, service_phone_schedule(&request));
        return;
    }
    bool applied = false;
    if (request.action >= PHONE_SET_VOLUME_LOCAL) {
        applied = service_phone_settings(&request);
    } else if (request.action >= PHONE_PLAYER_PLAY) {
        applied = service_phone_player(request.action);
    } else if (request.action == PHONE_TIMER_CANCEL) {
        if (timer.running) timer_cancel_now();
        applied = true;
    } else if (!timer.running && menu_state == MENU_CLOSED &&
               alarm_ui_state == ALARM_UI_CLOSED && !music_ui_open) {
        countdown_set_duration(&timer, request.value);
        if (request.action == PHONE_TIMER_START) timer_start_now(now);
        else {
            timer_screen = true;
            finished_screen_until_ms = 0;
        }
        applied = true;
    }
    phone_web_command_complete(request.id, applied ? PHONE_COMMAND_APPLIED : PHONE_COMMAND_BLOCKED);
}
#endif

static void birthday_status(void) {
    for (unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i) {
        const birthday_t *b=&birthdays[i];
        printf("[BIRTHDAY] %u enabled=%s date=%02u-%02u time=%02u:%02u last_year=%u pending=%s\n",
            i+1,b->enabled?"ON":"OFF",(unsigned)b->day,(unsigned)b->month,
            (unsigned)b->hour,(unsigned)b->minute,(unsigned)b->last_year,
            birthday_pending_mask&(1u<<i)?"YES":"NO");
    }
}

static void birthday_apply(unsigned index, birthday_t draft) {
    birthday_t *b=&birthdays[index];
    bool changed=b->month!=draft.month || b->day!=draft.day || b->hour!=draft.hour || b->minute!=draft.minute;
    draft.last_year=changed?0:b->last_year;
    if (changed || b->enabled!=draft.enabled)
        birthday_pending_mask&=(uint8_t)~(1u<<index);
    *b=draft;
    settings_mark_changed();
    birthday_status();
}

static void birthday_observe(const ds1302_datetime_t *dt, uint64_t now) {
    birthday_now=*dt;
    birthday_rtc_valid=true;
    uint8_t due=birthday_claim(birthdays,dt);
    if (due) {
        settings_mark_changed();
        if (birthday_rtc_primed) {
            birthday_pending_mask|=due;
            for(unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i)
                if(due&(1u<<i)) birthday_queued_ms[i]=now;
            printf("[BIRTHDAY] due_mask=0x%02x; wait up to 30m for idle/non-quiet audio\n",due);
        } else puts("[BIRTHDAY] Boot minute consumed without replay");
    }
    birthday_rtc_primed=true;
}

static void birthday_service(uint64_t now) {
    for(unsigned i=0;i<CLOCK_BIRTHDAY_COUNT;++i) {
        uint8_t bit=(uint8_t)(1u<<i);
        if (!(birthday_pending_mask&bit)) continue;
        if (birthday_wait_expired(birthday_queued_ms[i],now)) {
            birthday_pending_mask&=(uint8_t)~bit;
            printf("[BIRTHDAY] %u expired after 30m\n",i+1);
            continue;
        }
        if (!birthday_rtc_valid || auto_quiet_at(birthday_now.hour) || !sound_enabled ||
            timer.running || alarm_active_index>=0 || music_ui_open ||
            menu_state!=MENU_CLOSED || alarm_ui_state!=ALARM_UI_CLOSED ||
            !audio_queue_idle(&audio_queue) || dfplayer_busy() ||
            mp3_play_active() || mp3_probe_active() || sd_fat32_stream_active()) return;
#ifdef CLOCK_ENABLE_A2DP
        if(bt_speech_running || bt_timer_running || bt_alarm_running || routed_test_pending) return;
#endif
        audio_queue_t plan={0};
        audio_queue_enqueue(&plan,7,0); // Existing language-neutral birthday melody.
        if (!announce_info_plan(&plan)) return;
        birthday_pending_mask&=(uint8_t)~bit;
        snprintf(info_overlay_top,sizeof(info_overlay_top),"BIRTHDAY %u",i+1);
        snprintf(info_overlay_bottom,sizeof(info_overlay_bottom),"%02u.%02u CELEBRATE",
            (unsigned)birthdays[i].day,(unsigned)birthdays[i].month);
        info_overlay_until_ms=now+8000u;
        printf("[BIRTHDAY] %u playback requested via Manual output; file=07/000.mp3\n",i+1);
        return;
    }
}

static void menu_close(void) {
    if (menu_state == MENU_CLOSED) return;
#ifdef CLOCK_ENABLE_A2DP
    if (menu_state == MENU_BT_SCANNING || menu_state == MENU_BT_LIST ||
        menu_state == MENU_BT_CONFIRM) clock_a2dp_scan_stop();
#endif
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
            menu_audio_index = 0u;
            menu_state = MENU_AUDIO_LIST;
            printf("[MENU-AUDIO] Open: %s preferred=%s\n",
                   menu_audio_name(menu_audio_index),
                   menu_audio_selected_jbl(menu_audio_index) ? "BT" : "LOCAL");
            return;
        case 7:
            menu_bt_index = 0;
            menu_state = MENU_BT_LIST;
#ifdef CLOCK_ENABLE_A2DP
            printf("[MENU-BT] Open preferred=%s; select Scan devices\n",
                   clock_a2dp_target());
#else
            puts("[MENU-BT] Bluetooth not enabled in this build");
#endif
            return;
        case 8:
            birthday_index=0;
            menu_state=MENU_BIRTHDAY_LIST;
            return;
        case 9:
            display_light_draft = display_light;
            menu_state = MENU_DISPLAY_LIGHT_MODE;
            break;
        case 10:
#ifdef CLOCK_ENABLE_PHONE_WEB
            menu_state = MENU_PHONE_WEB;
            phone_menu_page = 0;
            return;
        case 11:
#endif
            menu_about_version = false;
            menu_state = MENU_ABOUT;
            puts("[MENU-ABOUT] Speaking Clock / Pico 2 W");
            return;
        default:
            return;
    }
    printf("[MENU] Edit %s\n", menu_item_name(menu_index));
}

static void menu_commit(uint64_t now) {
    menu_last_input_ms = now;
    switch (menu_state) {
        case MENU_TIMER_EDITOR: {
            uint32_t seconds;
            if (timer_editor_confirm(&timer_editor,&seconds)) {
                countdown_set_duration(&timer,seconds);
                menu_close();
                timer_acceleration=(timer_acceleration_t){0};
                timer_start_now(now);
                puts("[TIMER-EDIT] confirmed; countdown started");
            }
            return;
        }
        case MENU_BIRTHDAY_LIST:
            birthday_draft=birthdays[birthday_index];
            menu_state=MENU_BIRTHDAY_ENABLED;
            return;
        case MENU_BIRTHDAY_ENABLED: menu_state=MENU_BIRTHDAY_MONTH; return;
        case MENU_BIRTHDAY_MONTH: menu_state=MENU_BIRTHDAY_DAY; return;
        case MENU_BIRTHDAY_DAY: menu_state=MENU_BIRTHDAY_HOUR; return;
        case MENU_BIRTHDAY_HOUR: menu_state=MENU_BIRTHDAY_MINUTE; return;
        case MENU_BIRTHDAY_MINUTE:
            if (!birthday_valid(&birthday_draft)) return;
            birthday_apply(birthday_index,birthday_draft);
            menu_state=MENU_BIRTHDAY_LIST;
            return;
        case MENU_ABOUT:
            return; // Information only; rotate TIMER to change pages.
#ifdef CLOCK_ENABLE_PHONE_WEB
        case MENU_PHONE_WEB:
            if (phone_menu_page == 1u) {
                bool direct = !phone_web_direct();
                phone_menu_page = phone_web_set_direct(direct) && direct ? PHONE_PASSWORD_PAGE : 0u;
                menu_last_input_ms = to_ms_since_boot(get_absolute_time());
            } else if (phone_menu_page == PHONE_ACCESS_PAGE) {
                if (phone_web_begin_pair()) menu_state = MENU_PHONE_PAIR_CODE;
            } else if (phone_menu_page == PHONE_TRUST_PAGE && saved_wifi_ssid[0]) {
                phone_web_set_home_trusted(!phone_web_home_trusted());
            } else if (phone_menu_page == PHONE_FORGET_PAGE || phone_menu_page == PHONE_ROTATE_PAGE) {
                menu_state = MENU_PHONE_CONFIRM;
            }
            return;
        case MENU_PHONE_CONFIRM:
            if (phone_menu_page == PHONE_FORGET_PAGE) {
                phone_web_revoke();
            } else if (phone_web_rotate_password() && phone_web_direct()) {
                phone_menu_page = PHONE_PASSWORD_PAGE;
            }
            menu_state = MENU_PHONE_WEB;
            return;
        case MENU_PHONE_PAIR_CODE:
            return;
#endif
        case MENU_DISPLAY_LIGHT_MODE:
            if (display_light_draft.mode != DISPLAY_LIGHT_OFF) {
                menu_state = MENU_DISPLAY_LIGHT_DAY;
                return;
            }
            display_light = display_light_draft;
            settings_mark_changed();
            break;
        case MENU_DISPLAY_LIGHT_DAY:
            if (display_light_draft.mode == DISPLAY_LIGHT_AUTO) {
                menu_state = MENU_DISPLAY_LIGHT_NIGHT;
                return;
            }
            // ON uses the daytime level. Both modes retain the other brightness setting.
            /* fall through */
        case MENU_DISPLAY_LIGHT_NIGHT:
            display_light = display_light_draft;
            settings_mark_changed();
            break;
        case MENU_EDIT_LANGUAGE:
            if (language != menu_language_choice) {
                language = menu_language_choice;
                settings_mark_changed();
            }
            printf("[MENU] Language selected: %s\n", speech_language_name(language));
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
        case MENU_AUDIO_LIST:
            menu_audio_jbl_choice = menu_audio_selected_jbl(menu_audio_index);
            menu_state = MENU_EDIT_AUDIO_OUTPUT;
            printf("[MENU-AUDIO] Edit %s current=%s\n",
                   menu_audio_name(menu_audio_index),
                   menu_audio_jbl_choice ? "BT" : "LOCAL");
            return;
        case MENU_EDIT_AUDIO_OUTPUT:
            menu_audio_apply(menu_audio_index, menu_audio_jbl_choice);
            printf("[MENU-AUDIO] Saved %s=%s (Flash autosave)\n",
                   menu_audio_name(menu_audio_index),
                   menu_audio_selected_jbl(menu_audio_index) ? "BT" : "LOCAL");
            menu_state = MENU_AUDIO_LIST;
            return;
        case MENU_BT_LIST:
#ifdef CLOCK_ENABLE_A2DP
            if (menu_bt_index==bt_menu_count()-1u) {
                if (bt_user_disconnect()) puts("[MENU-BT] Disconnected; select a device to connect again");
            } else if (menu_bt_index == 0u) {
                if (!bt_sink_change_allowed()) {
                    puts("[MENU-BT] Scan blocked: timer/alarm/audio busy");
                } else if (bt_user_scan()) {
                    menu_state = MENU_BT_SCANNING;
                    puts("[MENU-BT] Scanning; previous sink disconnected");
                } else {
                    puts("[MENU-BT] Scan rejected: Bluetooth not ready");
                }
            } else {
                clock_bt_device_t d;
                if (bt_menu_device(menu_bt_index, &d)) {
                    menu_state = MENU_BT_CONFIRM;
                    printf("[MENU-BT] Confirm connect %s (%s)\n", d.name, d.address);
                }
            }
#else
            puts("[MENU-BT] Bluetooth disabled");
#endif
            return;
        case MENU_BT_CONFIRM:
#ifdef CLOCK_ENABLE_A2DP
            {
                clock_bt_device_t d;
                if (menu_bt_index > 0u &&
                    bt_menu_device(menu_bt_index, &d) &&
                    bt_sink_select(d.address))
                    printf("[MENU-BT] Connecting %s\n", d.name);
                else
                    puts("[MENU-BT] Selection failed; retry");
            }
#endif
            menu_state = MENU_BT_LIST;
            return;
        case MENU_BT_SCANNING:
            // Search is asynchronous. Wait or cancel with MINUS.
            return;
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
    } else if (menu_state == MENU_TIMER_EDITOR) {
        if (timer_editor_back(&timer_editor)) menu_close();
    } else if(menu_state==MENU_BIRTHDAY_LIST) {
        menu_state=MENU_LIST;
    } else if(menu_state>=MENU_BIRTHDAY_ENABLED && menu_state<=MENU_BIRTHDAY_MINUTE) {
        if(menu_state==MENU_BIRTHDAY_ENABLED) menu_state=MENU_BIRTHDAY_LIST;
        else menu_state=(menu_state_t)(menu_state-1);
    } else if (menu_state == MENU_ABOUT) {
        menu_state = MENU_LIST;
        puts("[MENU-ABOUT] Back to settings");
#ifdef CLOCK_ENABLE_PHONE_WEB
    } else if (menu_state == MENU_PHONE_WEB) {
        menu_state = MENU_LIST;
    } else if (menu_state == MENU_PHONE_PAIR_CODE || menu_state == MENU_PHONE_CONFIRM) {
        menu_state = MENU_PHONE_WEB;
#endif
    } else if (menu_state == MENU_EDIT_AUDIO_OUTPUT) {
        puts("[MENU-AUDIO] Choice discarded");
        menu_state = MENU_AUDIO_LIST;
    } else if (menu_state == MENU_AUDIO_LIST) {
        menu_state = MENU_LIST;
        puts("[MENU-AUDIO] Back to settings");
    } else if (menu_state == MENU_BT_CONFIRM) {
        menu_state = MENU_BT_LIST;
        puts("[MENU-BT] Connection choice cancelled");
    } else if (menu_state == MENU_BT_SCANNING) {
#ifdef CLOCK_ENABLE_A2DP
        clock_a2dp_scan_stop();
#endif
        menu_state = MENU_BT_LIST;
        puts("[MENU-BT] Search cancelled");
    } else if (menu_state == MENU_BT_LIST) {
#ifdef CLOCK_ENABLE_A2DP
        clock_a2dp_scan_stop(); // Browsing never changes the explicit connection policy.
#endif
        menu_state = MENU_LIST;
        puts("[MENU-BT] Back to settings");
    } else {
        printf("[MENU] Cancelled edit: %s\n", menu_item_name(menu_index));
        menu_state = MENU_LIST;
    }
}

static void menu_rotate(int direction, uint64_t now) {
    if (menu_state == MENU_CLOSED) return;
    menu_last_input_ms = now;
    switch (menu_state) {
        case MENU_TIMER_EDITOR:
            timer_editor_rotate(&timer_editor,direction);
            break;
        case MENU_LIST:
            menu_index = (uint8_t)((menu_index + SETTINGS_MENU_ITEMS +
                                  (direction > 0 ? 1u : SETTINGS_MENU_ITEMS - 1u)) %
                                  SETTINGS_MENU_ITEMS);
            printf("[MENU] Item: %s\n", menu_item_name(menu_index));
            break;
        case MENU_BIRTHDAY_LIST:
            birthday_index=wrap_u8(birthday_index,0,CLOCK_BIRTHDAY_COUNT-1,direction);
            break;
        case MENU_BIRTHDAY_ENABLED: birthday_draft.enabled=!birthday_draft.enabled; break;
        case MENU_BIRTHDAY_MONTH:
            birthday_draft.month=wrap_u8(birthday_draft.month,1,12,direction);
            if(birthday_draft.day>menu_month_days(2000,birthday_draft.month))
                birthday_draft.day=menu_month_days(2000,birthday_draft.month);
            break;
        case MENU_BIRTHDAY_DAY:
            birthday_draft.day=wrap_u8(birthday_draft.day,1,menu_month_days(2000,birthday_draft.month),direction);
            break;
        case MENU_BIRTHDAY_HOUR: birthday_draft.hour=wrap_u8(birthday_draft.hour,0,23,direction); break;
        case MENU_BIRTHDAY_MINUTE: birthday_draft.minute=wrap_u8(birthday_draft.minute,0,59,direction); break;
        case MENU_ABOUT:
            menu_about_version = !menu_about_version;
            if (menu_about_version)
                printf("[MENU-ABOUT] v%s / by FlintUA\n", CLOCK_FIRMWARE_VERSION);
            else
                puts("[MENU-ABOUT] Speaking Clock / Pico 2 W");
            break;
#ifdef CLOCK_ENABLE_PHONE_WEB
        case MENU_PHONE_WEB:
            phone_menu_page = wrap_u8(phone_menu_page, 0,
                phone_web_direct() ? PHONE_PASSWORD_PAGE : PHONE_ROTATE_PAGE, direction);
            break;
        case MENU_PHONE_PAIR_CODE:
        case MENU_PHONE_CONFIRM:
            break;
#endif
        case MENU_DISPLAY_LIGHT_MODE:
            display_light_draft.mode = wrap_u8(display_light_draft.mode,
                DISPLAY_LIGHT_OFF, DISPLAY_LIGHT_AUTO, direction);
            break;
        case MENU_DISPLAY_LIGHT_DAY:
            display_light_draft.day_percent =
                5u * wrap_u8(display_light_draft.day_percent / 5u, 0, 20, direction);
            break;
        case MENU_DISPLAY_LIGHT_NIGHT:
            display_light_draft.night_percent =
                5u * wrap_u8(display_light_draft.night_percent / 5u, 0, 20, direction);
            break;
        case MENU_EDIT_LANGUAGE:
            menu_language_choice = menu_language_choice == SPEECH_RU ? SPEECH_DE : SPEECH_RU;
            printf("[MENU] Language candidate: %s\n", speech_language_name(menu_language_choice));
            break;
        case MENU_BT_LIST:
#ifdef CLOCK_ENABLE_A2DP
            {
                unsigned count = bt_menu_count();
                menu_bt_index = (uint8_t)((menu_bt_index + count +
                                 (direction > 0 ? 1u : count - 1u)) % count);
                clock_bt_device_t device;
                if (menu_bt_index && bt_menu_device(menu_bt_index, &device))
                    printf("[MENU-BT] index=%u/%u name=%s MAC=%s\n",
                           (unsigned)(menu_bt_index + 1u), count,
                           device.name, device.address);
                else if(menu_bt_index==count-1u)
                    printf("[MENU-BT] index=%u/%u Disconnect\n",(unsigned)(menu_bt_index+1u),count);
                else
                    printf("[MENU-BT] index=%u/%u Scan devices\n",
                           (unsigned)(menu_bt_index + 1u), count);
            }
#endif
            break;
        case MENU_AUDIO_LIST:
            menu_audio_index = (uint8_t)((menu_audio_index + SETTINGS_AUDIO_ITEMS +
                                (direction > 0 ? 1u : SETTINGS_AUDIO_ITEMS - 1u)) %
                                SETTINGS_AUDIO_ITEMS);
            printf("[MENU-AUDIO] Item=%s preferred=%s\n",
                   menu_audio_name(menu_audio_index),
                   menu_audio_selected_jbl(menu_audio_index) ? "BT" : "LOCAL");
            break;
        case MENU_EDIT_AUDIO_OUTPUT:
#ifdef CLOCK_ENABLE_A2DP
            menu_audio_jbl_choice = !menu_audio_jbl_choice;
#else
            menu_audio_jbl_choice = false;
#endif
            printf("[MENU-AUDIO] %s candidate=%s\n",
                   menu_audio_name(menu_audio_index),
                   menu_audio_jbl_choice ? "BT" : "LOCAL");
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
        bt_volume = saved.bt_volume;
#ifdef CLOCK_ENABLE_A2DP
        clock_a2dp_set_volume(bt_volume); // Before A2DP connects: PCM-only initial state.
#endif
        exact_hour_phrase = saved.exact_hour;
        quiet_enabled = saved.quiet_enabled;
        quiet_start = saved.quiet_start;
        quiet_end = saved.quiet_end;
        half_hour_enabled = saved.half_hour_enabled;
        clock_mode = saved.clock_mode;
        display_light = saved.display_light;
        memcpy(alarms, saved.alarms, sizeof(alarms));
        memcpy(birthdays,saved.birthdays,sizeof(birthdays));
        memcpy(saved_bt_target_addr, saved.bt_target_addr,
               sizeof(saved_bt_target_addr));
        bt_known_count = saved.bt_known_count;
        memcpy(bt_known, saved.bt_known, sizeof(bt_known));
        memcpy(saved_wifi_ssid,saved.wifi_ssid,sizeof(saved_wifi_ssid));
        memcpy(saved_wifi_password,saved.wifi_password,sizeof(saved_wifi_password));
#ifdef CLOCK_ENABLE_PHONE_WEB
        phone_web_load_settings(&saved.phone);
#else
        saved_phone = saved.phone;
#endif
        // v5/v6 stored only the preferred MAC. Keep it selectable by MAC
        // until a fresh scan supplies its advertised name.
        bool legacy_target = false, cleared_target = true;
        for (unsigned k = 0; k < sizeof(saved_bt_target_addr); ++k) {
            legacy_target |= saved_bt_target_addr[k] != 0u;
            cleared_target &= saved_bt_target_addr[k] == 0xffu;
        }
        if (!bt_known_count && legacy_target && !cleared_target) {
            memcpy(bt_known[0].address, saved_bt_target_addr, 6);
            snprintf(bt_known[0].name, sizeof(bt_known[0].name),
                     "%02X:%02X:%02X:%02X:%02X:%02X",
                     saved_bt_target_addr[0], saved_bt_target_addr[1],
                     saved_bt_target_addr[2], saved_bt_target_addr[3],
                     saved_bt_target_addr[4], saved_bt_target_addr[5]);
            bt_known_count = 1u;
        }
#ifdef CLOCK_ENABLE_A2DP
        // All zeroes in a legacy record mean "use build-time sink".
        bool address_saved = false, target_cleared = true;
        for (unsigned k = 0; k < sizeof(saved_bt_target_addr); ++k)
            address_saved |= saved_bt_target_addr[k] != 0;
        for (unsigned k = 0; k < sizeof(saved_bt_target_addr); ++k)
            target_cleared &= saved_bt_target_addr[k] == 0xffu;
        if (target_cleared) {
            clock_a2dp_clear_target();
            puts("[BT-POLICY] auto=OFF at boot; explicit Connect required");
        } else if (address_saved) {
            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                     saved_bt_target_addr[0], saved_bt_target_addr[1],
                     saved_bt_target_addr[2], saved_bt_target_addr[3],
                     saved_bt_target_addr[4], saved_bt_target_addr[5]);
            if (!clock_a2dp_select_target(mac))
                puts("[BT] Saved sink MAC rejected; using build-time default");
            else
                printf("[FLASH] preferred Bluetooth sink=%s\n", mac);
        }
#endif
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
#ifdef CLOCK_ENABLE_A2DP
    clock_a2dp_boot_hold(false);
#endif
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
    printf("[ALARM] active=%d (0=none)\n",
           alarm_active_index + 1);
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        const alarm_entry_t *a = &alarms[i];
        printf("[ALARM] %u enabled=%s time=%02u:%02u sound=%s track=%u days=%s mask=0x%02x valid=%s\n",
               i + 1u, a->enabled ? "ON" : "OFF",
               (unsigned)a->hour, (unsigned)a->minute,
               a->sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL",
               (unsigned)a->track,alarm_days_name(a->weekdays),(unsigned)a->weekdays,
               alarm_entry_valid(a) ? "YES" : "NO");
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
            puts("[BT-ALARM] BT unavailable/busy; choosing LOCAL");
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
    active_alarm_hour = hour;
    active_alarm_minute = minute;
    active_alarm_test = test;
    // Direct test calls start immediately; deferred events preserve original due.
    if (!active_alarm_due_ms || active_alarm_due_ms > now)
        active_alarm_due_ms = now;
    alarm_deadline_ms = now + ALARM_SAFETY_TIMEOUT_MS;
    alarm_audio_armed = true;
#ifdef CLOCK_ENABLE_A2DP
    if (bt_alarm_prepare(&plan)) {
        printf("[ALARM-AUDIO] output=BT%s\n", test ? " TEST" : "");
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
    active_alarm_due_ms = 0u;
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
        active_alarm_due_ms = 0u;
        puts("[ALARM] finished=AUDIO_COMPLETED");
    }
}

static void alarm_service_rtc(const ds1302_datetime_t *dt, uint64_t now) {
    if (!dt) return;
    uint64_t civil_epoch;
    if (rtc_replay_guard_until && clock_datetime_to_utc(dt,&civil_epoch) &&
        civil_epoch < rtc_replay_guard_until) return;
    if (!alarm_boot_seeded) {
        // Never replay an RTC minute that was already in progress at boot.
        alarm_boot_seeded = true;
        alarm_scheduler.minute_valid = true;
        alarm_scheduler.minute_key = auto_minute_key(dt);
        alarm_scheduler.fired_mask = 0u;
        for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
            if (alarms[i].enabled && alarms[i].hour == dt->hour &&
                alarms[i].minute == dt->minute)
                alarm_scheduler.fired_mask |= (uint8_t)(1u << i);
        }
        printf("[ALARM] boot: %02u:%02u consumed, fired_mask=0x%02x; next minute armed\n",
               (unsigned)dt->hour, (unsigned)dt->minute,
               (unsigned)alarm_scheduler.fired_mask);
        return;
    }
    if (dt->second > 5u) return;
    // Claim all due alarms once, even when a timer or another alarm is active.
    // A single RTC minute is never replayed; each due slot is queued at most once.
    uint8_t other_mask = 0u;
    int index = alarm_scheduler_claim_due(&alarm_scheduler, alarms, dt->year,
                         dt->month, dt->day, dt->hour, dt->minute, &other_mask);
    if (index < 0) return;
    uint8_t due_mask = (uint8_t)((1u << index) | other_mask);
    if (other_mask)
        printf("[ALARM] collision %02u:%02u queued_mask=0x%02x\n",
               (unsigned)dt->hour, (unsigned)dt->minute, due_mask);
    // A due alarm owns this boundary, even if its sound must be deferred.
    if (dt->minute == 0u || dt->minute == 30u) {
        auto_seen_key_valid = true;
        auto_seen_minute_key = auto_minute_key(dt);
    }
    if (!sound_enabled) {
        printf("[ALARM] due_mask=0x%02x skipped=SOUND_OFF\n", due_mask);
        return;
    }
    for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) {
        if (due_mask & (1u << i))
            alarm_pending_add(i, dt->hour, dt->minute, now, false);
    }
}

static void alarm_consume_current_minute(unsigned index) {
    ds1302_datetime_t dt;
    if(!ds1302_read_datetime(&dt) || !alarms[index].enabled ||
       dt.hour!=alarms[index].hour || dt.minute!=alarms[index].minute) return;
    uint64_t key=auto_minute_key(&dt);
    if(!alarm_scheduler.minute_valid || alarm_scheduler.minute_key!=key) {
        alarm_scheduler.minute_valid=true; alarm_scheduler.minute_key=key;
        alarm_scheduler.fired_mask=0;
    }
    alarm_scheduler.fired_mask|=(uint8_t)(1u<<index);
}
static void alarm_days_command(char *args) {
    unsigned slot; char text[16], extra; uint8_t mask;
    if(sscanf(args,"%u %15s %c",&slot,text,&extra)!=2 || slot<1 ||
       slot>CLOCK_ALARM_COUNT || !alarm_days_parse(text,&mask)) {
        puts("[ALARM] alarm-days N daily|weekdays|weekends|135 (1=Mon,7=Sun)"); return;
    }
    if(alarm_active_index>=0 || alarm_ui_state!=ALARM_UI_CLOSED || music_ui_open) {
        puts("[ALARM] close editor/player and stop ringing before configuration"); return;
    }
    if(alarms[slot-1].weekdays!=mask) {
        alarms[slot-1].weekdays=mask;
        alarm_clear_waiting(slot-1); alarm_consume_current_minute(slot-1);
        settings_mark_changed();
    }
    alarm_status();
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
        puts("[ALARM] Use alarm-set HH MM signal OR alarm-set HH MM music TRACK(1..255)");
        return;
    }
    if (alarm_active_index >= 0 || alarm_ui_state != ALARM_UI_CLOSED ||
        music_ui_open) {
        puts("[ALARM] Stop ringing alarm or close editor/player before configuration");
        return;
    }
    alarm_clear_waiting(0);
    alarms[0].enabled = true;
    alarms[0].hour = (uint8_t)hh;
    alarms[0].minute = (uint8_t)mm;
    alarms[0].sound = music ? ALARM_SOUND_MUSIC : ALARM_SOUND_SIGNAL;
    alarms[0].track = music ? (uint8_t)track : 1u;
    // Consume only this slot; preserve other already-fired alarms.
    alarm_consume_current_minute(0);
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
    alarm_clear_waiting(slot-1u);
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
    bool enabled=strcmp(action,"on")==0;
    if(a->enabled!=enabled) alarm_clear_waiting(slot-1u);
    a->enabled = enabled;
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
    if(birthday_pending_mask) {
        puts("[BOOTSEL] REFUSED: birthday notification waiting; check birthday-status");
        return false;
    }
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
        // Compatibility alias uses the same explicit/persistent policy as bt-use.
        const char *address = line + 11;
        if (!bt_sink_select(address))
            puts("[BT] Direct connect rejected; use bt-use MAC for persistent selection");
    } else if (strcmp(line,"bt-disconnect")==0) {
        (void)bt_user_disconnect();
    } else if (strcmp(line, "bt-scan") == 0) {
        if (!bt_sink_change_allowed())
            puts("[BT-SCAN] Refused: timer/alarm/audio busy");
        else if (!bt_user_scan())
            puts("[BT-SCAN] Not ready or scan already active");
        else puts("[BT-SCAN] Started, run bt-devices after ~10s");
    } else if (strcmp(line, "bt-scan-stop") == 0) {
        clock_a2dp_scan_stop();
        puts("[BT-SCAN] Stopped");
    } else if (strcmp(line, "bt-devices") == 0) {
        bt_scan_list();
    } else if (strcmp(line, "bt-known") == 0) {
        bt_scan_list();
    } else if (strcmp(line, "bt-target") == 0) {
        printf("[BT] preferred=%s stream=%s pending_validation=%s\n",
               clock_a2dp_target(),
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               bt_pending_target_valid ? "YES" : "NO");
    } else if (strncmp(line, "bt-use ", 7) == 0) {
        const char *argument = line + 7;
        unsigned index = 0u;
        char extra = 0;
        if (sscanf(argument, "%u %c", &index, &extra) == 1 && index > 0u) {
            clock_bt_device_t d;
            if (!bt_menu_device(index, &d) ||
                !bt_sink_select(d.address))
                puts("[BT-SELECT] Invalid index or selection refused");
        } else {
            // Resolve exact discovered device name to MAC. Never guess when
            // multiple devices advertise the same name.
            clock_bt_device_t device, selected;
            unsigned matches = 0;
            for (unsigned i = 1u; i+1u < bt_menu_count(); ++i) {
                if (bt_menu_device(i, &device) && strcmp(device.name, argument) == 0) {
                    selected = device;
                    ++matches;
                }
            }
            if (matches == 1u) {
                if (!bt_sink_select(selected.address))
                    puts("[BT-SELECT] Connection refused");
            } else if (matches > 1u) {
                puts("[BT-SELECT] Ambiguous name: choose an index or MAC");
            } else if (!bt_sink_select(argument)) {
                puts("[BT-SELECT] Use bt-use N, bt-use <exact discovered name>, or bt-use XX:XX:XX:XX:XX:XX");
            }
        }
    } else if (strcmp(line, "bt-forget") == 0) {
        puts("[BT-KNOWN] Usage: bt-forget <known device number or MAC>; run bt-known first");
        bt_scan_list();
    } else if (strncmp(line, "bt-forget ", 10) == 0) {
        const char *argument = line + 10;
        unsigned index = 0u;
        char extra = 0;
        int found = -1;
        if (sscanf(argument, "%u %c", &index, &extra) == 1 &&
            index > 0u && index <= bt_known_count) found = (int)(index - 1u);
        for (unsigned i = 0; found < 0 && i < bt_known_count; ++i) {
            clock_bt_device_t known;
            bt_menu_device(i + 1u, &known);
            if (strcmp(argument, known.address) == 0) found = (int)i;
        }
        if (found < 0 || !bt_sink_change_allowed()) {
            puts("[BT-KNOWN] Unknown device or audio busy");
        } else {
            clock_bt_device_t known;
            bt_menu_device((unsigned)found + 1u, &known);
            if (clock_a2dp_forget(known.address)) {
                if (memcmp(saved_bt_target_addr, bt_known[found].address, 6) == 0)
                    memset(saved_bt_target_addr, 0xff, 6);
                if (bt_pending_target_valid &&
                    memcmp(bt_pending_target_addr, bt_known[found].address, 6) == 0)
                    bt_pending_target_valid = false;
                memmove(&bt_known[found], &bt_known[found + 1u],
                        (bt_known_count - (unsigned)found - 1u) * sizeof(bt_known[0]));
                --bt_known_count;
                memset(&bt_known[bt_known_count], 0, sizeof(bt_known[0]));
                settings_mark_changed();
                printf("[BT-KNOWN] Forgotten %s; Flash autosave pending\n", known.address);
            }
        }
    } else if (strcmp(line, "bt-volume") == 0) {
        printf("[BT-VOL] preferred=%u/30 AVRCP=%s remote_absolute=%u PCM_fallback=%s\n",
               (unsigned)bt_volume,
               clock_a2dp_avrcp_available() ? "CONNECTED" : "OFF",
               (unsigned)clock_a2dp_remote_volume(),
               clock_a2dp_pcm_volume_fallback() ? "YES" : "NO");
    } else if (strncmp(line, "bt-vol ", 7) == 0) {
        unsigned int requested = 0u;
        char extra = 0;
        if (sscanf(line + 7, "%u %c", &requested, &extra) != 1 || requested > 30u)
            puts("[BT-VOL] Usage: bt-vol <0..30>");
        else set_output_volume(true, (uint8_t)requested);
    } else if (strcmp(line, "bt-status") == 0) {
        printf("[BT] preferred=%s\n", clock_a2dp_target());
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
        printf("[AUDIO-HEALTH] timer=%s alarm=%d sound=%s local_queue=%u local_started=%s df_busy=%s\n",
               timer.running ? "RUNNING" : "IDLE", alarm_active_index + 1,
               sound_enabled ? "ON" : "OFF", (unsigned)audio_queue.size,
               audio_queue.track_started ? "YES" : "NO",
               dfplayer_busy() ? "YES" : "NO");
        alarm_pending_status(now);
        printf("[AUDIO-HEALTH] auto=%s clock_mode=%s saved_pending=%s menu=%s music=%s\n",
               auto_enabled ? "ON" : "OFF", clock_mode ? "CHIME" : "VOICE",
               autosave_pending ? "YES" : "NO",
               menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED
                 ? "OPEN" : "CLOSED", music_ui_open ? "OPEN" : "CLOSED");
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUDIO-HEALTH] BT stream=%s rate=%lu MP3=%s speech=%s (%u/%u) timer=%s (%u/%u) alarm=%s (%u/%u) route_test=%s\n",
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
               audio_router.requested == AUDIO_OUTPUT_BT_JBL ? "BT" : "LOCAL",
#ifdef CLOCK_ENABLE_A2DP
               auto_voice_jbl_enabled ? "BT" : "LOCAL",
               timer_audio_jbl_enabled ? "BT" : "LOCAL",
               alarm_audio_jbl_enabled ? "BT" : "LOCAL"
#else
               "LOCAL", "LOCAL", "LOCAL"
#endif
        );
        printf("[AUDIO-SETTINGS] volume LOCAL=%u/30 BT=%u/30\n",
               (unsigned)volume, (unsigned)bt_volume);
        printf("[AUDIO-SETTINGS] output=preferred, effective BT requires A2DP; autosave=%s\n",
               autosave_pending ? "PENDING" : "IDLE");
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUDIO-SETTINGS] Bluetooth stream=%s rate=%luHz; LOCAL fallback enabled\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               (unsigned long)clock_a2dp_current_rate());
#endif
    } else if ((strcmp(line, "timer-audio bt") == 0 || strcmp(line, "timer-audio jbl") == 0)) {
#ifdef CLOCK_ENABLE_A2DP
        timer_audio_jbl_enabled = true;
        settings_mark_changed();
        printf("[TIMER-AUDIO] output=BT_PREFERRED BT=%s LOCAL fallback (Flash autosave)\n",
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
               timer_audio_jbl_enabled ? "BT" : "LOCAL",
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
                printf("[AUTO-AUDIO] TEST %02u:00 via BT (scheduler unchanged)\n",
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
    } else if ((strcmp(line, "auto-audio bt") == 0 || strcmp(line, "auto-audio jbl") == 0)) {
#ifdef CLOCK_ENABLE_A2DP
        auto_voice_jbl_enabled = true;
        settings_mark_changed();
        printf("[AUTO-AUDIO] Hourly VOICE output=BT_PREFERRED, stream=%s; "
               "LOCAL fallback (Flash autosave)\n",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE");
        puts("[AUTO-AUDIO] Chime/half-hour stay LOCAL; music follows Manual; timer/alarm have independent outputs");
#else
        puts("[AUTO-AUDIO] Requires Bluetooth A2DP");
#endif
    } else if (strcmp(line, "auto-audio") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        printf("[AUTO-AUDIO] hourly_voice=%s stream=%s "
               "(Flash autosave; other automatic events LOCAL)\n",
               auto_voice_jbl_enabled ? "BT_PREFERRED" : "LOCAL",
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
    } else if ((strcmp(line, "audio-select bt") == 0 || strcmp(line, "audio-select jbl") == 0)) {
        audio_router_select(&audio_router, AUDIO_OUTPUT_BT_JBL);
        audio_router_bluetooth_state(&audio_router, clock_a2dp_stream_active());
        settings_mark_changed();
        puts("[AUDIO-ROUTE] Requested BT for manual say and route-test only (alarm-audio separate)");
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
                    printf("[AUDIO-ROUTE] TEST BT %s (automatic events still LOCAL)\n",
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
               alarm_audio_jbl_enabled ? "BT" : "LOCAL",
               bt_alarm_running ? "YES" : "NO",
               (unsigned)(bt_alarm_running ? bt_alarm_index + 1u : 0u),
               (unsigned)(bt_alarm_running ? bt_alarm_count : 0u));
        printf("[BT-TIMER] preferred=%s running=%s segment=%u/%u\n",
               timer_audio_jbl_enabled ? "BT" : "LOCAL",
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
        puts("[MP3] mp3-test 07/001.mp3 - experimental decode to RAM (no BT output)");
        puts("[MP3] mp3-play 07/001.mp3 - full file on BT; mp3-pause; mp3-resume; mp3-stop; mp3-status");
        puts("[MP3] mp3-pause-test 07/001.mp3 - auto pause at 300ms, auto resume after 1s");
        puts("[AUDIO-ROUTE] audio-select local|bt; audio-route; audio-route-test 7 1; say 08 25");
        puts("[AUTO-AUDIO] auto-audio bt|local; auto-audio (only automatic hourly VOICE)");
        puts("[AUTO-AUDIO] auto-audio-test 8 - play simulated 08:00 hourly voice, no RTC change");
        puts("[TIMER-AUDIO] timer-audio bt|local; timer-audio - timer start/cancel/finish routing");
        puts("[ALARM-AUDIO] alarm-audio bt|local; alarm-audio; alarm-audio-test 1; alarm-stop");
        puts("[AUDIO-SETTINGS] audio-settings shows persisted preferences; change with audio-select, auto-audio, timer-audio, alarm-audio; save");
        puts("[BT] SETTINGS > Bluetooth: known devices, scan, connect, Disconnect; CLI: bt-disconnect, bt-scan, bt-devices, bt-use N|name|MAC, bt-forget N|MAC, bt-target");
        puts("[BT-VOL] bt-volume shows independent Bluetooth level; bt-vol 0..30 sets it; VOLUME encoder follows audio route");
        puts("[AUDIO-HEALTH] audio-health shows read-only owner, queue and Bluetooth status");
        puts("[ALARM-PENDING] alarm-pending, alarm-pending-test N, alarm-pending-clear (RAM only)");
        puts("[ALARM] alarm-days N daily|weekdays|weekends|135; alarm-snooze; alarm-snooze-status; alarm-snooze-cancel");
        printf("[SNOOZE] ringing: GP28 snoozes %u min; ALARM stops. On clock screen ALARM cancels snoozes\n",
               CLOCK_ALARM_SNOOZE_MINUTES);
        puts("[CMD] t <seconds>, start, cancel, status, say [HH MM], lang ru|de, exact on|off, play F T, pause, audio, vol N, sound, menu, flash-info, flash-inspect, flash-tail, autosave-status, quiet-status, mode-status, mode voice|chime, alarm-status, alarm-set HH MM signal|music [track], alarm-config N HH MM signal|music [track], alarm-enable N on|off, alarm-off, alarm-stop, alarm-test HH MM [slot], auto-status, auto-debug, auto-test HH MM, auto on|off, rtc-debug, rtc-reset-stats, save, help");
        puts("[NETWORK] wifi-config SSID|password; wifi-clear; ntp-status; ntp-sync; version");
#ifdef CLOCK_ENABLE_PHONE_WEB
        puts("[PHONE-WEB] phone-web-status shows the active network and IPv4 address");
#endif
        puts("[DISPLAY-LIGHT] display-light-status shows mode and PWM level");
        puts("[INPUT] ALARM short=alarm editor; ALARM hold >=900ms=music; MUSIC TIMER=play/stop, encoder/SETTINGS/MINUS=next/previous, ALARM=mode; hold ALARM/MODE/MINUS=exit");
        puts("[INPUT] GP28=timer H/M/S editor; TIMER/SETTINGS=next/start; MINUS=back; GP28=cancel; quick encoder=1/5/10 min");
        puts("[INPUT] VOLUME push=sound toggle in clock/settings/player/editor; ringing alarm uses ALARM=stop");
        puts("[INFO] MINUS short=date, hold >=900ms=next alarm on idle clock; say-date / next-alarm via USB");
        puts("[BIRTHDAY] birthday-status; birthday-set N DD MM HH MM on|off; birthday-test N; birthday-pending-clear");
        puts("[BOOTSEL] bootsel arm -> bootsel confirm within 15s (USB required; refuses during busy/autosave)");
    } else if (strcmp(line,"birthday-status")==0) {
        birthday_status();
    } else if (strncmp(line,"birthday-set ",13)==0) {
        unsigned n,day,month,hour,minute;
        char action[4],extra;
        if (sscanf(line+13,"%u %u %u %u %u %3s %c",&n,&day,&month,&hour,&minute,action,&extra)!=6 ||
            n<1 || n>CLOCK_BIRTHDAY_COUNT || day>31 || month>12 || hour>23 || minute>59 ||
            (strcmp(action,"on") && strcmp(action,"off"))) {
            puts("[BIRTHDAY] Usage: birthday-set N DD MM HH MM on|off");
        } else if (menu_state!=MENU_CLOSED) puts("[BIRTHDAY] Close settings before USB configuration");
        else {
            birthday_t draft={.enabled=strcmp(action,"on")==0,.day=(uint8_t)day,.month=(uint8_t)month,
                .hour=(uint8_t)hour,.minute=(uint8_t)minute};
            if (!birthday_valid(&draft)) puts("[BIRTHDAY] Invalid date/time");
            else birthday_apply(n-1,draft);
        }
    } else if (strncmp(line,"birthday-test ",14)==0) {
        unsigned n; char extra;
        if(sscanf(line+14,"%u %c",&n,&extra)!=1 || n<1 || n>CLOCK_BIRTHDAY_COUNT)
            puts("[BIRTHDAY] Usage: birthday-test 1..5 (RAM only; same quiet/priority rules)");
        else if(birthday_pending_mask&(1u<<(n-1))) puts("[BIRTHDAY] Already pending; test refused");
        else {
            birthday_pending_mask|=(uint8_t)(1u<<(n-1));
            birthday_queued_ms[n-1]=now;
            puts("[BIRTHDAY] TEST queued in RAM; saved date/year unchanged");
        }
    } else if(strcmp(line,"birthday-pending-clear")==0) {
        birthday_pending_mask=0;
        puts("[BIRTHDAY] Waiting notifications cleared; saved dates/year unchanged");
    } else if (strcmp(line, "say-date") == 0) {
        announce_clock_info(false);
    } else if (strcmp(line, "next-alarm") == 0) {
        announce_clock_info(true);
    } else if (strcmp(line, "version") == 0) {
        printf("[VERSION] %s build=%s board=pico2_w\n",
               CLOCK_FIRMWARE_VERSION,CLOCK_BUILD_ID);
    } else if (strcmp(line, "wifi-config") == 0) {
        puts("[WIFI] Usage: wifi-config SSID|password (empty password for an open network)");
    } else if (strncmp(line, "wifi-config ",12) == 0) {
#ifdef CLOCK_ENABLE_WIFI
        const char *ssid=line+12, *separator=strchr(ssid,'|');
        size_t n=separator?(size_t)(separator-ssid):0;
        size_t key_size=separator?strlen(separator+1):0;
        if (timer.running || alarm_active_index>=0 || mp3_play_active() ||
            !audio_queue_idle(&audio_queue) || dfplayer_busy()) {
            puts("[WIFI] Configuration blocked while timer/alarm/audio is busy");
        } else if (!separator || n<1 || n>32 || key_size>64 ||
            (key_size>0 && key_size<8) ||
            (key_size==64 && strspn(separator+1,"0123456789abcdefABCDEF")!=64)) {
            puts("[WIFI] Invalid configuration; SSID 1..32, password empty or 8..64 characters");
        } else {
            if (strlen(saved_wifi_ssid) != n || memcmp(saved_wifi_ssid, ssid, n) != 0 ||
                strcmp(saved_wifi_password, separator + 1) != 0) {
#ifdef CLOCK_ENABLE_PHONE_WEB
                phone_web_set_home_trusted(false);
#else
                saved_phone.home_trusted = 0;
#endif
            }
            memset(saved_wifi_ssid,0,sizeof(saved_wifi_ssid));
            memset(saved_wifi_password,0,sizeof(saved_wifi_password));
            memcpy(saved_wifi_ssid,ssid,n);
            memcpy(saved_wifi_password,separator+1,key_size);
            settings_mark_changed();
            network_time_configure(saved_wifi_ssid,saved_wifi_password);
        }
#else
        puts("[WIFI] Network support is disabled in this build");
#endif
    } else if (strcmp(line,"wifi-clear") == 0) {
        memset(saved_wifi_ssid,0,sizeof(saved_wifi_ssid));
        memset(saved_wifi_password,0,sizeof(saved_wifi_password));
#ifdef CLOCK_ENABLE_PHONE_WEB
        phone_web_set_home_trusted(false);
#else
        saved_phone.home_trusted = 0;
#endif
        settings_mark_changed();
#ifdef CLOCK_ENABLE_WIFI
        network_time_configure("","");
#endif
#ifdef CLOCK_ENABLE_PHONE_WEB
    } else if (strcmp(line, "phone-web-status") == 0) {
        phone_web_status();
#endif
    } else if (strcmp(line, "display-light-status") == 0) {
        display_light_status();
    } else if (strcmp(line,"ntp-status") == 0 || strcmp(line,"ntp-sync") == 0) {
#ifdef CLOCK_ENABLE_WIFI
        if (strcmp(line,"ntp-sync")==0) network_time_request();
        network_time_status();
#else
        puts("[NTP] Network support is disabled in this build");
#endif
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
        alarm_pending_status(now);
        alarm_snooze_status(now);
    } else if (strncmp(line,"alarm-days ",11)==0) {
        alarm_days_command(line+11);
    } else if(strcmp(line,"alarm-snooze")==0) {
        alarm_snooze_start(now);
    } else if(strcmp(line,"alarm-snooze-status")==0) {
        alarm_snooze_status(now);
    } else if(strcmp(line,"alarm-snooze-cancel")==0) {
        alarm_snooze_cancel_all();
    } else if (strcmp(line, "alarm-pending") == 0) {
        alarm_pending_status(now);
    } else if (strcmp(line, "alarm-pending-clear") == 0) {
        for (unsigned i = 0; i < CLOCK_ALARM_COUNT; ++i) alarm_clear_waiting(i);
        puts("[ALARM-PENDING] cleared by user (RAM only, alarm schedule unchanged)");
    } else if (strncmp(line, "alarm-pending-test ", 19) == 0) {
        unsigned slot = 0;
        char extra = 0;
        if (sscanf(line + 19, "%u %c", &slot, &extra) != 1 ||
            slot < 1u || slot > CLOCK_ALARM_COUNT) {
            puts("[ALARM-PENDING] Usage: alarm-pending-test 1..5");
        } else {
            ds1302_datetime_t dt = {0};
            if (!ds1302_read_datetime(&dt))
                puts("[ALARM-PENDING] TEST refused: RTC unavailable");
            else
                alarm_pending_add(slot - 1u, dt.hour, dt.minute, now, true);
        }
    } else if (strncmp(line, "alarm-config ", 13) == 0) {
        alarm_config(line + 13);
    } else if (strncmp(line, "alarm-enable ", 13) == 0) {
        alarm_enable(line + 13);
    } else if (strcmp(line, "alarm-off") == 0) {
        if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open) {
            puts("[ALARM] close editor/player before changing alarm 1");
        } else {
            alarm_clear_waiting(0);
            alarms[0].enabled = false;
            settings_mark_changed();
            puts("[ALARM] 1 enabled=OFF");
        }
    } else if (strcmp(line, "alarm-audio") == 0) {
#ifdef CLOCK_ENABLE_A2DP
        printf("[ALARM-AUDIO] preferred=%s BT=%s running=%s (Flash autosave)\n",
               alarm_audio_jbl_enabled ? "BT" : "LOCAL",
               clock_a2dp_stream_active() ? "ACTIVE" : "INACTIVE",
               bt_alarm_running ? "YES" : "NO");
#else
        puts("[ALARM-AUDIO] LOCAL (Bluetooth disabled)");
#endif
    } else if ((strcmp(line, "alarm-audio bt") == 0 || strcmp(line, "alarm-audio jbl") == 0)) {
#ifdef CLOCK_ENABLE_A2DP
        alarm_audio_jbl_enabled = true;
        settings_mark_changed();
        puts("[ALARM-AUDIO] preferred=BT, LOCAL fallback; RAM-only");
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
                active_alarm_due_ms = now;
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
        // Timer is tactical: it can preempt and defer a ringing alarm.
        if (alarm_ui_state != ALARM_UI_CLOSED || music_ui_open) {
            puts("[TIMER] start blocked: close editor/player first");
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
    static bool discard_line;
    while ((ch = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (ch == '\r' || ch == '\n') {
            if (command_length && !discard_line) {
                command_line[command_length] = 0;
                handle_command(command_line);
            }
            memset(command_line,0,sizeof(command_line));
            command_length=0;
            discard_line=false;
        } else if (discard_line) {
            continue;
        } else if (ch == 8 || ch == 127) {
            if (command_length) --command_length;
        } else if (ch >= 32 && ch < 127) {
            if (command_length < sizeof(command_line) - 1) {
                command_line[command_length++] = (char)ch;
            } else {
                command_length = 0;
                memset(command_line,0,sizeof(command_line));
                discard_line=true;
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
        if (!timer.running && alarm_active_index<0) {
            unsigned step;
            uint32_t duration=timer_quick_adjust(&timer_acceleration,
                timer.duration_seconds,event->direction,now,&step);
            countdown_set_duration(&timer,duration);
            timer_screen = true;
            finished_screen_until_ms = 0;
            printf("[TIMER] selected=%lus step=%umin\n", (unsigned long)timer.duration_seconds,step);
        }
    } else {
        printf("[INPUT] button %s %s\n",
               controls_button_name(event->button),
               event->type == CONTROL_BUTTON_PRESS ? "PRESS" : "RELEASE");
        if (event->type==CONTROL_BUTTON_PRESS)
            timer_acceleration=(timer_acceleration_t){0};
        // Press/release separation prevents a long ALARM hold from
        // briefly opening the short-press alarm editor.
        if (event->button == BUTTON_ALARM) {
            if (event->type == CONTROL_BUTTON_PRESS) {
                alarm_button_pressed = true;
                alarm_button_pressed_at_ms = now;
                alarm_button_consumed = alarm_active_index >= 0;
                if (alarm_active_index >= 0) alarm_stop("BUTTON");
                else if(menu_state==MENU_CLOSED && alarm_ui_state==ALARM_UI_CLOSED &&
                        !music_ui_open && alarm_snooze_next(now,NULL)>=0) {
                    alarm_snooze_cancel_all();
                    alarm_button_consumed=true;
                }
            } else if (event->type == CONTROL_BUTTON_RELEASE &&
                       alarm_button_pressed) {
                alarm_button_pressed = false;
                if (!alarm_button_consumed && alarm_active_index < 0) {
                    if (music_ui_open) {
                        if(now-alarm_button_pressed_at_ms>=ALARM_LONG_PRESS_MS)
                            music_ui_close("ALARM_HOLD");
                        else music_ui_cycle_mode();
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
        // GP28 snoozes only a ringing alarm; ALARM still stops it.
        if (alarm_active_index >= 0) {
            if(event->type==CONTROL_BUTTON_PRESS && event->button==BUTTON_TIMER_MODE)
                alarm_snooze_start(now);
            return;
        }
        // Sound toggle is available before the player/editor consume buttons.
        if (event->button==BUTTON_VOLUME && event->type==CONTROL_BUTTON_PRESS) {
            toggle_sound();
            return;
        }
        if (music_ui_open) {
            if (event->type == CONTROL_BUTTON_PRESS) {
                if (event->button==BUTTON_TIMER) music_ui_toggle();
                else if(event->button==BUTTON_TIMER_MODE) {
                    unsigned minutes=music_sleep_cycle(&music_sleep,now);
                    printf("[MUSIC-UI] sleep=%umin (0=OFF; wall duration includes pause)\n",minutes);
                }
                else if(event->button==BUTTON_SETTINGS) music_ui_rotate(1);
                else if(event->button==BUTTON_MINUS) {
                    music_minus_pressed=true;
                    music_minus_pressed_at_ms=now;
                }
                else if(event->button==BUTTON_MODE) {
                    mode_press_active=true;
                    mode_pressed_at_ms=now;
                }
            } else if(event->type==CONTROL_BUTTON_RELEASE &&
                      event->button==BUTTON_MINUS && music_minus_pressed) {
                music_minus_pressed=false;
                if(now-music_minus_pressed_at_ms>=900u) music_ui_close("MINUS_HOLD");
                else music_ui_rotate(-1);
            } else if(event->type==CONTROL_BUTTON_RELEASE &&
                      event->button==BUTTON_MODE && mode_press_active) {
                mode_press_active=false;
                if(now-mode_pressed_at_ms>=900u) music_ui_close("MODE_HOLD");
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
        if (event->type==CONTROL_BUTTON_PRESS && event->button==BUTTON_TIMER_MODE) {
            if (menu_state==MENU_TIMER_EDITOR) {
                menu_close();
                puts("[TIMER-EDIT] cancelled; duration unchanged");
            } else if (menu_state==MENU_CLOSED && !timer.running) {
                timer_editor_open(&timer_editor,timer.duration_seconds);
                timer_acceleration=(timer_acceleration_t){0};
                menu_state=MENU_TIMER_EDITOR;
                menu_last_input_ms=now;
                finished_screen_until_ms=0;
                puts("[TIMER-EDIT] HOURS; TIMER/SETTINGS=next MINUS=back GP28=cancel");
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
        if (event->button==BUTTON_MINUS && menu_state==MENU_CLOSED) {
            if (event->type==CONTROL_BUTTON_PRESS) {
                info_press_active=true;
                info_pressed_at_ms=now;
            } else if (event->type==CONTROL_BUTTON_RELEASE && info_press_active) {
                info_press_active=false;
                if (!timer.running) announce_clock_info(now-info_pressed_at_ms>=900u);
            }
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
        if (event->button == BUTTON_TIMER && event->type == CONTROL_BUTTON_PRESS) {
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
    display_light_init();
    stdio_init_all();
    sleep_ms(3000); // Give VS Code Serial Monitor time to attach.
    printf("\n[BOOT] Speaking Clock %s build=%s (Pico 2 W)\n",CLOCK_FIRMWARE_VERSION,CLOCK_BUILD_ID);
#ifdef CLOCK_ENABLE_MP3
    // Initialize SD before starting Bluetooth, avoiding a long missing-card
    // command sequence while an A2DP media stream is already running.
    // Failures are nonfatal; playback falls back to local DFPlayer.
    if (!sd_spi_ensure_fat32())
        puts("[SD-AUTO] Boot will continue with LOCAL audio fallback");
#endif
#if defined(CLOCK_ENABLE_A2DP) || defined(CLOCK_ENABLE_WIFI)
    radio_ready=cyw43_arch_init()==PICO_OK;
    puts(radio_ready?"[RADIO] Initialized":"[RADIO] Unavailable; clock continues offline");
#ifdef CLOCK_ENABLE_A2DP
    clock_a2dp_boot_hold(true);
    if (radio_ready) printf("[BT] btstack_main=%d\n",btstack_main(0,NULL));
#endif
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
    birthday_defaults(birthdays);
    alarm_scheduler_init(&alarm_scheduler);
    load_settings_at_boot();
#ifdef CLOCK_ENABLE_WIFI
    network_time_init(radio_ready);
    network_time_configure(saved_wifi_ssid,saved_wifi_password);
#endif
#ifdef CLOCK_ENABLE_PHONE_WEB
    phone_web_init(radio_ready);
#endif
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
#ifdef CLOCK_ENABLE_PHONE_WEB
    bool rtc_available = false;
#endif
    uint64_t next_rtc_ms = 0;
    uint32_t previous_remaining = UINT32_MAX;
    uint64_t startup_until_ms=to_ms_since_boot(get_absolute_time())+1600u;
    puts("[STARTUP] Nonblocking LCD version splash; no startup audio queued");
    while (true) {
#ifdef CLOCK_ENABLE_A2DP
        // Persist a newly selected sink only after it has REALLY established
        // an A2DP stream. Observe the old link going down first so the old
        // ACTIVE state cannot accidentally commit a failed selection.
        if (!clock_a2dp_stream_active()) bt_volume_remote_synced = false;
        if (bt_pending_target_valid) {
            if (!clock_a2dp_stream_active()) bt_pending_seen_disconnected = true;
            if (bt_pending_seen_disconnected &&
                clock_a2dp_stream_matches(bt_pending_target_addr)) {
                int known = bt_known_find(bt_pending_target_addr);
                if (known < 0) {
                    if (bt_known_count == CLOCK_BT_KNOWN_MAX) {
                        memmove(&bt_known[0], &bt_known[1],
                                (CLOCK_BT_KNOWN_MAX - 1u) * sizeof(bt_known[0]));
                        --bt_known_count;
                    }
                    known = (int)bt_known_count++;
                }
                memcpy(bt_known[known].address, bt_pending_target_addr, 6);
                snprintf(bt_known[known].name, sizeof(bt_known[known].name), "%s",
                         bt_pending_target_name);
                memcpy(saved_bt_target_addr, bt_pending_target_addr,
                       sizeof(saved_bt_target_addr));
                bt_pending_target_valid = false;
                settings_mark_changed();
                printf("[BT-SELECT] stream ACTIVE, sink accepted for Flash autosave: %s\n",
                       clock_a2dp_target());
            }
        }
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
#ifdef CLOCK_ENABLE_PHONE_WEB
        if (phone_web_take_changes()) settings_mark_changed();
        service_phone_command(now);
#endif
        countdown_service(&timer, now);
        // countdown_service clears timer.running at zero; the finish
        // event and its audio must take priority over flash writes.
        if (countdown_consume_finished(&timer)) {
            puts("[TIMER] FINISHED");
            timer_audio_finished();
            timer_screen = false;
            finished_screen_until_ms = now + 3000u;
        }
        uint32_t menu_timeout_ms = SETTINGS_MENU_TIMEOUT_MS;
#ifdef CLOCK_ENABLE_PHONE_WEB
        if (menu_state == MENU_PHONE_WEB && phone_menu_page == 0u && !phone_web_direct()) {
            network_home_status_t connection = network_time_home_status(now);
            if (connection.state == NETWORK_HOME_WAIT_IDLE || connection.state == NETWORK_HOME_CONNECTING ||
                connection.state == NETWORK_HOME_ADDRESS || connection.state == NETWORK_HOME_RETRY)
                menu_last_input_ms = now;
        }
        if (menu_state == MENU_PHONE_WEB && phone_menu_page == PHONE_PASSWORD_PAGE)
            menu_timeout_ms = PHONE_PASSWORD_TIMEOUT_MS;
        else if (menu_state == MENU_PHONE_PAIR_CODE)
            menu_timeout_ms = PHONE_CODE_MS;
#endif
        if (menu_state != MENU_CLOSED &&
#ifdef CLOCK_ENABLE_A2DP
            // Remote-name requests follow the 10s Classic inquiry and can
            // take several more seconds. Never close the scan screen midway.
            !(menu_state == MENU_BT_SCANNING && clock_a2dp_scan_active()) &&
#endif
            now - menu_last_input_ms >= menu_timeout_ms) {
            printf("[MENU] Timeout (%lus); unsaved edit discarded\n",
                   (unsigned long)(menu_timeout_ms / 1000u));
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
             ) || (music_ui_open && !music_ui_bluetooth) ||
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
#ifdef CLOCK_ENABLE_WIFI
        static uint64_t network_next_ms;
        if (now >= network_next_ms) {
            network_next_ms=now+250;
            bool network_idle=!timer.running && alarm_active_index<0 &&
                alarm_ui_state==ALARM_UI_CLOSED &&
                !music_ui_open && audio_queue_idle(&audio_queue) && !dfplayer_busy() &&
                !mp3_play_active() && !mp3_probe_active() && !sd_fat32_stream_active();
#ifdef CLOCK_ENABLE_A2DP
            network_idle=network_idle && !bt_speech_running && !bt_timer_running &&
                 !bt_alarm_running && !routed_test_pending && !clock_a2dp_scan_active();
#endif
            bool idle = network_idle && menu_state == MENU_CLOSED;
            bool join_allowed = idle;
#ifdef CLOCK_ENABLE_PHONE_WEB
            join_allowed = network_idle && (menu_state == MENU_CLOSED || menu_state == MENU_PHONE_WEB ||
                menu_state == MENU_PHONE_PAIR_CODE || menu_state == MENU_PHONE_CONFIRM);
#endif
            ds1302_datetime_t local, old;
            if (network_time_service(now,idle,join_allowed,&local)) {
                uint64_t previous=0, corrected=0;
                bool have_old=ds1302_read_datetime(&old) && clock_datetime_to_utc(&old,&previous);
                clock_datetime_to_utc(&local,&corrected);
                uint64_t difference=have_old ? (previous>corrected?previous-corrected:corrected-previous) : UINT64_MAX;
                bool ok=difference<=1 || ds1302_write_datetime(&local);
                if (ok && difference>5) {
                    clock_adjustment_t adjustment=clock_adjustment_plan(
                        have_old,previous,corrected,network_time_is_dst_change());
                    rtc_replay_guard_until=adjustment.suppress_before;
                    auto_boot_seeded=adjustment.allow_boundary;
                    auto_seen_key_valid=false;
                    alarm_boot_seeded=adjustment.allow_boundary;
                    alarm_scheduler_init(&alarm_scheduler);
                }
                network_time_applied(ok);
                next_rtc_ms=0;
            }
        }
#endif
        if (now >= next_rtc_ms) {
            next_rtc_ms = now + 1000u;
            ds1302_datetime_t dt = {0};
            bool read_ok = ds1302_read_datetime(&dt);
#ifdef CLOCK_ENABLE_PHONE_WEB
            rtc_available = read_ok;
#endif
            if (read_ok) {
                display_light_hour = dt.hour;
                snprintf(rtc_top, sizeof(rtc_top), "%02u:%02u:%02u",
                         (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
                snprintf(rtc_bottom, sizeof(rtc_bottom), "%02u.%02u.%04u",
                         (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year);
                printf("[RTC] %s %s\n", rtc_top, rtc_bottom);
                alarm_service_rtc(&dt, now);
                auto_service_rtc(&dt);
                birthday_observe(&dt,now);
            } else {
                snprintf(rtc_top, sizeof(rtc_top), "RTC unavailable");
                snprintf(rtc_bottom, sizeof(rtc_bottom), "Check GP2/5/4");
                birthday_rtc_valid=false;
                display_light_hour = 24u;
                // RTC is polled once a second, but missing RTC doesn't stop controls.
                puts("[RTC] Invalid/unavailable (run rtc-debug for raw bytes and error reasons)");
            }
        }
        display_light_service();

        alarm_snooze_service(now);
        alarm_pending_service(now);
        birthday_service(now);
#ifdef CLOCK_ENABLE_PHONE_WEB
        static uint64_t next_phone_snapshot_ms;
        if (now >= next_phone_snapshot_ms) {
            next_phone_snapshot_ms = now + 250u;
            phone_web_snapshot_t snapshot = {
                .timer_seconds = timer.running ? countdown_remaining(&timer, now) : timer.duration_seconds,
                .track = music_ui_open ? music_ui_track : 0,
                .track_count = music_ui_open ? music_track_count : 0,
                .player_mode = music_ui_open ? music_playlist_mode_name(music_playlist.mode) : "",
                .preferences = {
                    .volume_local = volume, .volume_bt = bt_volume, .language = (uint8_t)language,
                    .audio = {menu_audio_selected_jbl(0), menu_audio_selected_jbl(1),
                              menu_audio_selected_jbl(2), menu_audio_selected_jbl(3)},
                    .light = display_light,
                    .editing = menu_state != MENU_CLOSED || alarm_ui_state != ALARM_UI_CLOSED,
                    .idle = phone_settings_idle(),
#ifdef CLOCK_ENABLE_A2DP
                    .bt_supported = true,
#endif
                    .pending = autosave_pending, .retry = autosave_attempted,
                },
                .timer_running = timer.running,
                .bluetooth = volume_for_bluetooth(),
                .player = !music_ui_open ? "Closed" : !sound_enabled ? "Muted" :
                          music_ui_pending ? "Waiting" : music_ui_paused ? "Paused" :
                          music_ui_playing ? "Playing" : "Stopped",
            };
            if (rtc_available) memcpy(snapshot.time, rtc_top, sizeof(snapshot.time));
            memcpy(snapshot.alarms, alarms, sizeof(snapshot.alarms));
            memcpy(snapshot.birthdays, birthdays, sizeof(snapshot.birthdays));
            phone_web_publish(&snapshot);
        }
#endif
#ifdef CLOCK_ENABLE_A2DP
        if (menu_state == MENU_BT_SCANNING && !clock_a2dp_scan_active()) {
            menu_state = MENU_BT_LIST;
            menu_bt_index = bt_menu_count() > 2u ? 1u : 0u;
            menu_last_input_ms = now;
            printf("[MENU-BT] scan complete, devices=%u\n",
                   clock_a2dp_scan_count());
        }
#endif

        char top[17], bottom[17];
        int cursor_column=-1; // Visible only on the editor branch actually rendered.
        if (music_ui_open) {
            snprintf(top,sizeof(top),"MUSIC %s %s",
                music_playlist_mode_name(music_playlist.mode),music_ui_bluetooth?"BT":"LOCAL");
            snprintf(bottom,sizeof(bottom),"%03u/%03u %s OFF",
                (unsigned)music_ui_track,(unsigned)music_track_count,
                !sound_enabled?"MUTE":music_ui_pending?"WAIT":music_ui_paused?"PAUS":music_ui_playing?"PLAY":"STOP");
            if (music_sleep.deadline_ms) {
                snprintf(bottom,sizeof(bottom),"%03u/%03u %s %02um",
                    (unsigned)music_ui_track,(unsigned)music_track_count,
                    !sound_enabled?"MUTE":music_ui_pending?"WAIT":music_ui_paused?"PAUS":music_ui_playing?"PLAY":"STOP",
                    music_sleep_minutes(&music_sleep,now));
            }
        } else if(menu_state==MENU_BIRTHDAY_LIST) {
            const birthday_t *b=&birthdays[birthday_index];
            snprintf(top,sizeof(top),"BIRTHDAY %u/5 %s",(unsigned)birthday_index+1,b->enabled?"ON":"OFF");
            snprintf(bottom,sizeof(bottom),"%02u.%02u %02u:%02u",
                (unsigned)b->day,(unsigned)b->month,(unsigned)b->hour,(unsigned)b->minute);
        } else if(menu_state>=MENU_BIRTHDAY_ENABLED && menu_state<=MENU_BIRTHDAY_MINUTE) {
            static const char *fields[]={"ON/OFF","MONTH","DAY","HOUR","MINUTE"};
            snprintf(top,sizeof(top),"BDAY %u %s",(unsigned)birthday_index+1,fields[menu_state-MENU_BIRTHDAY_ENABLED]);
            if(menu_state==MENU_BIRTHDAY_ENABLED) {
                snprintf(bottom,sizeof(bottom),"%s PRESS=NEXT",birthday_draft.enabled?"ON":"OFF");
                cursor_column=0;
            } else {
                snprintf(bottom,sizeof(bottom),"%02u.%02u %02u:%02u",
                    (unsigned)birthday_draft.day,(unsigned)birthday_draft.month,
                    (unsigned)birthday_draft.hour,(unsigned)birthday_draft.minute);
                cursor_column=menu_state==MENU_BIRTHDAY_DAY?0:menu_state==MENU_BIRTHDAY_MONTH?3:menu_state==MENU_BIRTHDAY_HOUR?6:9;
            }
        } else if (alarm_ui_state == ALARM_UI_LIST) {
            const alarm_entry_t *a = &alarms[alarm_ui_index];
            snprintf(top, sizeof(top), "A%u/5 %s %s",
                     (unsigned)(alarm_ui_index + 1u), a->enabled ? "ON" : "OFF",
                     alarm_days_name(a->weekdays));
            snprintf(bottom, sizeof(bottom), "%02u:%02u %s",
                     (unsigned)a->hour, (unsigned)a->minute,
                     a->sound == ALARM_SOUND_MUSIC ? "MUSIC" : "SIGNAL");
        } else if(alarm_ui_state==ALARM_UI_DAYS) {
            cursor_column=0;
            snprintf(top,sizeof(top),"ALARM %u DAYS",(unsigned)alarm_ui_index+1);
            snprintf(bottom,sizeof(bottom),"%s T=%s",alarm_ui_days_choice==3?"Custom":alarm_days_name(alarm_ui_draft.weekdays),
                alarm_ui_days_choice==3?"EDIT":"NEXT");
        } else if(alarm_ui_state==ALARM_UI_DAY) {
            cursor_column=4; // ON/OFF follows the three-letter day name.
            static const char *days[]={"Mon","Tue","Wed","Thu","Fri","Sat","Sun"};
            if(alarm_ui_days_invalid) snprintf(top,sizeof(top),"SELECT >=1 DAY");
            else snprintf(top,sizeof(top),"ALARM %u DAYS",(unsigned)alarm_ui_index+1);
            snprintf(bottom,sizeof(bottom),"%s %s T=NEXT",days[alarm_ui_day_index],
                alarm_ui_draft.weekdays&(1u<<alarm_ui_day_index)?"ON":"OFF");
        } else if (alarm_ui_state != ALARM_UI_CLOSED) {
            cursor_column=alarm_ui_state==ALARM_UI_MINUTE?3:0;
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
        } else if (menu_state == MENU_TIMER_EDITOR) {
            cursor_column=(int)timer_editor.field*3;
            snprintf(top,sizeof(top),"TIMER SET %s",
                timer_editor.field==TIMER_FIELD_HOUR?"HOUR":
                timer_editor.field==TIMER_FIELD_MINUTE?"MIN":"SEC");
            snprintf(bottom,sizeof(bottom),"%02u:%02u:%02u",
                (unsigned)timer_editor.hour,(unsigned)timer_editor.minute,
                (unsigned)timer_editor.second);
        } else if (menu_state == MENU_ABOUT) {
            if (menu_about_version) {
                snprintf(top, sizeof(top), "v%.15s", CLOCK_DISPLAY_VERSION);
                snprintf(bottom, sizeof(bottom), "by FlintUA");
            } else {
                snprintf(top, sizeof(top), "Speaking Clock");
                snprintf(bottom, sizeof(bottom), "Pico 2 W");
            }
#ifdef CLOCK_ENABLE_PHONE_WEB
        } else if (menu_state == MENU_PHONE_WEB) {
            bool direct = phone_web_direct();
            const char *ip = phone_web_address();
            if (phone_menu_page == 0u) {
                snprintf(top, sizeof(top), "%s Wi-Fi", direct ? "Direct" : "Home");
                network_home_status_t connection = network_time_home_status(now);
                unsigned long seconds = connection.seconds > 9999u ? 9999u : connection.seconds;
                if (direct) {
                    snprintf(bottom, sizeof(bottom), "%s", *ip ? ip : "Starting...");
                } else switch (connection.state) {
                    case NETWORK_HOME_READY:
                        snprintf(top, sizeof(top), "Home Wi-Fi ready");
                        snprintf(bottom, sizeof(bottom), "%s", *ip ? ip : "Getting address");
                        break;
                    case NETWORK_HOME_CONNECTING:
                        snprintf(bottom, sizeof(bottom), "Connecting %lus", seconds);
                        break;
                    case NETWORK_HOME_ADDRESS:
                        snprintf(bottom, sizeof(bottom), "Getting IP %lus", seconds);
                        break;
                    case NETWORK_HOME_RETRY:
                        snprintf(bottom, sizeof(bottom), "Retry in %lus", seconds);
                        break;
                    case NETWORK_HOME_WAIT_IDLE:
                        snprintf(bottom, sizeof(bottom), "Wait until idle");
                        break;
                    case NETWORK_HOME_UNCONFIGURED:
                        snprintf(bottom, sizeof(bottom), "Set home Wi-Fi");
                        break;
                    default:
                        snprintf(bottom, sizeof(bottom), "Radio offline");
                        break;
                }
            } else if (phone_menu_page == 1u) {
                snprintf(top, sizeof(top), "%s", direct ? "Return to home?" : "Start direct?");
                snprintf(bottom, sizeof(bottom), "TIMER=OK");
            } else if (phone_menu_page == PHONE_ACCESS_PAGE) {
                snprintf(top, sizeof(top), "Pair phone?");
                snprintf(bottom, sizeof(bottom), "%s", *ip ? "TIMER=OK" : "No address");
            } else if (phone_menu_page == PHONE_TRUST_PAGE) {
                snprintf(top, sizeof(top), "Home trust: %s", phone_web_home_trusted() ? "ON" : "OFF");
                snprintf(bottom, sizeof(bottom), "%s", saved_wifi_ssid[0] ? "TIMER=CHANGE" : "Set home Wi-Fi");
            } else if (phone_menu_page == PHONE_FORGET_PAGE || phone_menu_page == PHONE_ROTATE_PAGE) {
                snprintf(top, sizeof(top), "%s", phone_menu_page == PHONE_FORGET_PAGE ?
                    "Forget browsers?" : "New Wi-Fi key?");
                snprintf(bottom, sizeof(bottom), "TIMER=OK");
            } else {
                snprintf(top, sizeof(top), "%s", PHONE_WEB_SSID);
                snprintf(bottom, sizeof(bottom), "%s", phone_web_password());
            }
        } else if (menu_state == MENU_PHONE_CONFIRM) {
            snprintf(top, sizeof(top), "%s", phone_menu_page == PHONE_FORGET_PAGE ?
                "Forget all?" : "Replace Wi-Fi?");
            snprintf(bottom, sizeof(bottom), "TIMER=YES -=BACK");
        } else if (menu_state == MENU_PHONE_PAIR_CODE) {
            uint32_t code;
            if (phone_web_pair_code(&code)) {
                snprintf(top, sizeof(top), "PHONE CODE");
                snprintf(bottom, sizeof(bottom), "%06lu", (unsigned long)code);
            } else {
                snprintf(top, sizeof(top), "%s", phone_web_pair_succeeded() ? "Phone paired" : "Code unavailable");
                snprintf(bottom, sizeof(bottom), "MINUS=BACK");
            }
#endif
        } else if (menu_state == MENU_DISPLAY_LIGHT_MODE) {
            cursor_column = 0;
            snprintf(top, sizeof(top), "DISPLAY LIGHT");
            snprintf(bottom, sizeof(bottom), "%s", display_light_mode_name(display_light_draft.mode));
        } else if (menu_state == MENU_DISPLAY_LIGHT_DAY || menu_state == MENU_DISPLAY_LIGHT_NIGHT) {
            bool night = menu_state == MENU_DISPLAY_LIGHT_NIGHT;
            cursor_column = 0;
            snprintf(top, sizeof(top), "%s BRIGHTNESS", night ? "NIGHT" : "DAY");
            snprintf(bottom, sizeof(bottom), "%u%%",
                     (unsigned)(night ? display_light_draft.night_percent : display_light_draft.day_percent));
        } else if (menu_state == MENU_BT_SCANNING) {
#ifdef CLOCK_ENABLE_A2DP
            snprintf(top, sizeof(top), "BT SCAN+NAMES");
            snprintf(bottom, sizeof(bottom), "%u found MINUS",
                     clock_a2dp_scan_count());
#else
            snprintf(top, sizeof(top), "BLUETOOTH OFF");
            snprintf(bottom, sizeof(bottom), "MINUS=BACK");
#endif
        } else if (menu_state == MENU_BT_CONFIRM) {
#ifdef CLOCK_ENABLE_A2DP
            clock_bt_device_t d;
            snprintf(top, sizeof(top), "BT CONNECT?");
            if (menu_bt_index && bt_menu_device(menu_bt_index, &d))
                snprintf(bottom, sizeof(bottom), "%.16s", bt_lcd_name(menu_bt_index, &d));
            else snprintf(bottom, sizeof(bottom), "Device missing");
#else
            snprintf(top, sizeof(top), "BLUETOOTH OFF");
            snprintf(bottom, sizeof(bottom), "MINUS=BACK");
#endif
        } else if (menu_state == MENU_BT_LIST) {
#ifdef CLOCK_ENABLE_A2DP
            unsigned count = bt_menu_count();
            snprintf(top, sizeof(top), "BT %u/%u %s",
                     (unsigned)(menu_bt_index + 1u), count,
                     clock_a2dp_stream_active() ? "ON" : clock_a2dp_autoconnect_enabled()?"RETRY":"OFF");
            if (menu_bt_index == 0u)
                snprintf(bottom, sizeof(bottom), "Scan devices");
            else if (menu_bt_index==count-1u)
                snprintf(bottom,sizeof(bottom),"Disconnect");
            else {
                clock_bt_device_t d;
                if (bt_menu_device(menu_bt_index, &d))
                    snprintf(bottom, sizeof(bottom), "%.16s", bt_lcd_name(menu_bt_index, &d));
                else snprintf(bottom, sizeof(bottom), "No device");
            }
#else
            snprintf(top, sizeof(top), "BLUETOOTH OFF");
            snprintf(bottom, sizeof(bottom), "MINUS=BACK");
#endif
        } else if (menu_state == MENU_AUDIO_LIST) {
            snprintf(top, sizeof(top), "AUDIO %u/%u %s",
                     (unsigned)(menu_audio_index + 1u),
                     (unsigned)SETTINGS_AUDIO_ITEMS,
                     menu_audio_selected_jbl(menu_audio_index) ? "BT" : "LOCAL");
            snprintf(bottom, sizeof(bottom), "%s",
                     menu_audio_name(menu_audio_index));
        } else if (menu_state == MENU_EDIT_AUDIO_OUTPUT) {
            cursor_column=0;
            snprintf(top, sizeof(top), "OUT %s",
                     menu_audio_name(menu_audio_index));
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_audio_jbl_choice ? "BT" : "LOCAL");
        } else if (menu_state == MENU_EDIT_LANGUAGE) {
            cursor_column=0;
            snprintf(top, sizeof(top), "LANGUAGE SETTING");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_language_choice == SPEECH_RU ? "RU" : "DE");
        } else if (menu_is_time_edit()) {
            cursor_column=menu_state==MENU_EDIT_TIME_H?0:menu_state==MENU_EDIT_TIME_M?3:6;
            snprintf(top, sizeof(top), "TIME SET %s",
                     menu_state == MENU_EDIT_TIME_H ? "HOUR" :
                     menu_state == MENU_EDIT_TIME_M ? "MIN" : "SEC");
            snprintf(bottom, sizeof(bottom), "%02u:%02u:%02u",
                     (unsigned)menu_rtc_choice.hour, (unsigned)menu_rtc_choice.minute,
                     (unsigned)menu_rtc_choice.second);
        } else if (menu_is_date_edit()) {
            cursor_column=menu_state==MENU_EDIT_DATE_D?0:menu_state==MENU_EDIT_DATE_M?3:6;
            snprintf(top, sizeof(top), "DATE SET %s",
                     menu_state == MENU_EDIT_DATE_D ? "DAY" :
                     menu_state == MENU_EDIT_DATE_M ? "MONTH" : "YEAR");
            snprintf(bottom, sizeof(bottom), "%02u.%02u.%04u",
                     (unsigned)menu_rtc_choice.day, (unsigned)menu_rtc_choice.month,
                     (unsigned)menu_rtc_choice.year);
        } else if (menu_state == MENU_EDIT_QUIET_ENABLED) {
            cursor_column=0;
            snprintf(top, sizeof(top), "QUIET MODE");
            snprintf(bottom, sizeof(bottom), "%s PRESS=NEXT",
                     menu_quiet_enabled_choice ? "ON" : "OFF");
        } else if (menu_state == MENU_EDIT_QUIET_START) {
            cursor_column=0;
            snprintf(top, sizeof(top), "QUIET START");
            snprintf(bottom, sizeof(bottom), "%02u:00 PRESS=NEXT",
                     (unsigned)menu_quiet_start_choice);
        } else if (menu_state == MENU_EDIT_QUIET_END) {
            cursor_column=0;
            snprintf(top, sizeof(top), "QUIET END");
            snprintf(bottom, sizeof(bottom), "%02u:00 PRESS=SAVE",
                     (unsigned)menu_quiet_end_choice);
        } else if (menu_state == MENU_EDIT_HALF_HOUR) {
            cursor_column=0;
            snprintf(top, sizeof(top), "HALF-HOUR BEEP");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_half_hour_choice ? "ON" : "OFF");
        } else if (menu_state == MENU_EDIT_EXACT) {
            cursor_column=0;
            snprintf(top, sizeof(top), "EXACT HOUR WORD");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     menu_exact_choice ? "ON" : "OFF");
        } else if (alarm_active_index >= 0) {
            snprintf(top, sizeof(top), "ALARM %d RINGING", alarm_active_index + 1);
            snprintf(bottom, sizeof(bottom), "A=STOP TM=SNOOZE");
        } else if (info_overlay_until_ms && now < info_overlay_until_ms && !timer.running) {
            snprintf(top,sizeof(top),"%s",info_overlay_top);
            snprintf(bottom,sizeof(bottom),"%s",info_overlay_bottom);
        } else if (startup_until_ms && now<startup_until_ms && !timer.running && !timer_screen) {
            snprintf(top,sizeof(top),"Speaking Clock");
            snprintf(bottom,sizeof(bottom),"v%.15s",CLOCK_DISPLAY_VERSION);
        } else if (mode_overlay_until_ms && now < mode_overlay_until_ms) {
            snprintf(top, sizeof(top), "CLOCK MODE");
            snprintf(bottom, sizeof(bottom), "%s", clock_mode ? "CHIME" : "VOICE");
        } else if (speech_overlay_until_ms && now < speech_overlay_until_ms) {
            snprintf(top, sizeof(top), "SPEAK TIME %s", speech_language_name(language));
            snprintf(bottom, sizeof(bottom), "%s", speech_overlay_time);
        } else if (volume_overlay_until_ms && now < volume_overlay_until_ms) {
            snprintf(top, sizeof(top), "%s VOL %s",
                     volume_overlay_bluetooth ? "BT" : "LOCAL",
                     sound_enabled ? "ON" : "OFF");
            snprintf(bottom, sizeof(bottom), "%u / 30",
                     (unsigned)(volume_overlay_bluetooth ? bt_volume : volume));
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
        } else if(alarm_snooze_next(now,NULL)>=0) {
            uint64_t remaining;
            int slot=alarm_snooze_next(now,&remaining);
            snprintf(top,sizeof(top),"ALARM %u SNOOZE",(unsigned)slot+1);
            snprintf(bottom,sizeof(bottom),"%02u:%02u A=CANCEL",
                (unsigned)(remaining/60),(unsigned)(remaining%60));
        } else {
            snprintf(top, sizeof(top), "%s", rtc_top);
            snprintf(bottom, sizeof(bottom), "%s", rtc_bottom);
        }

        if (lcd_ok) {
            bool top_changed=strcmp(top,last_top)!=0;
            bool bottom_changed=strcmp(bottom,last_bottom)!=0;
            // Hide during repaint, then restore focus because writing text moves DDRAM.
            if (top_changed || bottom_changed)
                lcd1602_set_cursor(0,0,LCD1602_CURSOR_OFF);
            if (top_changed) {
                lcd1602_write_line(0,top);
                snprintf(last_top,sizeof(last_top),"%s",top);
            }
            if (bottom_changed) {
                lcd1602_write_line(1,bottom);
                snprintf(last_bottom,sizeof(last_bottom),"%s",bottom);
            }
            if (alarm_active_index>=0) cursor_column=-1;
            lcd1602_set_cursor(1,cursor_column<0?0u:(unsigned)cursor_column,
                cursor_column<0?LCD1602_CURSOR_OFF:LCD1602_CURSOR_BLINK);
        }
        sleep_ms(1);
    }
}
