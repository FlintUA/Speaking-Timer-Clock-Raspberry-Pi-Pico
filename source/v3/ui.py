# Speaking Timer-Clock v3 - LCD UI state model

STATE_CLOCK = 0
STATE_QUICK_TIMER = 1
STATE_TIMER_EDIT_H = 10
STATE_TIMER_EDIT_M = 11
STATE_TIMER_EDIT_S = 12
STATE_TIMER_RUNNING = 13
STATE_TIMER_FINISHED = 14
STATE_SETTINGS = 20
STATE_SETTINGS_LANGUAGE = 21
STATE_SETTINGS_TIME_H = 22
STATE_SETTINGS_TIME_M = 23
STATE_SETTINGS_TIME_S = 24
STATE_SETTINGS_DATE_D = 25
STATE_SETTINGS_DATE_M = 26
STATE_SETTINGS_DATE_Y = 27
STATE_SETTINGS_QUIET_ENABLED = 28
STATE_SETTINGS_QUIET_START = 29
STATE_SETTINGS_QUIET_END = 30
STATE_SETTINGS_RTC_CORR = 31
STATE_ALARM_LIST = 40
STATE_ALARM_ENABLED = 41
STATE_ALARM_HOUR = 42
STATE_ALARM_MINUTE = 43
STATE_ALARM_SOUND = 44
STATE_ALARM_TRACK = 45
STATE_ALARM_RINGING = 46
STATE_MUSIC_PLAYER = 50

MONTH_NAMES = {
    1: "Jan", 2: "Feb", 3: "Mar", 4: "Apr", 5: "Mai", 6: "Jun",
    7: "Jul", 8: "Aug", 9: "Sep", 10: "Okt", 11: "Nov", 12: "Dez",
}

DAY_NAMES = {
    1: "Mo", 2: "Di", 3: "Mi", 4: "Do", 5: "Fr", 6: "Sa", 7: "So",
}


def fit(text, width=16):
    text = str(text)
    return text[:width] + (" " * max(0, width - len(text)))


class ClockUI:
    def __init__(self, lcd):
        self.lcd = lcd
        self.state = STATE_CLOCK
        self._last_lines = (None, None)
        self._cursor = None

    def set_state(self, state):
        self.state = state
        self._last_lines = (None, None)
        self.cursor_off()

    def cursor_off(self):
        if self._cursor is not None:
            self.lcd.hide_cursor()
            self._cursor = None

    def cursor_at(self, x, y=1, blink=True):
        wanted = (x, y, bool(blink))
        if self._cursor != wanted:
            if blink:
                self.lcd.blink_cursor_on()
            else:
                self.lcd.show_cursor()
            self._cursor = wanted
        self.lcd.move_to(x, y)

    def _write(self, line1, line2, cursor=None):
        lines = (fit(line1), fit(line2))
        if lines[0] != self._last_lines[0]:
            self.lcd.move_to(0, 0)
            self.lcd.putstr(lines[0])
        if lines[1] != self._last_lines[1]:
            self.lcd.move_to(0, 1)
            self.lcd.putstr(lines[1])
        self._last_lines = lines
        if cursor is None:
            self.cursor_off()
        else:
            self.cursor_at(cursor[0], cursor[1], cursor[2])

    def show_clock(self, now, sound_enabled=True, clock_mode="voice", quiet=False):
        mode = "MO" if clock_mode == "voice" else "ST"
        status = "M" if not sound_enabled else ("N" if quiet else "S")
        self._write(
            "%02d:%02d:%02d %s %s" % (
                now["hour"], now["minute"], now["second"], mode, status
            ),
            "%02d-%s-%04d %s" % (
                now["day"], MONTH_NAMES.get(now["month"], "???"),
                now["year"], DAY_NAMES.get(now["weekday"], "??")
            ),
        )

    def show_quick_timer(self, now, hours, minutes, seconds):
        mode = "MO" if now.get("clock_mode", "voice") == "voice" else "ST"
        self._write(
            "%02d:%02d:%02d %s" % (now["hour"], now["minute"], now["second"], mode),
            "TIMER %02d:%02d:%02d" % (hours, minutes, seconds),
        )

    def show_timer_edit(self, hours, minutes, seconds, field):
        cursor_x = {"h": 0, "m": 3, "s": 6}.get(field, 0)
        self._write("SET TIMER", "%02d:%02d:%02d" % (hours, minutes, seconds), (cursor_x, 1, True))

    def show_timer_running(self, hours, minutes, seconds):
        self._write("TIMER RUNNING", "%02d:%02d:%02d" % (hours, minutes, seconds))

    def show_timer_finished(self):
        self._write("TIMER FINISHED", "00:00:00")

    def show_volume(self, volume):
        self._write("VOLUME", "%02d / 30" % volume)

    def show_sound(self, enabled):
        self._write("SOUND", "ON" if enabled else "OFF")

    def show_clock_mode(self, clock_mode):
        self._write("CLOCK MODE", "VOICE" if clock_mode == "voice" else "CHIMES")

    def show_settings(self, index, total, label):
        self._write("SETTINGS %d/%d" % (index + 1, total), "> " + label)

    def show_language(self, language):
        self._write("LANGUAGE", "RUSSIAN" if language == "ru" else "DEUTSCH", (0, 1, True))

    def show_set_time(self, hour, minute, second, field):
        cursor_x = {"h": 0, "m": 3, "s": 6}.get(field, 0)
        self._write("SET TIME", "%02d:%02d:%02d" % (hour, minute, second), (cursor_x, 1, True))

    def show_set_date(self, day, month, year, field):
        cursor_x = {"d": 0, "m": 3, "y": 6}.get(field, 0)
        self._write("SET DATE", "%02d-%02d-%04d" % (day, month, year), (cursor_x, 1, True))

    def show_quiet_enabled(self, enabled):
        self._write("QUIET MODE", "ON" if enabled else "OFF", (0, 1, True))

    def show_quiet_time(self, start, hour):
        self._write("QUIET FROM" if start else "QUIET TO", "%02d:00" % hour, (0, 1, True))

    def show_rtc_correction(self, value):
        self._write("RTC CORRECTION", "%+d sec/day" % value, (0, 1, True))

    def show_alarm_list(self, index, enabled, hour, minute, sound, track):
        state = "ON" if enabled else "OFF"
        detail = "SIG" if sound == "signal" else "M%02d" % track
        self._write(
            "ALARM %d %s %s" % (index + 1, state, detail),
            "%02d:%02d DAILY" % (hour, minute),
        )

    def show_alarm_enabled(self, index, enabled):
        self._write("ALARM %d ENABLE" % (index + 1), "ON" if enabled else "OFF", (0, 1, True))

    def show_alarm_time(self, index, hour, minute, field):
        cursor_x = 0 if field == "h" else 3
        self._write("SET ALARM %d" % (index + 1), "%02d:%02d" % (hour, minute), (cursor_x, 1, True))

    def show_alarm_sound(self, index, sound):
        self._write(
            "ALARM %d SOUND" % (index + 1),
            "SIGNAL" if sound == "signal" else "MUSIC",
            (0, 1, True),
        )

    def show_alarm_track(self, index, track, preview=False):
        line2 = "TRACK %02d / 45" % track
        if preview:
            line2 = "PLAY %02d / 45" % track
        self._write("ALARM %d MUSIC" % (index + 1), line2, (6, 1, True))

    def show_alarm_ringing(self, index, hour, minute, sound, track):
        detail = "SIGNAL" if sound == "signal" else "MUSIC %02d" % track
        self._write(
            "ALARM %d RINGING" % (index + 1),
            "%s STOP=KEY" % detail,
        )

    def show_music_player(self, track, mode, paused=False, total=45):
        mode_name = {
            "normal": "NORM",
            "shuffle": "SHUF",
            "repeat": "REP1",
        }.get(mode, "SHUF")
        state = "PAUSE" if paused else "PLAY"
        total = max(1, int(total))
        self._write(
            "MUSIC %s" % mode_name,
            "%s %02d/%02d" % (state, track, total),
        )

    def show_message(self, line1, line2=""):
        self._write(line1, line2)
