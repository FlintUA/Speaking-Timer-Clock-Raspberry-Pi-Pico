#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

// PCF8574 mapping matches library/pico_i2c_lcd.py:
// P0=RS P1=RW P2=EN P3=backlight P4..P7=LCD data.
bool lcd1602_init(i2c_inst_t *port, uint8_t address);
void lcd1602_write_line(unsigned row, const char *text);

typedef enum {
    LCD1602_CURSOR_OFF,
    LCD1602_CURSOR_UNDERLINE,
    LCD1602_CURSOR_BLINK
} lcd1602_cursor_t;

// Zero-based coordinates. Identical requests do not generate I2C traffic.
void lcd1602_set_cursor(unsigned row, unsigned column, lcd1602_cursor_t mode);
