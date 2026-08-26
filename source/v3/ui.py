# Speaking Timer-Clock v3 - LCD UI state model

STATE_CLOCK = 0
STATE_TIMER_EDIT_H = 10
STATE_TIMER_EDIT_M = 11
STATE_TIMER_EDIT_S = 12
STATE_TIMER_RUNNING = 13
STATE_SETTINGS = 20
STATE_SETTINGS_LANGUAGE = 21

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

    def set_state(self, state):
        self.state = state
        self._last_lines = (None, None)

    def _write(self, line1, line2):
        lines = (fit(line1), fit(line2))
        if lines == self._last_lines:
            return
        if lines[0] != self._last_lines[0]:
            self.lcd.move_to(0, 0)
            self.lcd.putstr(lines[0])
        if lines[1] != self._last_lines[1]:
            self.lcd.move_to(0, 1)
            self.lcd.putstr(lines[1])
        self._last_lines = lines

    def show_clock(self, now, language, sound_enabled=True, clock_mode="voice"):
        lang = language.upper()
        sound = "S" if sound_enabled else "M"
        mode = "MO" if clock_mode == "voice" else "ST"
        line1 = "%02d:%02d:%02d %s %s" % (
            now["hour"], now["minute"], now["second"], mode, sound
        )
        line2 = "%02d-%s-%04d %s" % (
            now["day"], MONTH_NAMES.get(now["month"], "???"),
            now["year"], lang
        )
        self._write(line1, line2)

    def show_timer_edit(self, hours, minutes, seconds, field):
        labels = {"h": "HOUR", "m": "MIN", "s": "SEC"}
        self._write(
            "SET TIMER %-4s" % labels.get(field, ""),
            "%02d:%02d:%02d" % (hours, minutes, seconds),
        )

    def show_timer_running(self, hours, minutes, seconds):
        self._write("TIMER RUNNING", "%02d:%02d:%02d" % (hours, minutes, seconds))

    def show_language(self, language):
        value = "RUSSIAN" if language == "ru" else "DEUTSCH"
        self._write("LANGUAGE", "> " + value)

    def show_settings(self):
        self._write("SETTINGS", "> Language")
