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

    def _send(self, command, p1=0, p2=0, ack=None):
        if ack is None:
            ack = self.ACKNOWLEDGE
        ack = 1 if ack else 0
        checksum = -(
            self.VERSION_BYTE
            + self.COMMAND_LENGTH
            + command
            + ack
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
            ack,
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

    def _drain_uart(self):
        while self.uart.any():
            self.uart.read()

    def _wait_for_response(self, command, timeout_ms=700):
        deadline = time.ticks_add(time.ticks_ms(), int(timeout_ms))
        buffer = bytearray()

        while time.ticks_diff(deadline, time.ticks_ms()) > 0:
            if self.uart.any():
                data = self.uart.read()
                if data:
                    buffer.extend(data)

                while len(buffer) >= 10:
                    start = 0
                    while start < len(buffer) and buffer[start] != self.START_BYTE:
                        start += 1
                    if start:
                        buffer = buffer[start:]
                    if len(buffer) < 10:
                        break

                    if buffer[9] != self.END_BYTE:
                        buffer = buffer[1:]
                        continue

                    frame = buffer[:10]
                    buffer = buffer[10:]
                    if frame[3] == command:
                        return (frame[5] << 8) | frame[6]
            time.sleep_ms(10)

        return None

    def read_file_count_in_folder(self, folder, timeout_ms=700, retries=3):
        """Return DFPlayer file count for a numbered folder, or None on failure.

        DFPlayer query command 0x4E reports the number of files in a folder.
        Some compatible clones have unreliable feedback, so callers should
        retain a safe fallback count.
        """
        folder = max(1, min(99, int(folder)))
        retries = max(1, int(retries))

        for _ in range(retries):
            self._drain_uart()
            while not self.command_ready():
                time.sleep_ms(10)

            # Match DFRobot's query form: 16-bit argument 0x00, folder and ACK.
            self._send(0x4E, 0, folder, ack=1)
            value = self._wait_for_response(0x4E, timeout_ms)
            if value is not None and 1 <= value <= 255:
                return value
            time.sleep_ms(150)

        return None


class AudioQueue:
    """Queue tracks without blocking the application loop."""

    def __init__(self, transport):
        self.transport = transport
        self._queue = []
        self._track_started = False
        self._seen_busy = False
        self._start_ms = 0
        self._volume_pending = None
        self._pause_pending = False

    def clear(self, pause=False):
        self._queue = []
        self._track_started = False
        self._seen_busy = False
        if pause:
            if self.transport.command_ready():
                self.transport.pause()
                self._pause_pending = False
            else:
                self._pause_pending = True

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
        if self._pause_pending:
            if self.transport.command_ready():
                self.transport.pause()
                self._pause_pending = False
            return

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

# The half-hour sound is language-neutral. Track 015 is the same classic
# double-beep in service folders 07 and 17. The current ST path historically
# imports these two names, so keep them as compatibility aliases and point
# them at the confirmed RU copy (07/015). The DE copy 17/015 remains mirrored.
FOLDER_SILENCE = 7
SILENCE_HALF_HOUR_TRACK = 15

# Confirmed service phrase map in folders 07 (RU) and 17 (DE):
# 006 - timer setup
# 007 - timer set / timer started
# 008 - sound on
# 009 - sound off
# 010 - volume
# 011 - timer finished
# 012 - short timer signal
# 013 - long timer signal
# 014 - timer cancelled
# 015 - half-hour double beep
# 016 - short UI click (same sound in 07 and 17)
# 017 - German-only "Minuten" in folder 17
PHRASE_TIMER_SETUP = 6
PHRASE_TIMER_SET = 7
PHRASE_SOUND_ON = 8
PHRASE_SOUND_OFF = 9
PHRASE_VOLUME = 10
PHRASE_TIMER_FINISHED = 11
PHRASE_TIMER_SIGNAL_SHORT = 12
PHRASE_TIMER_SIGNAL_LONG = 13
PHRASE_TIMER_CANCELLED = 14
PHRASE_HALF_HOUR_DOUBLE_BEEP = 15
PHRASE_UI_CLICK = 16
PHRASE_DE_MINUTEN = 17


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
