// Minimal *read-only* hardware bring-up for the C firmware migration.
// Does not touch RTC registers, DFPlayer, settings, flash or Bluetooth.
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ds1302.h"
#include "lcd1602_i2c.h"

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

    char last_top[17] = {0};
    char last_bottom[17] = {0};
    while (true) {
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
        sleep_ms(1000);
    }
}
