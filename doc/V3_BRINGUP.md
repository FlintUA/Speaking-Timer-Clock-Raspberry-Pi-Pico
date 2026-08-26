# Speaking Timer-Clock v3 - first hardware bring-up

This build is intentionally smaller than the final v3. Its purpose is to validate the new architecture on the already proven hardware before adding alarms, RTC editing/correction, music and the complete settings menu.

## Files

Keep the proven hardware libraries on Pico in `/lib`:

- `ds1302.py`
- `lcd_api.py`
- `pico_i2c_lcd.py`
- `picozero.py`
- `rotary.py`

For this first test the old blocking `picodfplayer.py` is not used by v3.

Copy these v3 files to the Pico root:

- `source/v3/config.py` -> `/config.py`
- `source/v3/timer_engine.py` -> `/timer_engine.py`
- `source/v3/audio.py` -> `/audio.py`
- `source/v3/ui.py` -> `/ui.py`
- `source/v3/main_v3.py` -> `/main_v3.py`

Run `/main_v3.py` manually from Thonny. Do not rename it to `main.py` yet.

## First-build controls

| Control | v3 first-build action |
| --- | --- |
| Left encoder rotate | Volume 0-30 |
| Left encoder press | Sound mute/unmute |
| Right encoder rotate on clock screen | Quick timer duration in 1-minute steps |
| Right encoder press on clock screen | Start quick timer |
| Right encoder press while timer runs | Cancel timer |
| Button GP21 | Speak current time in selected language only |
| Button GP22 | Settings -> Language -> Save/exit |
| Button GP26 | Enter exact timer editor |
| Right encoder in exact editor | Change selected HH/MM/SS field |
| Right encoder press in exact editor | HH -> MM -> SS -> start |
| GP27 | reserved for later v3 function |
| GP28 | reserved for later v3 function |

## Exact timer test

1. Press GP26.
2. LCD should show `SET TIMER HOUR`.
3. Rotate right encoder to set hours.
4. Press right encoder - field changes to minutes.
5. Rotate to set minutes.
6. Press - field changes to seconds.
7. Set e.g. 10 seconds.
8. Press - countdown starts.
9. LCD should update every second and reach `00:00:00` close to ten seconds later.
10. Timer completion should enqueue the selected-language `timer finished` phrase and long timer signal without blocking the LCD/main loop.

## Language test

1. From clock screen press GP22 - `SETTINGS`.
2. Press GP22 again - `LANGUAGE`.
3. Rotate right encoder - RU/DE changes.
4. Press GP22 - setting is saved to `/config.json` and clock screen returns.
5. Press GP21 - only the selected language should speak.
6. Reboot Pico - selected language should remain stored.

## What v3 already changes architecturally

- countdown resolution is one second, range 1 second to 4 hours;
- application timer uses `ticks_ms()` and is independent of RTC minute boundaries;
- only one active language is selected (`ru` or `de`);
- configuration is persistent in `/config.json`;
- DFPlayer commands no longer contain a built-in 500 ms blocking sleep;
- speech uses an audio queue and DFPlayer BUSY state;
- UI uses explicit application states instead of many loosely related flags;
- the old v2 remains available as the hardware reference build.

## Deliberately not implemented in this first build

- setting RTC time/date from the menu;
- RTC drift correction;
- editable alarms 1-5;
- birthday/date events;
- complete music player controls;
- chime settings;
- full quiet-mode settings UI;
- persistent volume save on every encoder movement (it is saved when settings are saved; later a deferred-save mechanism will be added).

These are subsequent v3 layers, not omissions from the final design.
