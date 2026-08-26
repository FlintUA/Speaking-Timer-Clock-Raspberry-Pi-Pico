# Speaking Timer-Clock v3 - modular hardware build
# MicroPython / Raspberry Pi Pico

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
    PHRASE_TIMER_FINISHED,
    PHRASE_TIMER_SIGNAL_LONG,
    PHRASE_TIMER_CANCELLED,
)
from ui import (
    ClockUI,
    STATE_CLOCK,
    STATE_TIMER_EDIT_H,
    STATE_TIMER_EDIT_M,
    STATE_TIMER_EDIT_S,
    STATE_TIMER_RUNNING,
    STATE_SETTINGS,
    STATE_SETTINGS_LANGUAGE,
)


# ----------------------------- hardware -----------------------------

I2C_ADDR = 0x3F
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


# ----------------------------- application state -----------------------------

config = load_config()
speech = Speech(audio, config["language"])
timer = CountdownTimer()
ui = ClockUI(lcd)

sound_enabled = True
volume = config["volume"]
audio.set_volume(volume)

edit_h, edit_m, edit_s = timer.get_hms()
last_auto_key = None


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


# ----------------------------- encoder callbacks -----------------------------

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
    print("Volume:", volume)


def _wrap(value, minimum, maximum, delta):
    value += delta
    if value > maximum:
        return minimum
    if value < minimum:
        return maximum
    return value


def on_timer(event):
    global edit_h, edit_m, edit_s
    delta = 1 if event == Rotary.ROT_CW else -1

    if ui.state == STATE_CLOCK:
        # Quick timer adjustment in whole minutes.
        h, m, s = timer.get_hms()
        total = h * 60 + m
        total = max(1, min(240, total + delta))
        timer.set_duration(minutes=total)
        edit_h, edit_m, edit_s = timer.get_hms()
        print("Quick timer:", "%02d:%02d:%02d" % (edit_h, edit_m, edit_s))
        return

    if ui.state == STATE_TIMER_EDIT_H:
        edit_h = _wrap(edit_h, 0, 4, delta)
    elif ui.state == STATE_TIMER_EDIT_M:
        edit_m = _wrap(edit_m, 0, 59, delta)
    elif ui.state == STATE_TIMER_EDIT_S:
        edit_s = _wrap(edit_s, 0, 59, delta)
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        config["language"] = "de" if config["language"] == "ru" else "ru"
        speech.set_language(config["language"])


rotary_volume.add_handler(on_volume)
rotary_timer.add_handler(on_timer)


# ----------------------------- buttons -----------------------------

def toggle_sound():
    global sound_enabled
    sound_enabled = not sound_enabled
    if not sound_enabled:
        audio.clear(pause=True)
    print("Sound:", sound_enabled)


def timer_button():
    global edit_h, edit_m, edit_s

    if timer.running:
        timer.cancel()
        ui.set_state(STATE_CLOCK)
        if sound_enabled:
            speech.phrase(PHRASE_TIMER_CANCELLED)
        print("Timer cancelled")
        return

    if ui.state in (STATE_TIMER_EDIT_H, STATE_TIMER_EDIT_M, STATE_TIMER_EDIT_S):
        if ui.state == STATE_TIMER_EDIT_H:
            ui.set_state(STATE_TIMER_EDIT_M)
        elif ui.state == STATE_TIMER_EDIT_M:
            ui.set_state(STATE_TIMER_EDIT_S)
        else:
            timer.set_duration(edit_h, edit_m, edit_s)
            if timer.duration_seconds > 0:
                timer.start()
                ui.set_state(STATE_TIMER_RUNNING)
                print("Timer started exact:", "%02d:%02d:%02d" % (edit_h, edit_m, edit_s))
        return

    timer.start()
    ui.set_state(STATE_TIMER_RUNNING)
    print("Timer started quick:", timer.get_hms())


def timer_mode_button():
    """Physical Timer 1/2 button: enter exact HH:MM:SS setup for the one timer."""
    global edit_h, edit_m, edit_s
    if timer.running:
        return
    if ui.state == STATE_CLOCK:
        edit_h, edit_m, edit_s = timer.get_hms()
        ui.set_state(STATE_TIMER_EDIT_H)
    else:
        ui.set_state(STATE_CLOCK)


def alarm_button():
    # Alarm editor is the next v3 stage. Keep the physical button reserved.
    print("Alarm menu: not implemented yet")


def mode_button():
    """Physical ST/MO button: exclusive strike/voice clock mode."""
    config["clock_mode"] = "chime" if config["clock_mode"] == "voice" else "voice"
    save_config(config)
    print("Clock mode:", config["clock_mode"])


def minus_button():
    # Physical '-' button acts as Back while menu work is being built.
    if ui.state != STATE_CLOCK:
        ui.set_state(STATE_CLOCK)


def settings_button():
    # Physical Setup/+ button.
    if timer.running:
        return
    if ui.state == STATE_CLOCK:
        ui.set_state(STATE_SETTINGS)
    elif ui.state == STATE_SETTINGS:
        ui.set_state(STATE_SETTINGS_LANGUAGE)
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        save_config(config)
        speech.set_language(config["language"])
        ui.set_state(STATE_CLOCK)
        print("Config saved; language:", config["language"])
    else:
        ui.set_state(STATE_CLOCK)


# Physical front-panel mapping:
# GP20 - VOLUME encoder push / ON-OFF
# GP19 - TIMER encoder push / start-stop-confirm
# GP28 - Timer 1/2
# GP21 - Alarm
# GP22 - ST/MO
# GP26 - Preset/Search '-'
# GP27 - Preset/Setup '+'
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


# ----------------------------- automatic services -----------------------------

def service_timer():
    timer.service()
    if timer.consume_finished():
        ui.set_state(STATE_CLOCK)
        print("Timer finished")
        # Timers are intentional user alarms and are NOT suppressed by quiet mode.
        if sound_enabled:
            speech.phrase(PHRASE_TIMER_FINISHED)
            speech.phrase(PHRASE_TIMER_SIGNAL_LONG)


def service_clock_auto(now):
    """Automatic clock output: voice OR chime, never both.

    Quiet mode suppresses all automatic voice/chime events. It does not suppress
    countdown timers or, later, explicit alarm-clock events.
    """
    global last_auto_key

    if not sound_enabled or quiet_now(now):
        return

    if now["minute"] not in (0, 30):
        return
    if now["minute"] == 30 and not config["half_hour_enabled"]:
        return

    key = (now["year"], now["month"], now["day"], now["hour"], now["minute"])
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
            # Half-hour convention: one strike.
            audio.enqueue(FOLDER_CHIMES, 1)

    last_auto_key = key


def service_display(now):
    if ui.state == STATE_CLOCK:
        ui.show_clock(
            now,
            config["language"],
            sound_enabled,
            config["clock_mode"],
        )
    elif ui.state == STATE_TIMER_RUNNING:
        h, m, s = timer.remaining_hms()
        ui.show_timer_running(h, m, s)
    elif ui.state == STATE_TIMER_EDIT_H:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "h")
    elif ui.state == STATE_TIMER_EDIT_M:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "m")
    elif ui.state == STATE_TIMER_EDIT_S:
        ui.show_timer_edit(edit_h, edit_m, edit_s, "s")
    elif ui.state == STATE_SETTINGS:
        ui.show_settings()
    elif ui.state == STATE_SETTINGS_LANGUAGE:
        ui.show_language(config["language"])


print("Speaking Timer-Clock v3 starting")
print(
    "Language:", config["language"],
    "Volume:", volume,
    "Clock mode:", config["clock_mode"],
    "Quiet:", config["quiet_enabled"], config["quiet_start"], "-", config["quiet_end"],
)

while True:
    now = rtc_now()
    service_timer()
    service_clock_auto(now)
    audio.service()
    service_display(now)
    time.sleep_ms(20)
