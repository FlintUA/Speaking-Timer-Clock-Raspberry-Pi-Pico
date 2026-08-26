# Speaking Timer-Clock v3 - countdown timer engine
# MicroPython

import time

MAX_TIMER_SECONDS = 4 * 60 * 60


class CountdownTimer:
    def __init__(self):
        self.duration_seconds = 60
        self._deadline_ms = None
        self._running = False
        self._finished_event = False

    @property
    def running(self):
        return self._running

    def set_duration(self, hours=0, minutes=0, seconds=0):
        total = int(hours) * 3600 + int(minutes) * 60 + int(seconds)
        total = max(1, min(MAX_TIMER_SECONDS, total))
        self.duration_seconds = total
        return total

    def get_hms(self):
        total = self.duration_seconds
        return total // 3600, (total % 3600) // 60, total % 60

    def start(self):
        self._deadline_ms = time.ticks_add(
            time.ticks_ms(), self.duration_seconds * 1000
        )
        self._running = True
        self._finished_event = False

    def cancel(self):
        self._running = False
        self._deadline_ms = None
        self._finished_event = False

    def remaining_seconds(self):
        if not self._running or self._deadline_ms is None:
            return 0
        remaining_ms = time.ticks_diff(self._deadline_ms, time.ticks_ms())
        if remaining_ms <= 0:
            return 0
        return (remaining_ms + 999) // 1000

    def remaining_hms(self):
        total = self.remaining_seconds()
        return total // 3600, (total % 3600) // 60, total % 60

    def service(self):
        if self._running and self.remaining_seconds() == 0:
            self._running = False
            self._deadline_ms = None
            self._finished_event = True

    def consume_finished(self):
        if self._finished_event:
            self._finished_event = False
            return True
        return False
