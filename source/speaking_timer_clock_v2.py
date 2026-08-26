# Speaking Timer-Clock v2 for Raspberry Pi Pico
# MicroPython
#
# Hardware mapping preserved from the original project:
# LCD 1602 I2C: SDA GP0, SCL GP1, address 0x3F
# DS1302: CLK GP2, DAT GP5, RST GP4
# DFPlayer Mini: UART0 TX GP16, RX GP17, BUSY GP18
# Encoder 1: A GP14, B GP15, button GP20
# Encoder 2: A GP11, B GP10, button GP19
# Extra buttons: GP28, GP21, GP22, GP26, GP27

import random
import time
from machine import Pin, I2C

from pico_i2c_lcd import I2cLcd
from picozero import Button
from picodfplayer import DFPlayer
from ds1302 import DS1302
from rotary import Rotary


# ----------------------------- configuration -----------------------------

I2C_ADDR = 0x3F
I2C_NUM_ROWS = 2
I2C_NUM_COLS = 16

QUIET_START_HOUR = 22
QUIET_END_HOUR = 7
MAX_TIMER_MINUTES = 240
DEFAULT_TIMER_MINUTES = 1
DEFAULT_VOLUME = 10
MUSIC_TRACK_COUNT = 45

# Fixed daily alarms, HHMM.
FIXED_ALARMS = [1149, 1235, 1400, 1600, 1700]

# Confirmed legacy DFPlayer microSD folder mapping.
FOLDER_RU_HOURS = 1
FOLDER_RU_MINUTES = 2
FOLDER_RU_DAYS = 3
FOLDER_RU_MONTHS = 4
FOLDER_RU_YEARS = 5
FOLDER_RU_WEEKDAYS = 6
FOLDER_RU_PHRASES = 7
FOLDER_MUSIC = 8
FOLDER_RU_NUMBERS_0_30 = 9
FOLDER_RESERVED_10 = 10
FOLDER_DE_HOURS = 11
FOLDER_DE_MINUTES = 12
FOLDER_DE_DAYS = 13
FOLDER_DE_MONTHS = 14
FOLDER_DE_YEARS = 15
FOLDER_DE_WEEKDAYS = 16
FOLDER_DE_PHRASES = 17
FOLDER_CHIMES = 18
FOLDER_SILENCE = 19
FOLDER_RESERVED_20 = 20
FOLDER_RESERVED_21 = 21

# Confirmed phrase file numbers in folders 07 (RU) and 17 (DE).
PHRASE_ALARM_1 = 1
PHRASE_ALARM_2 = 2
PHRASE_ALARM_3 = 3
PHRASE_ALARM_4 = 4
PHRASE_ALARM_5 = 5
PHRASE_TIMER_SETUP = 6
PHRASE_TIMER_SET = 9
PHRASE_TIMER_FINISHED = 11
PHRASE_TIMER_SIGNAL_SHORT = 12
PHRASE_TIMER_SIGNAL_LONG = 13
PHRASE_TIMER_CANCELLED = 14

SUPPORTED_SPOKEN_YEARS = tuple(range(2023, 2030))

MONTH_NAMES = {
    1: "Jan", 2: "Feb", 3: "Mar", 4: "Apr", 5: "Mai", 6: "Jun",
    7: "Jul", 8: "Aug", 9: "Sep", 10: "Okt", 11: "Nov", 12: "Dez",
}

DAY_NAMES = {
    1: "Mo", 2: "Di", 3: "Mi", 4: "Do", 5: "Fr", 6: "Sa", 7: "So",
}


# ----------------------------- hardware -----------------------------

player = DFPlayer(0, 16, 17, 18)
dfplayer_busy = Pin(18, Pin.IN, Pin.PULL_UP)

ds = DS1302(Pin(2), Pin(5), Pin(4))

i2c = I2C(0, sda=Pin(0), scl=Pin(1), freq=400000)
lcd = I2cLcd(i2c, I2C_ADDR, I2C_NUM_ROWS, I2C_NUM_COLS)

# Encoder push buttons are intentionally handled separately by picozero.Button.
# Rotary owns only the two phase pins, avoiding competing IRQ handlers.
rotary_volume = Rotary(14, 15)
rotary_timer = Rotary(11, 10)


# ----------------------------- runtime state -----------------------------

volume = DEFAULT_VOLUME
sound_enabled = True
selected_timer_minutes = DEFAULT_TIMER_MINUTES

timer_running = False
timer_deadline_ms = None

music_enabled = False
music_playing = False
playlist = list(range(1, MUSIC_TRACK_COUNT + 1))
playlist_index = 0

last_display_second = None
last_announcement_key = None
last_alarm_key = None


# ----------------------------- helpers -----------------------------

def rtc_now():
    value = ds.date_time()
    return {
        "year": value[0],
        "month": value[1],
        "day": value[2],
        "weekday": value[3],
        "hour": value[4],
        "minute": value[5],
        "second": value[6],
    }


def in_quiet_hours(hour):
    if QUIET_START_HOUR < QUIET_END_HOUR:
        return QUIET_START_HOUR <= hour < QUIET_END_HOUR
    return hour >= QUIET_START_HOUR or hour < QUIET_END_HOUR


def audio_allowed(now=None):
    if not sound_enabled:
        return False
    if now is None:
        now = rtc_now()
    return not in_quiet_hours(now["hour"])


def current_hhmm(now):
    return now["hour"] * 100 + now["minute"]


def format_lcd_line(text):
    text = str(text)
    if len(text) > I2C_NUM_COLS:
        return text[:I2C_NUM_COLS]
    return text + (" " * (I2C_NUM_COLS - len(text)))


def timer_remaining_seconds():
    if not timer_running or timer_deadline_ms is None:
        return 0
    remaining = time.ticks_diff(timer_deadline_ms, time.ticks_ms())
    if remaining <= 0:
        return 0
    return (remaining + 999) // 1000


def stop_music():
    global music_enabled, music_playing
    player.pause()
    music_enabled = False
    music_playing = False


def shuffle_playlist():
    global playlist_index
    for i in range(len(playlist) - 1, 0, -1):
        j = random.randint(0, i)
        playlist[i], playlist[j] = playlist[j], playlist[i]
    playlist_index = 0


def play_phrase(folder, phrase_number, wait_after=0.0):
    if not sound_enabled:
        return False
    player.playTrack(folder, phrase_number)
    if wait_after > 0:
        time.sleep(wait_after)
    return True


def play_bilingual_phrase(phrase_number, wait_between=0.8):
    if not sound_enabled:
        return
    player.playTrack(FOLDER_RU_PHRASES, phrase_number)
    time.sleep(wait_between)
    player.playTrack(FOLDER_DE_PHRASES, phrase_number)


# ----------------------------- speech -----------------------------

def speak_time_ru(now=None):
    if now is None:
        now = rtc_now()
    if not audio_allowed(now):
        return
    player.playTrack(FOLDER_RU_HOURS, now["hour"])
    time.sleep(0.7)
    player.playTrack(FOLDER_RU_MINUTES, now["minute"])
    time.sleep(1.0)


def speak_time_de(now=None):
    if now is None:
        now = rtc_now()
    if not audio_allowed(now):
        return
    player.playTrack(FOLDER_DE_HOURS, now["hour"])
    time.sleep(0.7)
    player.playTrack(FOLDER_DE_MINUTES, now["minute"])
    time.sleep(1.0)


def speak_date_ru(now=None):
    if now is None:
        now = rtc_now()
    if not audio_allowed(now):
        return
    player.playTrack(FOLDER_RU_WEEKDAYS, now["weekday"])
    time.sleep(1.0)
    player.playTrack(FOLDER_RU_DAYS, now["day"])
    time.sleep(1.0)
    player.playTrack(FOLDER_RU_MONTHS, now["month"])
    time.sleep(1.0)


def speak_date_de(now=None):
    if now is None:
        now = rtc_now()
    if not audio_allowed(now):
        return
    player.playTrack(FOLDER_DE_WEEKDAYS, now["weekday"])
    time.sleep(1.0)
    player.playTrack(FOLDER_DE_DAYS, now["day"])
    time.sleep(1.6)
    player.playTrack(FOLDER_DE_MONTHS, now["month"])
    time.sleep(1.0)


def speak_year_ru(now=None):
    if now is None:
        now = rtc_now()
    year = now["year"]
    if not audio_allowed(now) or year not in SUPPORTED_SPOKEN_YEARS:
        return False
    player.playTrack(FOLDER_RU_YEARS, year % 10)
    time.sleep(1.0)
    return True


def speak_year_de(now=None):
    if now is None:
        now = rtc_now()
    year = now["year"]
    if not audio_allowed(now) or year not in SUPPORTED_SPOKEN_YEARS:
        return False
    player.playTrack(FOLDER_DE_YEARS, year % 10)
    time.sleep(1.0)
    return True


def speak_full_datetime():
    now = rtc_now()
    speak_time_ru(now)
    speak_date_ru(now)
    speak_year_ru(now)
    speak_time_de(now)
    speak_date_de(now)
    speak_year_de(now)


# ----------------------------- controls -----------------------------

def on_volume_rotary(event):
    global volume
    if event == Rotary.ROT_CW and volume < 30:
        volume += 1
    elif event == Rotary.ROT_CCW and volume > 0:
        volume -= 1
    else:
        return

    player.setVolume(volume)
    print("Volume:", volume)


def on_timer_rotary(event):
    global selected_timer_minutes
    if timer_running:
        return

    if event == Rotary.ROT_CW and selected_timer_minutes < MAX_TIMER_MINUTES:
        selected_timer_minutes += 1
    elif event == Rotary.ROT_CCW and selected_timer_minutes > 1:
        selected_timer_minutes -= 1
    else:
        return

    print("Timer selected:", selected_timer_minutes, "min")


def toggle_sound():
    global sound_enabled
    sound_enabled = not sound_enabled
    if not sound_enabled:
        stop_music()
    print("Sound enabled:", sound_enabled)


def toggle_timer():
    global timer_running, timer_deadline_ms

    if timer_running:
        timer_running = False
        timer_deadline_ms = None
        print("Timer cancelled")
        if sound_enabled:
            play_bilingual_phrase(PHRASE_TIMER_CANCELLED)
        return

    duration_ms = selected_timer_minutes * 60 * 1000
    timer_deadline_ms = time.ticks_add(time.ticks_ms(), duration_ms)
    timer_running = True
    print("Timer started for", selected_timer_minutes, "minutes")

    # The exact spoken representation of 31-240 minutes is not available in
    # the confirmed legacy folders, so v2 confirms the action without guessing.
    if audio_allowed():
        play_bilingual_phrase(PHRASE_TIMER_SET)


def toggle_music():
    global music_enabled, music_playing
    if music_enabled or music_playing:
        stop_music()
        print("Music stopped")
    else:
        music_enabled = True
        music_playing = False
        print("Music enabled")


def button_clock_demo():
    """Button 3 - German date/time + clock strike demo."""
    if not audio_allowed():
        return

    now = rtc_now()
    speak_time_de(now)
    speak_date_de(now)
    speak_year_de(now)

    # Folder 18: 000 = no strike/prelude, 001..012 = strike count.
    player.playTrack(FOLDER_CHIMES, 0)
    time.sleep(17)

    strike_hour = now["hour"] % 12
    if strike_hour == 0:
        strike_hour = 12
    player.playTrack(FOLDER_CHIMES, strike_hour)


def button_ru_datetime():
    now = rtc_now()
    speak_time_ru(now)
    speak_date_ru(now)
    speak_year_ru(now)


def button_full_datetime():
    speak_full_datetime()


rotary_volume.add_handler(on_volume_rotary)
rotary_timer.add_handler(on_timer_rotary)

btn_enc_volume = Button(20)
btn_enc_volume.when_pressed = toggle_sound

btn_enc_timer = Button(19)
btn_enc_timer.when_pressed = toggle_timer

btn_music = Button(28)
btn_music.when_pressed = toggle_music

btn_datetime = Button(21)
btn_datetime.when_pressed = button_full_datetime

btn_demo = Button(22)
btn_demo.when_pressed = button_clock_demo

btn_minus = Button(26)
btn_minus.when_pressed = button_ru_datetime

btn_plus = Button(27)
btn_plus.when_pressed = button_ru_datetime


# ----------------------------- periodic tasks -----------------------------

def service_music():
    global playlist_index, music_playing

    if not music_enabled or not sound_enabled or not audio_allowed():
        if music_playing:
            stop_music()
        return

    busy = not dfplayer_busy.value()
    if busy:
        music_playing = True
        return

    music_playing = False
    if playlist_index >= len(playlist):
        shuffle_playlist()

    track = playlist[playlist_index]
    playlist_index += 1
    player.playTrack(FOLDER_MUSIC, track)
    music_playing = True
    print("Music track:", track)


def service_timer():
    global timer_running, timer_deadline_ms

    if not timer_running:
        return

    if time.ticks_diff(timer_deadline_ms, time.ticks_ms()) <= 0:
        timer_running = False
        timer_deadline_ms = None
        print("Timer finished")
        if sound_enabled:
            player.playTrack(FOLDER_RU_PHRASES, PHRASE_TIMER_FINISHED)
            time.sleep(0.7)
            player.playTrack(FOLDER_DE_PHRASES, PHRASE_TIMER_FINISHED)
            time.sleep(0.7)
            # Long signal is used as the final timer alarm.
            player.playTrack(FOLDER_RU_PHRASES, PHRASE_TIMER_SIGNAL_LONG)


def service_announcements(now):
    global last_announcement_key

    if not audio_allowed(now):
        return

    if now["minute"] not in (0, 30):
        return

    key = (
        now["year"], now["month"], now["day"],
        now["hour"], now["minute"],
    )
    if key == last_announcement_key:
        return

    speak_time_ru(now)
    speak_time_de(now)
    last_announcement_key = key


def service_fixed_alarms(now):
    global last_alarm_key

    hhmm = current_hhmm(now)
    if hhmm not in FIXED_ALARMS:
        return

    key = (now["year"], now["month"], now["day"], hhmm)
    if key == last_alarm_key:
        return

    alarm_number = FIXED_ALARMS.index(hhmm) + 1
    print("Fixed alarm:", alarm_number, hhmm)

    if audio_allowed(now):
        player.playTrack(FOLDER_RU_PHRASES, alarm_number)
        time.sleep(1.5)
        speak_time_ru(now)
        time.sleep(0.7)
        player.playTrack(FOLDER_DE_PHRASES, alarm_number)
        time.sleep(1.5)
        speak_time_de(now)

    last_alarm_key = key


def update_display(now, force=False):
    global last_display_second

    if not force and now["second"] == last_display_second:
        return

    last_display_second = now["second"]

    sound_mark = "S" if sound_enabled else "M"
    timer_mark = "T" if timer_running else " "
    day_name = DAY_NAMES.get(now["weekday"], "??")

    line1 = "%02d:%02d:%02d %s%s %s" % (
        now["hour"], now["minute"], now["second"],
        sound_mark, timer_mark, day_name,
    )

    if timer_running:
        remain = timer_remaining_seconds()
        remain_min = (remain + 59) // 60
        line2 = "%02d-%s T:%03dm" % (
            now["day"], MONTH_NAMES.get(now["month"], "???"), remain_min,
        )
    else:
        line2 = "%02d-%s-%04d %03dm" % (
            now["day"], MONTH_NAMES.get(now["month"], "???"),
            now["year"], selected_timer_minutes,
        )

    lcd.move_to(0, 0)
    lcd.putstr(format_lcd_line(line1))
    lcd.move_to(0, 1)
    lcd.putstr(format_lcd_line(line2))


# ----------------------------- startup -----------------------------

print("Speaking Timer-Clock v2 starting")
player.setVolume(volume)
time.sleep(0.5)
player.pause()
shuffle_playlist()

# DS1302 should already contain valid time. Uncomment once when setting it:
# ds.date_time([2026, 8, 26, 3, 14, 30, 0])

update_display(rtc_now(), force=True)


# ----------------------------- main loop -----------------------------

while True:
    now = rtc_now()

    service_timer()
    service_fixed_alarms(now)
    service_announcements(now)
    service_music()
    update_display(now)

    # Timer accuracy does not depend on this delay because ticks_ms() is used.
    time.sleep_ms(50)
