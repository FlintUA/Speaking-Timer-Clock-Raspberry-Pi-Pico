from machine import Pin

DS1302_REG_SECOND = 0x80
DS1302_REG_MINUTE = 0x82
DS1302_REG_HOUR = 0x84
DS1302_REG_DAY = 0x86
DS1302_REG_MONTH = 0x88
DS1302_REG_WEEKDAY = 0x8A
DS1302_REG_YEAR = 0x8C
DS1302_REG_WP = 0x8E
DS1302_REG_CTRL = 0x90
DS1302_REG_RAM = 0xC0


class DS1302:
    def __init__(self, clk, dio, cs):
        self.clk = clk
        self.dio = dio
        self.cs = cs
        self.clk.init(Pin.OUT)
        self.cs.init(Pin.OUT)
        self.clk.value(0)
        self.cs.value(0)

    def _dec2hex(self, dat):
        return (dat // 10) * 16 + (dat % 10)

    def _hex2dec(self, dat):
        return (dat // 16) * 10 + (dat % 16)

    def _write_byte(self, dat):
        self.dio.init(Pin.OUT)
        for i in range(8):
            self.dio.value((dat >> i) & 1)
            self.clk.value(1)
            self.clk.value(0)

    def _read_byte(self):
        d = 0
        self.dio.init(Pin.IN)
        for i in range(8):
            d |= self.dio.value() << i
            self.clk.value(1)
            self.clk.value(0)
        return d

    def _get_reg(self, reg):
        self.cs.value(1)
        self._write_byte(reg)
        value = self._read_byte()
        self.cs.value(0)
        return value

    def _set_reg(self, reg, dat):
        self.cs.value(1)
        self._write_byte(reg)
        self._write_byte(dat)
        self.cs.value(0)

    def _wr(self, reg, dat):
        self._set_reg(DS1302_REG_WP, 0x00)
        self._set_reg(reg, dat)
        self._set_reg(DS1302_REG_WP, 0x80)

    def start(self):
        raw = self._get_reg(DS1302_REG_SECOND + 1)
        self._wr(DS1302_REG_SECOND, raw & 0x7F)

    def stop(self):
        raw = self._get_reg(DS1302_REG_SECOND + 1)
        self._wr(DS1302_REG_SECOND, raw | 0x80)

    def second(self, second=None):
        if second is None:
            return self._hex2dec(self._get_reg(DS1302_REG_SECOND + 1) & 0x7F) % 60
        self._wr(DS1302_REG_SECOND, self._dec2hex(second % 60))

    def minute(self, minute=None):
        if minute is None:
            return self._hex2dec(self._get_reg(DS1302_REG_MINUTE + 1) & 0x7F)
        self._wr(DS1302_REG_MINUTE, self._dec2hex(minute % 60))

    def hour(self, hour=None):
        if hour is None:
            return self._hex2dec(self._get_reg(DS1302_REG_HOUR + 1) & 0x3F)
        self._wr(DS1302_REG_HOUR, self._dec2hex(hour % 24))

    def weekday(self, weekday=None):
        if weekday is None:
            return self._hex2dec(self._get_reg(DS1302_REG_WEEKDAY + 1) & 0x07)
        weekday = max(1, min(7, int(weekday)))
        self._wr(DS1302_REG_WEEKDAY, self._dec2hex(weekday))

    def day(self, day=None):
        if day is None:
            return self._hex2dec(self._get_reg(DS1302_REG_DAY + 1) & 0x3F)
        day = max(1, min(31, int(day)))
        self._wr(DS1302_REG_DAY, self._dec2hex(day))

    def month(self, month=None):
        if month is None:
            return self._hex2dec(self._get_reg(DS1302_REG_MONTH + 1) & 0x1F)
        month = max(1, min(12, int(month)))
        self._wr(DS1302_REG_MONTH, self._dec2hex(month))

    def year(self, year=None):
        if year is None:
            return self._hex2dec(self._get_reg(DS1302_REG_YEAR + 1)) + 2000
        year = max(2000, min(2099, int(year)))
        self._wr(DS1302_REG_YEAR, self._dec2hex(year % 100))

    def date_time(self, dat=None):
        if dat is None:
            return [
                self.year(),
                self.month(),
                self.day(),
                self.weekday(),
                self.hour(),
                self.minute(),
                self.second(),
            ]

        year, month, day, weekday, hour, minute, second = dat

        # Stop the oscillator while all calendar/time registers are updated.
        # Write-protect is disabled once for the complete transaction so the
        # date cannot end up only partially updated.
        current_second = self._get_reg(DS1302_REG_SECOND + 1)
        self._set_reg(DS1302_REG_WP, 0x00)
        self._set_reg(DS1302_REG_SECOND, current_second | 0x80)

        self._set_reg(DS1302_REG_YEAR, self._dec2hex(max(2000, min(2099, int(year))) % 100))
        self._set_reg(DS1302_REG_MONTH, self._dec2hex(max(1, min(12, int(month)))))
        self._set_reg(DS1302_REG_DAY, self._dec2hex(max(1, min(31, int(day)))))
        self._set_reg(DS1302_REG_WEEKDAY, self._dec2hex(max(1, min(7, int(weekday)))))
        self._set_reg(DS1302_REG_HOUR, self._dec2hex(int(hour) % 24))
        self._set_reg(DS1302_REG_MINUTE, self._dec2hex(int(minute) % 60))
        self._set_reg(DS1302_REG_SECOND, self._dec2hex(int(second) % 60))

        self._set_reg(DS1302_REG_WP, 0x80)

    def ram(self, reg, dat=None):
        if dat is None:
            return self._get_reg(DS1302_REG_RAM + 1 + (reg % 31) * 2)
        self._wr(DS1302_REG_RAM + (reg % 31) * 2, dat)
