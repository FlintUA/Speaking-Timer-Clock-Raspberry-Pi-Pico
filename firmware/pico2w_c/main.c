// Minimal *read-only* hardware bring-up for the C firmware migration.
// Does not touch RTC registers, DFPlayer, settings, flash or Bluetooth.
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

static void print_timer_status(uint64_t now_ms) {
    uint32_t remaining = countdown_remaining(&timer, now_ms);
    printf("[TIMER] %s duration=%lus remaining=%lus\n",
           timer.running ? "RUNNING" : "STOPPED",
           (unsigned long)timer.duration_seconds, (unsigned long)remaining);
}

static void handle_command(char *line) {
    while (isspace((unsigned char)*line)) ++line;
    size_t len = strlen(line);
    while (len && isspace((unsigned char)line[len - 1])) line[--len] = 0;
    uint64_t now = to_ms_since_boot(get_absolute_time());
    if (strcmp(line, "help") == 0 || strcmp(line, "h") == 0) {
        puts("[CMD] t <seconds> (1..14400), start, cancel, status, help");
    } else if (strcmp(line, "start") == 0) {
        countdown_start(&timer, now);
        print_timer_status(now);
    } else if (strcmp(line, "cancel") == 0) {
        countdown_cancel(&timer);
        print_timer_status(now);
    } else if (strcmp(line, "status") == 0) {
        print_timer_status(now);
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
    if (event->type == CONTROL_VOLUME_ROTATE) {
        printf("[INPUT] encoder VOLUME %s\n", event->direction > 0 ? "CW" : "CCW");
    } else if (event->type == CONTROL_TIMER_ROTATE) {
        printf("[INPUT] encoder TIMER %s\n", event->direction > 0 ? "CW" : "CCW");
    } else {
        printf("[INPUT] button %s %s\n",
               controls_button_name(event->button),
               event->type == CONTROL_BUTTON_PRESS ? "PRESS" : "RELEASE");
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
    countdown_init(&timer);
    puts("[CMD] Serial timer ready. Type help + Enter");
    char last_top[17] = {0};
    char last_bottom[17] = {0};
    uint64_t next_rtc_ms = 0;
    uint32_t previous_remaining = UINT32_MAX;
    while (true) {
        poll_serial();
        controls_poll(to_ms_since_boot(get_absolute_time()), on_control_event, NULL);
        uint64_t now = to_ms_since_boot(get_absolute_time());
        countdown_service(&timer, now);
        if (countdown_consume_finished(&timer)) {
            puts("[TIMER] FINISHED");
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
        if (now < next_rtc_ms) {
            sleep_ms(10);
            continue;
        }
        next_rtc_ms = now + 1000u;
        ds1302_datetime_t dt = {0};
        bool valid = ds1302_read_datetime(&dt);
        char top[17], bottom[17];
        if (valid) {
            snprintf(top, sizeof(top), "%02u:%02u:%02u",
                     (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
            snprintf(bottom, sizeof(bottom), "%02u.%02u.%04u",
                     (unsigned)dt.day, (unsigned)dt.month, (unsigned)dt.year);
            printf("[RTC] %s %s\n", top, bottom);
        } else {
            snprintf(top, sizeof(top), "RTC unavailable");
            snprintf(bottom, sizeof(bottom), "Check GP2/5/4");
            printf("[RTC] Invalid/unavailable (power, wiring, or oscillator)\n");
        }
        if (lcd_ok && __builtin_strcmp(top, last_top) != 0) {
            lcd1602_write_line(0, top);
            snprintf(last_top, sizeof(last_top), "%s", top);
        }
        if (lcd_ok && __builtin_strcmp(bottom, last_bottom) != 0) {
            lcd1602_write_line(1, bottom);
            snprintf(last_bottom, sizeof(last_bottom), "%s", bottom);
        }
    }
}
