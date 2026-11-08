#include "lcd1602_i2c.h"
#include <assert.h>
#include <stddef.h>

static i2c_inst_t bus;
static uint8_t bytes[2048];
static unsigned used, calls, fail_at;

int i2c_write_blocking(i2c_inst_t *port, uint8_t address, const uint8_t *data,
                       size_t length, bool nostop) {
    assert(port==&bus && address==0x27 && length==1 && !nostop);
    ++calls;
    if(fail_at && calls==fail_at) return -1;
    assert(used<sizeof(bytes)); bytes[used++]=*data;
    return 1;
}
void sleep_ms(uint32_t ms) { (void)ms; }
void sleep_us(uint64_t us) { (void)us; }

// Decode the actual PCF8574 EN pulses back into HD44780 bytes.
static uint8_t decoded(unsigned at, bool data) {
    assert(at+3<used);
    assert((bytes[at]&4) && !(bytes[at+1]&4));
    assert((bytes[at+2]&4) && !(bytes[at+3]&4));
    assert((bytes[at]&1)==(data?1:0));
    return (uint8_t)((bytes[at]&0xf0)|(bytes[at+2]>>4));
}
int main(void) {
    assert(lcd1602_init(&bus,0x27));
    used=0;
    lcd1602_set_cursor(1,3,LCD1602_CURSOR_UNDERLINE);
    assert(used==8 && decoded(0,false)==0xc3 && decoded(4,false)==0x0e);
    unsigned before=calls;
    for(unsigned i=0;i<1000;++i) lcd1602_set_cursor(1,3,LCD1602_CURSOR_UNDERLINE);
    assert(calls==before); // Main-loop polling must not flood I2C.
    used=0;
    lcd1602_set_cursor(1,6,LCD1602_CURSOR_UNDERLINE);
    assert(used==4 && decoded(0,false)==0xc6);
    used=0;
    lcd1602_set_cursor(1,6,LCD1602_CURSOR_BLINK);
    assert(used==4 && decoded(0,false)==0x0f);
    used=0;
    lcd1602_set_cursor(0,0,LCD1602_CURSOR_OFF);
    assert(used==4 && decoded(0,false)==0x0c);
    before=calls;
    lcd1602_set_cursor(0,0,LCD1602_CURSOR_OFF);
    lcd1602_set_cursor(2,0,LCD1602_CURSOR_UNDERLINE);
    lcd1602_set_cursor(1,16,LCD1602_CURSOR_UNDERLINE);
    assert(calls==before);

    lcd1602_set_cursor(1,0,LCD1602_CURSOR_UNDERLINE);
    used=0;
    lcd1602_write_line(1,"01:23:45");
    assert(used==68 && decoded(0,false)==0xc0);
    assert(decoded(4,true)=='0' && decoded(64,true)==' ');
    lcd1602_set_cursor(1,0,LCD1602_CURSOR_UNDERLINE);
    assert(used==72 && decoded(68,false)==0xc0); // Repaint moved the real cursor.

    fail_at=calls+1;
    lcd1602_set_cursor(0,2,LCD1602_CURSOR_UNDERLINE);
    before=calls;
    lcd1602_set_cursor(0,2,LCD1602_CURSOR_UNDERLINE);
    lcd1602_write_line(0,"ignored after I2C failure");
    assert(calls==before);
    fail_at=0; used=0;
    assert(lcd1602_init(&bus,0x27));
    used=0;
    lcd1602_set_cursor(0,15,LCD1602_CURSOR_UNDERLINE);
    assert(used==8 && decoded(0,false)==0x8f && decoded(4,false)==0x0e);
    // Failure of a row address command must also disable subsequent writes.
    fail_at=calls+1;
    lcd1602_write_line(0,"not sent");
    before=calls;
    lcd1602_set_cursor(0,0,LCD1602_CURSOR_UNDERLINE);
    assert(calls==before);
    return 0;
}
