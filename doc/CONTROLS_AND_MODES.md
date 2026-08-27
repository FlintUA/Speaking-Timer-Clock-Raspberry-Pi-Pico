# Speaking Timer-Clock v3 - Controls and Operating Modes

This document describes the current front-panel controls, operating modes, and user-facing behavior of the `develop/v3` firmware.

Current documented firmware baseline: **v3.2.0**.

> This file is intended to be maintained together with the firmware. When button behavior, menu structure, audio behavior, or operating modes change, this document should be updated in the same development cycle.

## 1. Front-panel controls

### Left encoder - Volume

Physical label: `- VOLUME +`

- Rotate clockwise - increase volume.
- Rotate counter-clockwise - decrease volume.
- Range - `0..30`.
- Push - global sound ON/OFF.
- When volume changes, the LCD temporarily shows the current volume.

GPIO:

- A/DT - GP14
- B/CLK - GP15
- Push - GP20

### Right encoder - Timer / menu / value editing

Physical label: `- TIMER +`

Its function depends on the current screen.

- Main clock screen - quick countdown timer adjustment in minutes.
- Timer edit - changes hours, minutes, or seconds.
- Settings - moves through menu items or changes the selected value.
- Alarm menu - selects Alarm 1..5 and edits ON/OFF, hour, and minute.
- Push - start/stop/confirm depending on context.

GPIO:

- A/DT - GP11
- B/CLK - GP10
- Push - GP19

### Button GP28 - Timer 1/2

The physical `Timer 1/2` label is reused for the single precise countdown timer.

- From the normal clock screen - opens exact timer setup.
- Exact setup order - hours -> minutes -> seconds.
- Right encoder changes the selected value.
- Right encoder push confirms the field and advances to the next field.
- After confirming seconds, the timer starts.

There is only **one countdown timer** in v3.

### Button GP21 - Alarm

- Opens the alarm list.
- There are 5 daily alarms.
- While an alarm is ringing, pressing this button stops the active alarm.

### Button GP22 - ST/MO

Switches the automatic clock audio mode.

- `MO` - spoken time.
- `ST` - clock strikes / chime mode.

The two modes are mutually exclusive for automatic clock output.

### Button GP26 - Minus / Back

Physical labels include `Preset/Search -` depending on the original device function.

Current v3 use:

- Back from settings and edit screens.
- Exit quick timer selection without starting it.
- Return toward the normal clock display.

### Button GP27 - Setup / Plus

- From the normal clock screen - opens Settings.
- Inside Settings - enters the selected menu item.
- While editing a setting - acts as confirm/next in the same way as the right encoder push where implemented.

## 2. Main clock screen

Typical display:

```text
21:15:32 MO S
27-Aug-2026 Do
```

Indicators:

- `MO` - spoken clock mode.
- `ST` - strike/chime clock mode.
- `S` - sound enabled and not currently suppressed by quiet mode.
- `M` - global sound muted.
- `N` - quiet/night mode currently suppresses automatic clock audio.

The second row shows date and weekday.

## 3. Quick countdown timer

Rotate the right encoder from the main clock screen.

Typical display:

```text
21:15:32 MO
TIMER 00:10:00
```

Behavior:

- Slow rotation changes the timer in small minute steps.
- Faster rotation uses acceleration for larger steps.
- Current quick-timer range is up to 4 hours.
- After a short inactivity timeout the display returns to the main clock if the timer was not started.
- Push the right encoder to start the selected countdown.

## 4. Exact countdown timer

Press `Timer 1/2`.

Typical screen:

```text
SET TIMER
00:00:10
```

Editing sequence:

1. Hours
2. Minutes
3. Seconds
4. Start

The selected field is indicated by the LCD cursor.

During countdown:

```text
TIMER RUNNING
00:00:09
```

At completion:

```text
TIMER FINISHED
00:00:00
```

The finished screen remains visible for approximately **7 seconds**, then the LCD automatically returns to the clock/date screen.

The timer is intended to remain audible even during quiet/night mode.

## 5. Settings menu

Open using the `Setup/+` button.

Current menu items:

1. Language
2. Time
3. Date
4. Quiet mode
5. RTC correction
6. Alarms

The right encoder selects items and changes values.

## 6. Language

Supported UI/audio language modes:

- Russian - `ru`
- German - `de`

The selected language controls the speech/service audio folders used by DFPlayer.

## 7. Time setting

Settings -> Time.

Editing order:

1. Hours
2. Minutes
3. Seconds

After confirmation, the new value is written to the DS1302 RTC.

## 8. Date setting

Settings -> Date.

Editing order:

1. Day
2. Month
3. Year

The weekday is calculated automatically from the selected calendar date and written to the DS1302.

Date setting has been hardware-tested and is working in the current build.

## 9. Quiet / night mode

Default configuration:

- Enabled
- Start - 22:00
- End - 07:00

Quiet mode suppresses only automatic clock output such as:

- MO spoken time
- ST strikes/chimes

Quiet mode does **not** suppress:

- countdown timer completion
- daily alarms

This distinction is intentional.

## 10. RTC correction

Settings -> RTC correction.

The setting is expressed in seconds per day.

Current allowed range:

- `-30 .. +30 sec/day`

This feature is intended for compensating a systematic DS1302 drift.

Long-term calibration should be tested over multiple days before relying on a correction value.

## 11. ST / MO automatic clock modes

### MO - spoken time

The clock automatically queues spoken time according to the current clock schedule when quiet mode does not suppress it.

Russian folders:

- 01 - hours
- 02 - minutes

German folders:

- 11 - hours
- 12 - minutes

### ST - strikes / chimes

Full hours use folder `18`.

- Tracks 1..12 correspond to the number of strikes.

Half-hour behavior currently uses a technical silent placeholder:

- Folder `19`
- Track `001`

A dedicated half-hour sound can be substituted later.

## 12. Daily alarms

Five independent daily alarms are available.

Each alarm stores:

- enabled / disabled
- hour
- minute

Typical list screen:

```text
ALARM 1 ON
07:30 DAILY
```

Editing sequence:

1. Select Alarm 1..5
2. ON/OFF
3. Hour
4. Minute
5. Save

Alarm settings are stored in `/config.json` and survive Pico restart.

### Alarm audio

Current alarm behavior uses the language-specific service phrase track for the corresponding alarm number.

Russian service folder:

- `07/001` - Alarm 1
- `07/002` - Alarm 2
- `07/003` - Alarm 3
- `07/004` - Alarm 4
- `07/005` - Alarm 5

German service folder:

- `17/001` - Alarm 1
- `17/002` - Alarm 2
- `17/003` - Alarm 3
- `17/004` - Alarm 4
- `17/005` - Alarm 5

At the moment this produces the available alarm speech/service audio only. A separate music or repeating alarm signal after the spoken phrase is **not yet implemented**.

When an alarm is active, it can be stopped using:

- the Alarm button
- the right encoder push

The active alarm screen also has a safety timeout so it cannot remain permanently on the LCD.

Quiet mode does not block alarms.

## 13. Global sound ON/OFF

Push the left volume encoder.

- ON - normal audio operation.
- OFF - global audio mute.

The LCD briefly displays the sound state.

Note: interaction between mute and an already-playing DFPlayer track still deserves additional hardware testing.

## 14. Audio folder map used by v3

Primary numeric DFPlayer folder structure:

- 01 - Russian hours
- 02 - Russian minutes
- 03 - Russian day/date
- 04 - Russian months
- 05 - Russian years 2023..2029
- 06 - Russian weekdays
- 07 - Russian service phrases / alarms / timer phrases
- 08 - music
- 09 - Russian generic numbers
- 10 - reserved
- 11 - German hours
- 12 - German minutes
- 13 - German day/date
- 14 - German months
- 15 - German years 2023..2029
- 16 - German weekdays
- 17 - German service phrases / alarms / timer phrases
- 18 - strikes / chimes
- 19 - silent technical tracks
- 20 - reserved
- 21 - reserved

## 15. Current known incomplete features

The following areas are not yet considered complete:

- separate alarm music / repeating alarm tone after the spoken alarm phrase
- music player mode using folder 08
- final half-hour ST sound
- audio priority rules between alarm, timer, clock speech/chime, and future music
- persistent/deferred volume saving strategy
- long-term RTC correction validation
- final verification of sound-off behavior during an already-playing track
- birthday function / birthday melody

## 16. Firmware versioning

`main_v3.py` contains an explicit application version and prints it on startup.

Example:

```text
Speaking Timer-Clock v3.2.0 starting
```

For hardware testing, always verify this line in the REPL before diagnosing behavior.

## 17. Hardware pin summary

- LCD SDA - GP0
- LCD SCL - GP1
- DS1302 CLK - GP2
- DS1302 DAT - GP5
- DS1302 CE/RST - GP4
- DFPlayer TX - GP16
- DFPlayer RX - GP17
- DFPlayer BUSY - GP18
- Volume encoder A/DT - GP14
- Volume encoder B/CLK - GP15
- Volume encoder push - GP20
- Timer encoder A/DT - GP11
- Timer encoder B/CLK - GP10
- Timer encoder push - GP19
- Timer 1/2 button - GP28
- Alarm button - GP21
- ST/MO button - GP22
- Minus/Back button - GP26
- Setup/Plus button - GP27

---

Last documented state: v3.2.0, hardware-tested core clock/timer/date/time/alarm navigation working. This document should be updated whenever user-visible behavior changes.
