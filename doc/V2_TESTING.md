# Speaking Timer-Clock v2 - bring-up and test plan

This document is for the `fix/working-v2` branch. The original `source/speaking_timer_clock.py` is intentionally preserved as the legacy version.

## Goal

Bring the project up one subsystem at a time instead of debugging the complete clock at once.

## Files to copy to Raspberry Pi Pico

Copy these files to the Pico filesystem root:

- `library/ds1302.py`
- `library/lcd_api.py`
- `library/pico_i2c_lcd.py`
- `library/picodfplayer.py`
- `library/picozero.py`
- `library/rotary.py`
- `source/speaking_timer_clock_v2.py`

For automatic startup, copy or rename `source/speaking_timer_clock_v2.py` to `/main.py` only after the individual hardware tests below pass.

## Expected pin mapping

| Function | Pico GPIO |
| --- | ---: |
| LCD SDA | GP0 |
| LCD SCL | GP1 |
| DS1302 CLK | GP2 |
| DS1302 RST/CE | GP4 |
| DS1302 DAT | GP5 |
| Timer encoder B/CLK | GP10 |
| Timer encoder A/DT | GP11 |
| Volume encoder A/DT | GP14 |
| Volume encoder B/CLK | GP15 |
| DFPlayer TX | GP16 |
| DFPlayer RX | GP17 |
| DFPlayer BUSY | GP18 |
| Timer encoder button | GP19 |
| Volume encoder button | GP20 |
| Button 2 | GP21 |
| Button 3 | GP22 |
| Button 4 | GP26 |
| Button 5 | GP27 |
| Button 1 | GP28 |

## 1. Check MicroPython

Use a current stable MicroPython build for Raspberry Pi Pico (RP2040). Open the REPL and confirm:

```python
import sys
print(sys.implementation)
```

## 2. Check LCD before running the clock

```python
from machine import Pin, I2C
from pico_i2c_lcd import I2cLcd

i2c = I2C(0, sda=Pin(0), scl=Pin(1), freq=400000)
print(i2c.scan())
lcd = I2cLcd(i2c, 0x3F, 2, 16)
lcd.putstr("LCD OK")
```

Expected result: `i2c.scan()` contains decimal `63` (`0x3F`).

If the backpack is `0x27`, change `I2C_ADDR` in v2 accordingly.

## 3. Check DS1302

```python
from machine import Pin
from ds1302 import DS1302

ds = DS1302(Pin(2), Pin(5), Pin(4))
print(ds.date_time())
```

The list must contain changing seconds. Example format:

```text
[2026, 8, 26, 3, 15, 30, 12]
```

If the time must be set, run once with the correct values:

```python
ds.date_time([2026, 8, 26, 3, 15, 30, 0])
```

Then comment/remove the setter again.

## 4. Check DFPlayer independently

The microSD structure in the repository uses numbered folders and numbered files such as `01/000.mp3`, `01/001.mp3`, etc.

```python
from picodfplayer import DFPlayer
from time import sleep

p = DFPlayer(0, 16, 17, 18)
p.setVolume(10)
sleep(1)
p.playTrack(1, 12)
```

If there is no sound, check:

- common ground between Pico and DFPlayer;
- TX/RX direction;
- DFPlayer supply voltage/current;
- microSD FAT filesystem;
- exact folder/file numbering;
- speaker/amplifier connection;
- BUSY output level on GP18.

## 5. Check encoders

The v2 rotary driver deliberately does not attach an IRQ to the encoder push button. Rotation is handled by `rotary.py`; push buttons are handled separately by `picozero.Button`.

```python
from rotary import Rotary

r = Rotary(14, 15)

def changed(event):
    print(event)

r.add_handler(changed)
```

Rotate slowly in both directions. Expected event codes are `1` and `2`.

If direction is reversed, swap the two encoder phase pins in the constructor.

## 6. Run v2 manually

Do not rename it to `main.py` yet. Run `speaking_timer_clock_v2.py` from Thonny and watch the console.

Expected startup message:

```text
Speaking Timer-Clock v2 starting
```

Then verify in this order:

1. LCD displays time including seconds.
2. Seconds advance continuously.
3. Volume encoder changes values 0-30.
4. Timer encoder changes 1-240 minutes.
5. Timer button starts/cancels the timer.
6. A 1-minute timer fires close to 60 seconds, not on an arbitrary minute boundary.
7. Sound button changes the sound state.
8. Fixed alarms trigger once per matching date/time.
9. Hour/half-hour announcements do not repeat continuously for the whole minute.
10. Night interval 22:00-07:00 suppresses automatic audio.

## Important v2 changes

- Removed the missing `machine` namespace usage from the application code.
- Timer now uses `time.ticks_ms()` and therefore has second-level timing instead of comparing only RTC hours/minutes.
- Maximum timer value is 240 minutes, matching the README.
- Timer naturally works across hour and midnight boundaries.
- Daily alarm comparison uses `hour * 100 + minute`, avoiding the old string-concatenation error.
- Repeated hour/alarm triggering is blocked using date/time keys.
- Encoder switch IRQ ownership conflict is removed.
- Original implementation is preserved for comparison.

## Known uncertainties requiring hardware verification

1. The exact semantic mapping of every voice file in folders `07` and `17` is not documented well enough to guarantee that every spoken status phrase matches the action.
2. Mechanical encoder direction/debounce can vary with the installed modules.
3. Some DFPlayer Mini clones need more startup delay or behave differently with folder command `0x0F`.
4. The LCD backpack may use `0x27` instead of `0x3F` on another unit.
5. Long audio sequences are blocking in the current DFPlayer library (`COMMAND_LATENCY = 500 ms`). The clock/timer timebase remains correct, but UI responsiveness during speech can be improved later with a non-blocking audio queue.

## Next engineering step after first hardware run

Record the first exception or incorrect behavior from the REPL exactly as printed. Fix only that layer, rerun, and continue until all ten checks above pass. After that, rename the v2 application to `main.py` and update the main README.
