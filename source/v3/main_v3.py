# Speaking Timer-Clock v3 - modular hardware build
# Version: 3.1.0
# MicroPython / Raspberry Pi Pico

APP_VERSION = "3.1.0"

import time
from machine import Pin, I2C

from pico_i2c_lcd import I2cLcd
from picozero import Button
from ds1302 import DS1302
from rotary import Rotary

from config import load_config, save_config
from timer_engine import CountdownTimer
from audio import (
    DFPlayerTransport,
    AudioQueue,
    Speech,
    FOLDER_CHIMES,
    FOLDER_SILENCE,
    SILENCE_HALF_HOUR_TRACK,
    PHRASE_TIMER_FINISHED,
    PHRASE_TIMER_SIGNAL_LONG,
    PHRASE_TIMER_CANCELLED,
)
from ui import (
    ClockUI,
    STATE_CLOCK,
    STATE_QUICK_TIMER,
    STATE_TIMER_EDIT_H,
    STATE_TIMER_EDIT_M,
    STATE_TIMER_EDIT_S,
    STATE_TIMER_RUNNING,
    STATE_TIMER_FINISHED,
    STATE_SETTINGS,
    STATE_SETTINGS_LANGUAGE,
    STATE_SETTINGS_TIME_H,
    STATE_SETTINGS_TIME_M,
    STATE_SETTINGS_TIME_S,
    STATE_SETTINGS_DATE_D,
    STATE_SETTINGS_DATE_M,
    STATE_SETTINGS_DATE_Y,
    STATE_SETTINGS_QUIET_ENABLED,
    STATE_SETTINGS_QUIET_START,
    STATE_SETTINGS_QUIET_END,
    STATE_SETTINGS_RTC_CORR,
)

I2C_ADDR = 0x3F
QUICK_TIMER_TIMEOUT_MS = 2500
TIMER_FINISHED_TIMEOUT_MS = 5000
OVERLAY_TIMEOUT_MS = 1600
SETTINGS_TIMEOUT_MS = 20000
TIMER_MAX_MINUTES = 240

lcd = I2cLcd(
    I2C(0, sda=Pin(0), scl=Pin(1), freq=400000),
    I2C_ADDR,
    2,
    16,
)
rtc = DS1302(Pin(2), Pin(5), Pin(4))
transport = DFPlayerTransport(0, 16, 17, 18)
audio = AudioQueue(transport)
rotary_volume = Rotary(14, 15)
rotary_timer = Rotary(11, 10)

config = load_config()
speech = Speech(audio, config["language"])
timer = CountdownTimer()
ui = ClockUI(lcd)

sound_enabled = True
volume = config["volume"]
audio.set_volume(volume)

edit_h, edit_m, edit_s = timer.get_hms()
edit_time_h = edit_time_m = edit_time_s = 0
edit_date_d = edit_date_m = 1
edit_date_y = 2026
edit_quiet_enabled = config["quiet_enabled"]
edit_quiet_start = config["quiet_start"]
edit_quiet_end = config["quiet_end"]
edit_rtc_corr = config["rtc_correction_sec_per_day"]

settings_items = (
    "Language",
    "Time",
    "Date",
    "Quiet mode",
    "RTC correction",
    "Alarms",
)
settings_index = 0
last_input_ms = time.ticks_ms()
quick_until_ms = 0
timer_finished_until_ms = 0
overlay_until_ms = 0
overlay_kind = None
overlay_value = None

last_auto_key = None
last_rtc_correction_key = None
last_timer_rotary_ms = 0
timer_fast_streak = 0


def mark_input():
    global last_input_ms
    last_input_ms = time.ticks_ms()


def rtc_now():
    value = rtc.date_time()
    return {
        "year": value[0],
        "month": value[1],
        "day": value[2],
        "weekday": value[3],
        "hour": value[4],
        "minute": value[5],
        "second": value[6],
    }


def quiet_now(now):
    if not config["quiet_enabled"]:
        return False
    start = config["quiet_start"]
    end = config["quiet_end"]
    if start < end:
        return start <= now["hour"] < end
    return now["hour"] >= start or now["hour"] < end


def _wrap(value, minimum, maximum, delta):
    value += delta
    if value > maximum:
        return minimum
    if value < minimum:
        return maximum
    return value


def is_leap(year):
    return (year % 4 == 0 and year % 100 != 0) or (year % 400 == 0)


def days_in_month(year, month):
    if month == 2:
        return 29 if is_leap(year) else 28
    if month in (4, 6, 9, 11):
        return 30
    return 31


def weekday_for_date(year, month, day):
    # Sakamoto algorithm converted to Monday=1 ... Sunday=7.
    offsets = (0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4)
    y = year - (1 if month < 3 else 0)
    sunday_zero = (
        y + y // 4 - y // 100 + y // 400 + offsets[month - 1] + day
    ) % 7
    return 7 if sunday_zero == 0 else sunday_zero


def write_rtc(year, month, day, hour, minute, second):
    day = max(1, min(days_in_month(year, month), day))
    weekday = weekday_for_date(year, month, day)
    rtc.date_time([year, month, day, weekday, hour, minute, second])
    rtc.start()


def show_overlay(kind, value=None, duration_ms=OVERLAY_TIMEOUT_MS):
    global overlay_kind, overlay_value, overlay_until_ms
    overlay_kind = kind
    overlay_value = value
    overlay_until_ms = time.ticks_add(time.ticks_ms(), duration_ms)
    mark_input()


def overlay_active():
    if overlay_kind is None:
        return False
    return time.ticks_diff(overlay_until_ms, time.ticks_ms()) > 0


def clear_overlay():
    global overlay_kind, overlay_value, overlay_until_ms
    overlay_kind = None
    overlay_value = None
    overlay_until_ms = 0


def timer_accel_step():
    global last_timer_rotary_ms, timer_fast_streak
    now_ms = time.ticks_ms()
    elapsed = time.ticks_diff(now_ms, last_timer_rotary_ms)
    last_timer_rotary_ms = now_ms

    if elapsed < 120:
        timer_fast_streak += 1
    elif elapsed < 260:
        timer_fast_streak = min(timer_fast_streak + 1, 4)
    else:
        timer_fast_streak = 0

    if timer_fast_streak >= 4:
        return 10
    if timer_fast_streak >= 2:
        return 5
    return 1


def set_quick_timer_delta(direction):
    global edit_h, edit_m, edit_s, quick_until_ms
    h, m, s = timer.get_hms()
    total_minutes = h * 60 + m
    step = timer_accel_step()
    total_minutes += direction * step
    total_minutes = max(1, min(TIMER_MAX_MINUTES, total_minutes))
    timer.set_duration(minutes=total_minutes)
    edit_h, edit_m, edit_s = timer.get_hms()
    ui.set_state(STATE_QUICK_TIMER)
    quick_until_ms = time.ticks_add(time.ticks_ms(), QUICK_TIMER_TIMEOUT_MS)
    mark_input()
    print(
        "Quick timer:",
        "%02d:%02d:%02d" % (edit_h, edit_m, edit_s),
        "step", step,
    )


def on_volume(event):
    global volume
    if event == Rotary.ROT_CW and volume < 30:
        volume += 1
    elif event == Rotary.ROT_CCW and volume > 0:
        volume -= 1
    else:
        return
    config["volume"] = volume
    audio.set_volume(volume)
    show_overlay("volume", volume)
    print("Volume:", volume)


def on_timer(event):
    global edit_h, edit_m, edit_s
    global edit_time_h, edit_time_m, edit_time_s
    global edit_date_d, edit_date_m, edit_date_y
    global edit_quiet_enabled, edit_quiet_start, edit_quiet_end, edit_rtc_corr
    global settings_index

    direction = 1 if event == Rotary.ROT_CW else -1
    mark_input()

    if ui.state in (STATE_CLOCK, STATE_QUICK_TIMER):
        set_quick_timer_delta(direction)
        return

    if ui.state == STATE_TIMER_EDIT_H:
        edit_h = _wrap(edit_h, 0, 4, direction)
    elif ui.state == STATE_TIMER_EDIT_M:
        edit_m = _wrap(edit_m, 0, 59, direction)
    elif ui.state == STATE_TIMER_EDIT_S:
        edit_s = _wrap(edit_s, 0, 59, direction)
    elif ui.state == STATE_SETTINGS:
        settings_index = _wrap(
            settings_index, 0, len(settings_items) - 1, direction
        )
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        config["language"] = "de" if config["language"] == "ru" else "ru"
        speech.set_language(config["language"])
    elif ui.state == STATE_SETTINGS_TIME_H:
        edit_time_h = _wrap(edit_time_h, 0, 23, direction)
    elif ui.state == STATE_SETTINGS_TIME_M:
        edit_time_m = _wrap(edit_time_m, 0, 59, direction)
    elif ui.state == STATE_SETTINGS_TIME_S:
        edit_time_s = _wrap(edit_time_s, 0, 59, direction)
    elif ui.state == STATE_SETTINGS_DATE_D:
        edit_date_d = _wrap(
            edit_date_d, 1, days_in_month(edit_date_y, edit_date_m), direction
        )
    elif ui.state == STATE_SETTINGS_DATE_M:
        edit_date_m = _wrap(edit_date_m, 1, 12, direction)
        edit_date_d = min(
            edit_date_d, days_in_month(edit_date_y, edit_date_m)
        )
    elif ui.state == STATE_SETTINGS_DATE_Y:
        edit_date_y = _wrap(edit_date_y, 2023, 2099, direction)
        edit_date_d = min(
            edit_date_d, days_in_month(edit_date_y, edit_date_m)
        )
    elif ui.state == STATE_SETTINGS_QUIET_ENABLED:
        edit_quiet_enabled = not edit_quiet_enabled
    elif ui.state == STATE_SETTINGS_QUIET_START:
        edit_quiet_start = _wrap(edit_quiet_start, 0, 23, direction)
    elif ui.state == STATE_SETTINGS_QUIET_END:
        edit_quiet_end = _wrap(edit_quiet_end, 0, 23, direction)
    elif ui.state == STATE_SETTINGS_RTC_CORR:
        edit_rtc_corr = _wrap(edit_rtc_corr, -30, 30, direction)


rotary_volume.add_handler(on_volume)
rotary_timer.add_handler(on_timer)


def toggle_sound():
    global sound_enabled
    sound_enabled = not sound_enabled
    if not sound_enabled:
        audio.clear(pause=True)
    show_overlay("sound", sound_enabled)
    print("Sound:", sound_enabled)


def start_current_timer():
    clear_overlay()
    timer.start()
    ui.set_state(STATE_TIMER_RUNNING)
    mark_input()
    print("Timer started:", timer.get_hms())


def cancel_timer():
    timer.cancel()
    ui.set_state(STATE_CLOCK)
    if sound_enabled:
        speech.phrase(PHRASE_TIMER_CANCELLED)
    print("Timer cancelled")


def save_time_to_rtc():
    now = rtc_now()
    write_rtc(
        now["year"], now["month"], now["day"],
        edit_time_h, edit_time_m, edit_time_s,
    )
    print(
        "RTC time set:",
        "%02d:%02d:%02d" % (edit_time_h, edit_time_m, edit_time_s),
    )


def save_date_to_rtc():
    now = rtc_now()
    write_rtc(
        edit_date_y, edit_date_m, edit_date_d,
        now["hour"], now["minute"], now["second"],
    )
    print(
        "RTC date set:",
        "%02d-%02d-%04d" % (edit_date_d, edit_date_m, edit_date_y),
    )


def enter_selected_setting():
    global edit_time_h, edit_time_m, edit_time_s
    global edit_date_d, edit_date_m, edit_date_y
    global edit_quiet_enabled, edit_quiet_start, edit_quiet_end, edit_rtc_corr

    now = rtc_now()
    item = settings_items[settings_index]
    if item == "Language":
        ui.set_state(STATE_SETTINGS_LANGUAGE)
    elif item == "Time":
        edit_time_h = now["hour"]
        edit_time_m = now["minute"]
        edit_time_s = now["second"]
        ui.set_state(STATE_SETTINGS_TIME_H)
    elif item == "Date":
        edit_date_d = now["day"]
        edit_date_m = now["month"]
        edit_date_y = now["year"]
        ui.set_state(STATE_SETTINGS_DATE_D)
    elif item == "Quiet mode":
        edit_quiet_enabled = config["quiet_enabled"]
        edit_quiet_start = config["quiet_start"]
        edit_quiet_end = config["quiet_end"]
        ui.set_state(STATE_SETTINGS_QUIET_ENABLED)
    elif item == "RTC correction":
        edit_rtc_corr = config["rtc_correction_sec_per_day"]
        ui.set_state(STATE_SETTINGS_RTC_CORR)
    else:
        show_overlay("message", ("ALARMS", "COMING SOON"))


def timer_button():
    if timer.running:
        cancel_timer()
        return

    if ui.state in (STATE_CLOCK, STATE_QUICK_TIMER):
        start_current_timer()
    elif ui.state == STATE_TIMER_EDIT_H:
        ui.set_state(STATE_TIMER_EDIT_M)
    elif ui.state == STATE_TIMER_EDIT_M:
        ui.set_state(STATE_TIMER_EDIT_S)
    elif ui.state == STATE_TIMER_EDIT_S:
        timer.set_duration(edit_h, edit_m, edit_s)
        start_current_timer()
    elif ui.state == STATE_SETTINGS:
        enter_selected_setting()
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        save_config(config)
        speech.set_language(config["language"])
        ui.set_state(STATE_SETTINGS)
    elif ui.state == STATE_SETTINGS_TIME_H:
        ui.set_state(STATE_SETTINGS_TIME_M)
    elif ui.state == STATE_SETTINGS_TIME_M:
        ui.set_state(STATE_SETTINGS_TIME_S)
    elif ui.state == STATE_SETTINGS_TIME_S:
        save_time_to_rtc()
        ui.set_state(STATE_SETTINGS)
        show_overlay("message", ("TIME", "SAVED"))
    elif ui.state == STATE_SETTINGS_DATE_D:
        ui.set_state(STATE_SETTINGS_DATE_M)
    elif ui.state == STATE_SETTINGS_DATE_M:
        ui.set_state(STATE_SETTINGS_DATE_Y)
    elif ui.state == STATE_SETTINGS_DATE_Y:
        save_date_to_rtc()
        ui.set_state(STATE_SETTINGS)
        show_overlay("message", ("DATE", "SAVED"))
    elif ui.state == STATE_SETTINGS_QUIET_ENABLED:
        ui.set_state(STATE_SETTINGS_QUIET_START)
    elif ui.state == STATE_SETTINGS_QUIET_START:
        ui.set_state(STATE_SETTINGS_QUIET_END)
    elif ui.state == STATE_SETTINGS_QUIET_END:
        config["quiet_enabled"] = edit_quiet_enabled
        config["quiet_start"] = edit_quiet_start
        config["quiet_end"] = edit_quiet_end
        save_config(config)
        ui.set_state(STATE_SETTINGS)
        show_overlay("message", ("QUIET MODE", "SAVED"))
    elif ui.state == STATE_SETTINGS_RTC_CORR:
        config["rtc_correction_sec_per_day"] = edit_rtc_corr
        save_config(config)
        ui.set_state(STATE_SETTINGS)
        show_overlay("message", ("RTC CORR", "SAVED"))
    elif ui.state == STATE_TIMER_FINISHED:
        ui.set_state(STATE_CLOCK)

    mark_input()


def timer_mode_button():
    global edit_h, edit_m, edit_s
    if timer.running:
        return
    clear_overlay()
    if ui.state in (STATE_CLOCK, STATE_QUICK_TIMER):
        edit_h, edit_m, edit_s = timer.get_hms()
        ui.set_state(STATE_TIMER_EDIT_H)
    elif ui.state in (
        STATE_TIMER_EDIT_H, STATE_TIMER_EDIT_M, STATE_TIMER_EDIT_S
    ):
        ui.set_state(STATE_CLOCK)
    mark_input()


def alarm_button():
    show_overlay("message", ("ALARMS", "COMING SOON"))
    print("Alarm menu: not implemented yet")


def mode_button():
    if timer.running:
        return
    config["clock_mode"] = (
        "chime" if config["clock_mode"] == "voice" else "voice"
    )
    save_config(config)
    show_overlay("mode", config["clock_mode"])
    print("Clock mode:", config["clock_mode"])


def minus_button():
    clear_overlay()
    if timer.running:
        return
    if ui.state == STATE_SETTINGS:
        ui.set_state(STATE_CLOCK)
    elif ui.state in (
        STATE_SETTINGS_LANGUAGE,
        STATE_SETTINGS_TIME_H, STATE_SETTINGS_TIME_M, STATE_SETTINGS_TIME_S,
        STATE_SETTINGS_DATE_D, STATE_SETTINGS_DATE_M, STATE_SETTINGS_DATE_Y,
        STATE_SETTINGS_QUIET_ENABLED,
        STATE_SETTINGS_QUIET_START,
        STATE_SETTINGS_QUIET_END,
        STATE_SETTINGS_RTC_CORR,
    ):
        ui.set_state(STATE_SETTINGS)
    elif ui.state in (
        STATE_TIMER_EDIT_H,
        STATE_TIMER_EDIT_M,
        STATE_TIMER_EDIT_S,
        STATE_QUICK_TIMER,
    ):
        ui.set_state(STATE_CLOCK)
    mark_input()


def settings_button():
    if timer.running:
        return
    clear_overlay()
    if ui.state in (STATE_CLOCK, STATE_QUICK_TIMER):
        ui.set_state(STATE_SETTINGS)
    elif ui.state == STATE_SETTINGS:
        enter_selected_setting()
    else:
        timer_button()
    mark_input()


# Physical front-panel mapping:
# GP20 - VOLUME encoder push / ON-OFF
# GP19 - TIMER encoder push / start-stop-confirm
# GP28 - Timer 1/2 / exact HH:MM:SS setup
# GP21 - Alarm
# GP22 - ST/MO
# GP26 - Preset/Search '-' / Back
# GP27 - Preset/Setup '+' / Setup-enter
btn_volume = Button(20)
btn_volume.when_pressed = toggle_sound
btn_timer = Button(19)
btn_timer.when_pressed = timer_button
btn_timer_mode = Button(28)
btn_timer_mode.when_pressed = timer_mode_button
btn_alarm = Button(21)
btn_alarm.when_pressed = alarm_button
btn_st_mo = Button(22)
btn_st_mo.when_pressed = mode_button
btn_minus = Button(26)
btn_minus.when_pressed = minus_button
btn_setup_plus = Button(27)
btn_setup_plus.when_pressed = settings_button


def service_timer():
    global timer_finished_until_ms

    if (
        ui.state == STATE_QUICK_TIMER
        and time.ticks_diff(quick_until_ms, time.ticks_ms()) <= 0
    ):
        ui.set_state(STATE_CLOCK)

    if (
        ui.state == STATE_TIMER_FINISHED
        and timer_finished_until_ms
        and time.ticks_diff(timer_finished_until_ms, time.ticks_ms()) <= 0
    ):
        timer_finished_until_ms = 0
        ui.set_state(STATE_CLOCK)

    timer.service()
    if timer.consume_finished():
        ui.set_state(STATE_TIMER_FINISHED)
        timer_finished_until_ms = time.ticks_add(
            time.ticks_ms(), TIMER_FINISHED_TIMEOUT_MS
        )
        print("Timer finished")
        if sound_enabled:
            speech.phrase(PHRASE_TIMER_FINISHED)
            speech.phrase(PHRASE_TIMER_SIGNAL_LONG)


def service_ui_timeout():
    if ui.state >= STATE_SETTINGS:
        if time.ticks_diff(time.ticks_ms(), last_input_ms) >= SETTINGS_TIMEOUT_MS:
            clear_overlay()
            ui.set_state(STATE_CLOCK)


def service_clock_auto(now):
    global last_auto_key
    if not sound_enabled or quiet_now(now):
        return
    if now["minute"] not in (0, 30):
        return
    if now["minute"] == 30 and not config["half_hour_enabled"]:
        return

    key = (
        now["year"], now["month"], now["day"],
        now["hour"], now["minute"],
    )
    if key == last_auto_key:
        return

    if config["clock_mode"] == "voice":
        speech.say_time(now["hour"], now["minute"])
    else:
        if now["minute"] == 0:
            strikes = now["hour"] % 12
            if strikes == 0:
                strikes = 12
            audio.enqueue(FOLDER_CHIMES, strikes)
        else:
            audio.enqueue(FOLDER_SILENCE, SILENCE_HALF_HOUR_TRACK)

    last_auto_key = key


def service_rtc_correction(now):
    global last_rtc_correction_key
    correction = int(config["rtc_correction_sec_per_day"])
    if correction == 0:
        return

    # Apply one small correction per day at 03:05.
    if now["hour"] != 3 or now["minute"] != 5 or now["second"] > 10:
        return

    key = (now["year"], now["month"], now["day"])
    if key == last_rtc_correction_key:
        return

    second = now["second"] + correction
    minute = now["minute"]
    hour = now["hour"]

    while second < 0:
        second += 60
        minute -= 1
    while second >= 60:
        second -= 60
        minute += 1
    while minute < 0:
        minute += 60
        hour -= 1
    while minute >= 60:
        minute -= 60
        hour += 1

    write_rtc(
        now["year"], now["month"], now["day"],
        hour, minute, second,
    )
    last_rtc_correction_key = key
    print("RTC correction applied:", correction, "sec")


def service_display(now):
    edit_states = (
        STATE_TIMER_EDIT_H, STATE_TIMER_EDIT_M, STATE_TIMER_EDIT_S,
        STATE_SETTINGS_LANGUAGE,
        STATE_SETTINGS_TIME_H, STATE_SETTINGS_TIME_M, STATE_SETTINGS_TIME_S,
        STATE_SETTINGS_DATE_D, STATE_SETTINGS_DATE_M, STATE_SETTINGS_DATE_Y,
        STATE_SETTINGS_QUIET_ENABLED,
        STATE_SETTINGS_QUIET_START,
        STATE_SETTINGS_QUIET_END,
        STATE_SETTINGS_RTC_CORR,
    )

    if (
        overlay_active()
        and ui.state != STATE_TIMER_RUNNING
        and ui.state not in edit_states
    ):
        if overlay_kind == "volume":
            ui.show_volume(overlay_value)
        elif overlay_kind == "sound":
            ui.show_sound(overlay_value)
        elif overlay_kind == "mode":
            ui.show_clock_mode(overlay_value)
        elif overlay_kind == "message":
            ui.show_message(overlay_value[0], overlay_value[1])
        return
    elif overlay_kind is not None and not overlay_active():
        clear_overlay()

    if ui.state == STATE_CLOCK:
        ui.show_clock(
            now,
            sound_enabled,
            config["clock_mode"],
            quiet_now(now),
        )
    elif ui.state == STATE_QUICK_TIMER:
        h, m, s = timer.get_hms()
        quick_now = dict(now)
        quick_now["clock_mode"] = config["clock_mode"]
        ui.show_quick_timer(quick_now, h, m, s)
    elif ui.state == STATE_TIMER_RUNNING:
        h, m, s = timer.remaining_hms()
        ui.show_timer_running(h, m, s)
    elif ui.state == STATE_TIMER_FINISHED:
        ui.show_timer_finished()
    elif ui.state == STATE_TIMER_EDIT_H:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "h")
    elif ui.state == STATE_TIMER_EDIT_M:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "m")
    elif ui.state == STATE_TIMER_EDIT_S:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "s")
    elif ui.state == STATE_SETTINGS:
        ui.show_settings(
            settings_index,
            len(settings_items),
            settings_items[settings_index],
        )
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        ui.show_language(config["language"])
    elif ui.state == STATE_SETTINGS_TIME_H:
        ui.show_set_time(edit_time_h, edit_time_m, edit_time_s, "h")
    elif ui.state == STATE_SETTINGS_TIME_M:
        ui.show_set_time(edit_time_h, edit_time_m, edit_time_s, "m")
    elif ui.state == STATE_SETTINGS_TIME_S:
        ui.show_set_time(edit_time_h, edit_time_m, edit_time_s, "s")
    elif ui.state == STATE_SETTINGS_DATE_D:
        ui.show_set_date(edit_date_d, edit_date_m, edit_date_y, "d")
    elif ui.state == STATE_SETTINGS_DATE_M:
        ui.show_set_date(edit_date_d, edit_date_m, edit_date_y, "m")
    elif ui.state == STATE_SETTINGS_DATE_Y:
        ui.show_set_date(edit_date_d, edit_date_m, edit_date_y, "y")
    elif ui.state == STATE_SETTINGS_QUIET_ENABLED:
        ui.show_quiet_enabled(edit_quiet_enabled)
    elif ui.state == STATE_SETTINGS_QUIET_START:
        ui.show_quiet_time(True, edit_quiet_start)
    elif ui.state == STATE_SETTINGS_QUIET_END:
        ui.show_quiet_time(False, edit_quiet_end)
    elif ui.state == STATE_SETTINGS_RTC_CORR:
        ui.show_rtc_correction(edit_rtc_corr)


print("Speaking Timer-Clock v%s starting" % APP_VERSION)
print(
    "Language:", config["language"],
    "Volume:", volume,
    "Clock mode:", config["clock_mode"],
    "Quiet:", config["quiet_enabled"],
    config["quiet_start"], "-", config["quiet_end"],
    "RTC correction:", config["rtc_correction_sec_per_day"],
)

while True:
    now = rtc_now()
    service_timer()
    service_ui_timeout()
    service_clock_auto(now)
    service_rtc_correction(now)
    audio.service()
    service_display(now)
    time.sleep_ms(20)
