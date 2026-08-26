# Speaking Timer-Clock v3 - non-blocking DFPlayer transport and queue
# MicroPython

import time
from machine import UART, Pin


class DFPlayerTransport:
    START_BYTE = 0x7E
    VERSION_BYTE = 0xFF
    COMMAND_LENGTH = 0x06
    ACKNOWLEDGE = 0x00
    END_BYTE = 0xEF

    def __init__(self, uart_id, tx_pin, rx_pin, busy_pin):
        self.uart = UART(
            uart_id,
            baudrate=9600,
            tx=Pin(tx_pin),
            rx=Pin(rx_pin),
            bits=8,
            parity=None,
            stop=1,
        )
        self.busy_pin = Pin(busy_pin, Pin.IN, Pin.PULL_UP)
        self._last_command_ms = time.ticks_ms()

    def busy(self):
        return self.busy_pin.value() == 0

    def _send(self, command, p1=0, p2=0):
        checksum = -(
            self.VERSION_BYTE
            + self.COMMAND_LENGTH
            + command
            + self.ACKNOWLEDGE
            + p1
            + p2
        )
        high = (checksum >> 8) & 0xFF
        low = checksum & 0xFF
        packet = bytes((
            self.START_BYTE,
            self.VERSION_BYTE,
            self.COMMAND_LENGTH,
            command,
            self.ACKNOWLEDGE,
            p1 & 0xFF,
            p2 & 0xFF,
            high,
            low,
            self.END_BYTE,
        ))
        self.uart.write(packet)
        self._last_command_ms = time.ticks_ms()

    def command_ready(self, minimum_gap_ms=120):
        return time.ticks_diff(time.ticks_ms(), self._last_command_ms) >= minimum_gap_ms

    def play_track(self, folder, track):
        self._send(0x0F, int(folder), int(track))

    def set_volume(self, volume):
        self._send(0x06, 0, max(0, min(30, int(volume))))

    def pause(self):
        self._send(0x0E, 0, 0)

    def resume(self):
        self._send(0x0D, 0, 0)


class AudioQueue:
    """Queue tracks without blocking the application loop.

    Queue entries are (folder, track). The next item starts after DFPlayer BUSY
    has returned idle and a short guard interval has elapsed.
    """

    def __init__(self, transport):
        self.transport = transport
        self._queue = []
        self._track_started = False
        self._seen_busy = False
        self._start_ms = 0
        self._volume_pending = None

    def clear(self, pause=False):
        self._queue = []
        self._track_started = False
        self._seen_busy = False
        if pause and self.transport.command_ready():
            self.transport.pause()

    def enqueue(self, folder, track):
        self._queue.append((int(folder), int(track)))

    def enqueue_many(self, items):
        for folder, track in items:
            self.enqueue(folder, track)

    def set_volume(self, volume):
        self._volume_pending = max(0, min(30, int(volume)))

    def idle(self):
        return not self._queue and not self._track_started

    def service(self):
        # Apply volume outside encoder callbacks and without sleep().
        if self._volume_pending is not None and self.transport.command_ready():
            value = self._volume_pending
            self._volume_pending = None
            self.transport.set_volume(value)
            return

        if self._track_started:
            if self.transport.busy():
                self._seen_busy = True
                return

            elapsed = time.ticks_diff(time.ticks_ms(), self._start_ms)
            # Allow BUSY some time to assert. Once BUSY has been seen, idle means
            # playback completed. The timeout also tolerates clones with weak BUSY.
            if self._seen_busy or elapsed >= 2500:
                self._track_started = False
                self._seen_busy = False
            else:
                return

        if self._queue and self.transport.command_ready():
            folder, track = self._queue.pop(0)
            self.transport.play_track(folder, track)
            self._track_started = True
            self._seen_busy = False
            self._start_ms = time.ticks_ms()


# Confirmed physical folder mapping on the existing DFPlayer microSD.
LANGUAGE_FOLDERS = {
    "ru": {
        "hours": 1,
        "minutes": 2,
        "days": 3,
        "months": 4,
        "years": 5,
        "weekdays": 6,
        "phrases": 7,
        "numbers": 9,
    },
    "de": {
        "hours": 11,
        "minutes": 12,
        "days": 13,
        "months": 14,
        "years": 15,
        "weekdays": 16,
        "phrases": 17,
    },
}

FOLDER_MUSIC = 8
FOLDER_CHIMES = 18

PHRASE_TIMER_SETUP = 6
PHRASE_TIMER_SET = 9
PHRASE_TIMER_FINISHED = 11
PHRASE_TIMER_SIGNAL_SHORT = 12
PHRASE_TIMER_SIGNAL_LONG = 13
PHRASE_TIMER_CANCELLED = 14


class Speech:
    def __init__(self, audio_queue, language="ru"):
        self.audio = audio_queue
        self.language = "ru"
        self.set_language(language)

    def set_language(self, language):
        if language not in LANGUAGE_FOLDERS:
            language = "ru"
        self.language = language

    @property
    def folders(self):
        return LANGUAGE_FOLDERS[self.language]

    def phrase(self, number):
        self.audio.enqueue(self.folders["phrases"], number)

    def say_time(self, hour, minute):
        self.audio.enqueue(self.folders["hours"], hour)
        self.audio.enqueue(self.folders["minutes"], minute)

    def say_date(self, year, month, day, weekday):
        self.audio.enqueue(self.folders["weekdays"], weekday)
        self.audio.enqueue(self.folders["days"], day)
        self.audio.enqueue(self.folders["months"], month)
        if 2023 <= year <= 2029:
            self.audio.enqueue(self.folders["years"], year % 10)
