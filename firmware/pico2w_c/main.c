// Speaking-clock C firmware with protected flash settings (no Bluetooth yet).
// DS1302 RTC is read-only.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ds1302.h"
#include "lcd1602_i2c.h"
#include "countdown.h"
#include "controls.h"
#include "dfplayer.h"
#include "queue.h"
#include "speech.h"
#include "settings_store.h"

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
static char command_line[48];
static size_t command_length;
static bool timer_screen;
static audio_queue_t audio_queue;
static uint8_t volume = 10; // Default; overridden by saved flash settings.
static bool sound_enabled = true;
static speech_language_t language = SPEECH_RU;
// UI selection updates RAM, and schedules a delayed flash save.
static bool language_menu_open;
static speech_language_t language_menu_choice;
static uint64_t language_menu_last_input_ms;
#define LANGUAGE_MENU_TIMEOUT_MS 20000u
static bool exact_hour_phrase;
static uint64_t mode_pressed_at_ms;
static bool mode_press_active;
static uint64_t speech_overlay_until_ms;
static char speech_overlay_time[17];
static uint64_t volume_overlay_until_ms;
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


static void print_timer_status(uint64_t now_ms) {
    uint32_t remaining = countdown_remaining(&timer, now_ms);
    printf("[TIMER] %s duration=%lus remaining=%lus\n",
           timer.running ? "RUNNING" : "STOPPED",
           (unsigned long)timer.duration_seconds, (unsigned long)remaining);
}

static void timer_audio_start(void) {
    if (!sound_enabled) return;
    audio_queue_clear(&audio_queue, true);
    speech_enqueue_timer_start(&audio_queue, language, timer.duration_seconds);
}

static void timer_audio_cancel(void) {
    audio_queue_clear(&audio_queue, true);
    if (sound_enabled) speech_enqueue_timer_cancel(&audio_queue, language);
}

static void timer_audio_finished(void) {
    audio_queue_clear(&audio_queue, true);
    if (sound_enabled) speech_enqueue_timer_finish(&audio_queue, language);
}

// Use the same hour/minute file mapping as MicroPython enqueue_clock_time.
// Never announce automatically: only on user command or MODE long press.
static void announce_time(uint8_t hour, uint8_t minute) {
    if (!sound_enabled) {
        puts("[SPEECH] skipped: sound OFF");
        return;
    }
    if (hour > 23u || minute > 59u) {
        puts("[SPEECH] invalid time");
        return;
    }
    audio_queue_clear(&audio_queue, true);
    if (!speech_enqueue_time(&audio_queue, language, hour, minute, exact_hour_phrase)) {
        puts("[SPEECH] failed to queue time");
        return;
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
        .exact_hour = exact_hour_phrase
    };
    return cfg;
}

static bool settings_equal(clock_settings_t a, clock_settings_t b) {
    // Compare fields explicitly, not struct padding.
    return a.language == b.language && a.volume == b.volume &&
           a.exact_hour == b.exact_hour;
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
    // Avoid interrupting live countdown and queued DFPlayer speech.
    if (timer.running || language_menu_open || !audio_queue_idle(&audio_queue)) return;

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
    if (!sound_enabled) audio_queue_clear(&audio_queue, true);
    printf("[AUDIO] sound=%s\n", sound_enabled ? "ON" : "OFF");
}

static void close_language_menu(bool commit) {
    if (!language_menu_open) return;
    if (commit) {
        speech_language_t previous = language;
        language = language_menu_choice;
        if (previous != language) settings_mark_changed();
        printf("[MENU] Language selected: %s (autosave if changed)\n",
               speech_language_name(language));
    } else {
        printf("[MENU] Language selection cancelled; remains %s\n",
               speech_language_name(language));
    }
    language_menu_open = false;
}

static void open_language_menu(uint64_t now_ms) {
    if (timer.running) {
        puts("[MENU] Settings unavailable during countdown");
        return;
    }
    language_menu_choice = language;
    language_menu_last_input_ms = now_ms;
    language_menu_open = true;
    // The dedicated menu takes precedence over any prior temporary overlays.
    printf("[MENU] Select language: %s (turn TIMER, press TIMER to confirm, SETTINGS to cancel)\n",
           speech_language_name(language_menu_choice));
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
        printf("[FLASH] loaded lang=%s volume=%u exact=%s\n",
               speech_language_name(language), (unsigned)volume,
               exact_hour_phrase ? "ON" : "OFF");
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

static void handle_command(char *line) {
    while (isspace((unsigned char)*line)) ++line;
    size_t len = strlen(line);
    while (len && isspace((unsigned char)line[len - 1])) line[--len] = 0;
    uint64_t now = to_ms_since_boot(get_absolute_time());
    if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
        puts("[CMD] t <seconds>, start, cancel, status, say [HH MM], lang ru|de, exact on|off, play F T, pause, audio, vol N, sound, menu, flash-info, flash-inspect, flash-tail, autosave-status, rtc-debug, rtc-reset-stats, save, help");
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
        save_settings_now();
    } else if (strcmp(line, "autosave-status") == 0) {
        print_autosave_status(now);
    } else if (strcmp(line, "menu") == 0) {
        if (language_menu_open) close_language_menu(false);
        else open_language_menu(now);
    } else if (strcmp(line, "start") == 0) {
        if (language_menu_open) close_language_menu(false);
        timer_start_now(now);
        print_timer_status(now);
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
        if (language_menu_open) {
            language_menu_choice = language;
            language_menu_last_input_ms = now;
        }
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
        audio_queue_clear(&audio_queue, true);
        puts("[AUDIO] Pause requested");
    } else if (strncmp(line, "play", 4) == 0 && isspace((unsigned char)line[4])) {
        unsigned int folder = 0, track = 0;
        char extra = 0;
        if (sscanf(line + 4, " %u %u %c", &folder, &track, &extra) != 2 ||
            folder < 1 || folder > 99 || track < 1 || track > 255) {
            puts("[AUDIO] Use play <folder 1..99> <track 1..255>");
        } else if (!sound_enabled) {
            puts("[AUDIO] Sound OFF; press volume button to enable");
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
        if (language_menu_open) {
            language_menu_choice = language_menu_choice == SPEECH_RU ? SPEECH_DE : SPEECH_RU;
            language_menu_last_input_ms = now;
            printf("[MENU] Language candidate: %s\n",
                   speech_language_name(language_menu_choice));
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
        if (event->button == BUTTON_SETTINGS &&
            event->type == CONTROL_BUTTON_PRESS) {
            if (language_menu_open) close_language_menu(false);
            else open_language_menu(now);
            return;
        }
        if (event->button == BUTTON_TIMER &&
            event->type == CONTROL_BUTTON_PRESS && language_menu_open) {
            language_menu_last_input_ms = now;
            close_language_menu(true);
            timer_screen = false;
            return;
        }
        if (event->button == BUTTON_MODE) {
            // Parity with MicroPython ST/MO long-press threshold: 900 ms.
            if (event->type == CONTROL_BUTTON_PRESS) {
                mode_press_active = true;
                mode_pressed_at_ms = now;
            } else if (event->type == CONTROL_BUTTON_RELEASE && mode_press_active) {
                mode_press_active = false;
                if (now - mode_pressed_at_ms >= 900u) {
                    announce_rtc_time();
                }
            }
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
    printf("[BOOT] Read-only DS1302; LCD1602 scan; no Bluetooth in this target\n");

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
    load_settings_at_boot();
    audio_queue_set_volume(&audio_queue, volume);
    puts("[AUDIO] DFPlayer UART0 GP16/17 BUSY GP18; no auto playback");
    countdown_init(&timer);
    puts("[CMD] Serial timer ready. Type help + Enter");
    char last_top[17] = {0};
    char last_bottom[17] = {0};
    char rtc_top[17] = "RTC unavailable";
    char rtc_bottom[17] = "Check GP2/5/4";
    uint64_t next_rtc_ms = 0;
    uint32_t previous_remaining = UINT32_MAX;
    while (true) {
        poll_serial();
        audio_queue_service(&audio_queue, to_ms_since_boot(get_absolute_time()));
        controls_poll(to_ms_since_boot(get_absolute_time()), on_control_event, NULL);
        uint64_t now = to_ms_since_boot(get_absolute_time());
        countdown_service(&timer, now);
        autosave_service(now);
        if (language_menu_open &&
            now - language_menu_last_input_ms >= LANGUAGE_MENU_TIMEOUT_MS) {
            puts("[MENU] Timeout (20s); discarding unconfirmed choice");
            close_language_menu(false);
        }
        if (countdown_consume_finished(&timer)) {
            puts("[TIMER] FINISHED");
            timer_audio_finished();
            timer_screen = false;
            finished_screen_until_ms = now + 3000u;
        }
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
            } else {
                snprintf(rtc_top, sizeof(rtc_top), "RTC unavailable");
                snprintf(rtc_bottom, sizeof(rtc_bottom), "Check GP2/5/4");
                // RTC is polled once a second, but missing RTC doesn't stop controls.
                puts("[RTC] Invalid/unavailable (run rtc-debug for raw bytes and error reasons)");
            }
        }

        char top[17], bottom[17];
        if (language_menu_open) {
            snprintf(top, sizeof(top), "LANGUAGE SETTING");
            snprintf(bottom, sizeof(bottom), "%s PRESS=SAVE",
                     language_menu_choice == SPEECH_RU ? "RU" : "DE");
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
