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

## Confirmed DFPlayer microSD layout

The numbered folders are the working hardware format and remain the v2 audio source. The newer named folders (`tts`, `music`, `chimes`, etc.) are not required by v2.

| Folder | Purpose |
| --- | --- |
| `01` | Russian hours |
| `02` | Russian minutes, `000` = exactly/on the hour |
| `03` | Russian day/date number |
| `04` | Russian month |
| `05` | Russian year: `003` = 2023 through `009` = 2029 |
| `06` | Russian weekday |
| `07` | Russian service phrases |
| `08` | Music player tracks `001-045` |
| `09` | Russian service numbers `0-30` |
| `10` | Reserved/empty |
| `11` | German hours, `000` = midnight |
| `12` | German minutes, `000` = exactly/on the hour |
| `13` | German day/date number |
| `14` | German month |
| `15` | German year: `003` = 2023 through `009` = 2029 |
| `16` | German weekday |
| `17` | German service phrases |
| `18` | Clock strikes: `000` no strike/prelude, `001-012` strike count |
| `19` | Silent technical files `001`, `002`, retained as reserve |
| `20` | Reserved/empty |
| `21` | Reserved/empty |

Confirmed service phrase numbers in both `07` and `17`:

| File | Meaning |
| ---: | --- |
| `001` | Alarm 1 triggered |
| `002` | Alarm 2 triggered |
| `003` | Alarm 3 triggered |
| `004` | Alarm 4 triggered |
| `005` | Alarm 5 triggered |
| `006` | Timer setup |
| `009` | Timer set |
| `011` | Timer triggered |
| `012` | Short timer signal |
| `013` | Longer timer signal |
| `014` | Timer cancelled |

The birthday melody exists as file `000` in the newer phrase set, but birthday/date-time event support is intentionally not implemented in the current bring-up phase.

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

If the time must be set, run once with the correct values and then remove/comment the setter again.

## 4. Check DFPlayer independently

```python
from picodfplayer import DFPlayer
from time import sleep

p = DFPlayer(0, 16, 17, 18)
p.setVolume(10)
sleep(1)
p.playTrack(1, 12)
```

Expected: Russian hour file 12 from folder `01`.

Additional useful checks:

```python
p.playTrack(7, 9)    # RU: timer set
p.playTrack(17, 9)   # DE: timer set
p.playTrack(18, 3)   # three clock strikes
p.playTrack(8, 1)    # first music track
```

If there is no sound, check common ground, TX/RX direction, DFPlayer supply, FAT microSD, numbering, speaker/amplifier connection and BUSY on GP18.

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
6. Starting a timer plays phrase `009` from RU/DE service folders.
7. Cancelling a timer plays phrase `014` from RU/DE folders.
8. A 1-minute timer fires close to 60 seconds and uses `011` plus the longer signal `013`.
9. Sound button changes the sound state without relying on unconfirmed phrase IDs.
10. Fixed alarms trigger once per matching date/time using phrases `001-005`.
11. Hour/half-hour announcements do not repeat continuously for the whole minute.
12. Night interval 22:00-07:00 suppresses automatic audio.
13. Date/time button speaks year only for 2023-2029; unsupported years are skipped rather than mapped to a wrong file.
14. Music button plays tracks from folder `08`, files `001-045`.

## Important v2 changes

- Uses the confirmed numbered DFPlayer folders `01-21`; named TTS directories are not required.
- Replaces raw audio magic numbers with named folder/phrase constants.
- Does not guess meanings for unconfirmed service phrase files.
- Spoken years are explicitly limited to available files 2023-2029.
- Timer uses `time.ticks_ms()` for second-level timing.
- Maximum timer value is 240 minutes.
- Timer works across hour and midnight boundaries.
- Daily alarm comparison uses `hour * 100 + minute`.
- Repeated hour/alarm triggering is blocked using date/time keys.
- Encoder switch IRQ ownership conflict is removed.
- Original implementation remains preserved for comparison.

## Known uncertainties requiring hardware verification

1. Mechanical encoder direction/debounce can vary with the installed modules.
2. Some DFPlayer Mini clones need more startup delay or behave differently with folder command `0x0F`.
3. The LCD backpack may use `0x27` instead of `0x3F` on another unit.
4. Long audio sequences are blocking in the current DFPlayer library (`COMMAND_LATENCY = 500 ms`). The clock/timer timebase remains correct, but UI responsiveness during speech can later be improved with an audio queue.
5. Timer durations above the available spoken-number range are currently confirmed by the generic `timer set` phrase instead of an incorrect spoken duration.

## Next engineering step after first hardware run

Record the first exception or incorrect behavior from the REPL exactly as printed. Fix only that layer, rerun, and continue until the checks above pass. After that, rename the v2 application to `main.py` and update the main README.
