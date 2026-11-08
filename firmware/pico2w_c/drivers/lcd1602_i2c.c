#include "lcd1602_i2c.h"
#include "pico/stdlib.h"
#include <string.h>

static i2c_inst_t *s_i2c;
static uint8_t s_addr;
static bool s_ready;
static uint8_t s_display_control;
static unsigned s_cursor_row, s_cursor_column;
static bool s_cursor_position_valid;

static bool expander_write(uint8_t b) {
    return i2c_write_blocking(s_i2c, s_addr, &b, 1, false) == 1;
}

static bool send_nibble(uint8_t nibble, bool rs) {
    uint8_t b = (uint8_t)((nibble & 0x0fu) << 4) | 0x08u | (rs ? 0x01u : 0);
    return expander_write(b | 0x04u) && expander_write(b);
}

static bool send_byte(uint8_t b, bool rs) {
    if (!send_nibble(b >> 4, rs) || !send_nibble(b & 0x0fu, rs)) return false;
    if (!rs && (b == 0x01 || b == 0x02)) sleep_ms(3);
    else sleep_us(50);
    return true;
}

bool lcd1602_init(i2c_inst_t *port, uint8_t address) {
    s_i2c = port;
    s_addr = address;
    s_ready = false;
    s_cursor_position_valid = false;
    if (!expander_write(0)) return false;
    sleep_ms(25);
    if (!send_nibble(0x03, false)) return false;
    sleep_ms(5);
    if (!send_nibble(0x03, false)) return false;
    sleep_ms(1);
    if (!send_nibble(0x03, false)) return false;
    sleep_ms(1);
    if (!send_nibble(0x02, false)) return false;
    sleep_ms(1);
    if (!send_byte(0x28, false) || // 4-bit, two lines
        !send_byte(0x08, false) || // display off
        !send_byte(0x01, false) || // clear
        !send_byte(0x06, false) || // entry mode
        !send_byte(0x0c, false)) return false; // display on
    s_ready = true;
    s_display_control = 0x0c;
    return true;
}

void lcd1602_write_line(unsigned row, const char *text) {
    if (!s_ready || row > 1 || !text) return;
    s_cursor_position_valid = false;
    if (!send_byte((uint8_t)(0x80 + (row ? 0x40 : 0)), false)) {
        s_ready = false;
        return;
    }
    size_t n = strlen(text);
    for (unsigned i = 0; i < 16; ++i) {
        char c = (i < n) ? text[i] : ' ';
        if (!send_byte((uint8_t)c, true)) {
            s_ready = false;
            return;
        }
    }
}

void lcd1602_set_cursor(unsigned row, unsigned column, lcd1602_cursor_t mode) {
    if (!s_ready || (mode != LCD1602_CURSOR_OFF && mode != LCD1602_CURSOR_UNDERLINE &&
                     mode != LCD1602_CURSOR_BLINK)) return;
    if (mode != LCD1602_CURSOR_OFF) {
        if (row > 1 || column > 15) return;
        if (!s_cursor_position_valid || row != s_cursor_row || column != s_cursor_column) {
            if (!send_byte((uint8_t)(0x80u + (row ? 0x40u : 0u) + column), false)) {
                s_ready = false;
                return;
            }
            s_cursor_row = row;
            s_cursor_column = column;
            s_cursor_position_valid = true;
        }
    }
    uint8_t control = mode == LCD1602_CURSOR_OFF ? 0x0c :
                      mode == LCD1602_CURSOR_UNDERLINE ? 0x0e : 0x0f;
    if (control != s_display_control) {
        if (!send_byte(control, false)) { s_ready = false; return; }
        s_display_control = control;
    }
}
