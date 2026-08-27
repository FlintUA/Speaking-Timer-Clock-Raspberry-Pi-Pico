# Speaking Timer-Clock v3 - Controls and Operating Modes

This document describes the current front-panel controls, operating modes, and user-facing behavior of the `develop/v3` firmware.

Current documented firmware baseline: **v3.3.1**.

> This file is intended to be maintained together with the firmware. When button behavior, menu structure, audio behavior, or operating modes change, this document should be updated in the same development cycle.

## 1. Front-panel controls

### Left encoder - Volume

Physical label: `- VOLUME +`

- Rotate clockwise - increase volume.
- Rotate counter-clockwise - decrease volume.
- Range - `0..30`.
- Push - global sound ON/OFF.
- When volume changes, the LCD temporarily shows the current volume.
- If an alarm is ringing, push stops the alarm instead of toggling mute.

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
- Alarm menu - selects Alarm 1..5 and edits ON/OFF, hour, minute, sound type, and music track.
- Push - start/stop/confirm depending on context.
- If an alarm is ringing, push stops the alarm.

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
- On the `ALARM N MUSIC` track-selection screen, pressing this button previews the selected music track.
- Press `Alarm` again during preview to stop the preview.

### Button GP22 - ST/MO

The button now has two actions:

- Short press - switches the automatic clock audio mode between `MO` and `ST`.
- Long press, about 0.9 seconds or longer - speaks the current time once.

Automatic modes:

- `MO` - spoken time.
- `ST` - clock strikes / chime mode.

The two modes are mutually exclusive for automatic clock output.

Manual long-press speech is a direct user request, so it is allowed even during quiet/night mode. Global `SOUND OFF` still prevents playback.

### Button GP26 - Minus / Back

Physical labels include `Preset/Search -` depending on the original device function.

Current v3 use:

- Back from settings and edit screens.
- Exit quick timer selection without starting it.
- Return toward the normal clock display.
- Exit alarm editing without saving the current edit sequence.

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
- manual current-time speech requested by long-pressing `ST/MO`

This distinction is intentional.

## 10. RTC correction

Settings -> RTC correction.

The setting is expressed in seconds per day.

Current allowed range:

- `-30 .. +30 sec/day`

This feature is intended for compensating a systematic DS1302 drift.

Long-term calibration should be tested over multiple days before relying on a correction value.

## 11. ST / MO automatic and manual clock audio

### MO - spoken time

The clock automatically queues spoken time according to the current clock schedule when quiet mode does not suppress it.

Russian folders:

- 01 - hours
- 02 - minutes

German folders:

- 11 - hours
- 12 - minutes

### Manual current-time speech

Hold the physical `ST/MO` button for about 0.9 seconds or longer.

The current RTC hour and minute are spoken once using the currently selected language, regardless of whether the automatic mode is `MO` or `ST`.

Typical temporary LCD overlay:

```text
SPEAK TIME
21:37
```

Behavior:

- Works during quiet/night mode because it is a manual request.
- Respects global `SOUND OFF`; when muted, no speech is queued.
- If an alarm is ringing, the button action first stops the active alarm instead of starting manual time speech.
- If alarm music preview is playing, long-pressing `ST/MO` stops that preview before attempting manual speech.

### ST - strikes / chimes

Full hours use folder `18`.

- Tracks 1..12 correspond to the number of strikes.

Half-hour behavior currently uses a technical silent placeholder:

- Folder `19`
- Track `001`

A dedicated half-hour sound can be substituted later.

## 12. Daily alarms

Five independent daily alarms are available.

Each alarm stores independently:

- enabled / disabled
- hour
- minute
- sound type - `signal` or `music`
- selected music track - `01..45`

Typical list screens:

```text
ALARM 1 ON SIG
07:30 DAILY
```

or:

```text
ALARM 1 ON M12
07:30 DAILY
```

`SIG` means the standard long signal. `M12` means music track 12.

### Alarm editing sequence

1. Select Alarm 1..5
2. ON/OFF
3. Hour
4. Minute
5. Sound type - SIGNAL or MUSIC
6. If MUSIC is selected - music track 01..45
7. Save

For `SIGNAL`, saving happens immediately after confirming the sound type.

For `MUSIC`, the track-selection screen appears:

```text
ALARM 1 MUSIC
TRACK 12 / 45
```

Rotate the right encoder to choose a track.

### Previewing alarm music

On the music track-selection screen:

- Press the physical `Alarm` button to preview the selected track.
- The screen changes the second line to `PLAY NN / 45` while preview is active.
- Press `Alarm` again to stop preview.
- Rotating to another track stops the current preview before changing the track number.
- Leaving the edit screen also stops preview.

### Alarm audio sequence

At the alarm time, the audio sequence is:

```text
Alarm N spoken/service phrase
            ->
selected SIGNAL or MUSIC track
            ->
return to clock when audio ends
```

The alarm does **not** loop indefinitely.

It ends when either:

- the user stops it, or
- the selected signal/music finishes naturally.

A large safety timeout exists only as protection against a stuck DFPlayer/BUSY condition. It is not the normal alarm duration and should not truncate ordinary music tracks.

### Alarm signal mode

The language-specific Alarm 1..5 phrase plays first.

Then the standard long signal uses service phrase track `013`:

- Russian - `07/013`
- German - `17/013`

### Alarm music mode

The language-specific Alarm 1..5 phrase plays first.

Then one selected music track from folder `08` is played:

- `08/001` through `08/045`

Each of the five alarms can use a different music track.

### Alarm phrase tracks

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

### Stopping a ringing alarm

A ringing alarm can be stopped using:

- the Alarm button
- the right encoder push
- the left encoder push / ON-OFF

The stop request clears the pending alarm queue and sends a DFPlayer pause command. If the DFPlayer command-gap interval is still active, the pause command is deferred and sent as soon as the transport allows it.

Quiet mode does not block alarms.

Existing v3.2.0 alarm configurations are backward-compatible. If an old alarm has no `sound` field, v3.3.x automatically treats it as `SIGNAL`.

## 13. Global sound ON/OFF

Push the left volume encoder.

- ON - normal audio operation.
- OFF - global audio mute.
- While an alarm is ringing, the same push stops the alarm instead of toggling the mute state.

The LCD briefly displays the sound state during normal operation.

## 14. Audio folder map used by v3

Primary numeric DFPlayer folder structure:

- 01 - Russian hours
- 02 - Russian minutes
- 03 - Russian day/date
- 04 - Russian months
- 05 - Russian years 2023..2029
- 06 - Russian weekdays
- 07 - Russian service phrases / alarms / timer phrases
- 08 - music, tracks 001..045 currently used for selectable alarm music
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

- standalone music player mode using folder 08
- final half-hour ST sound
- broader audio priority rules between alarm, timer, clock speech/chime, manual time speech, and future music player
- persistent/deferred volume saving strategy
- long-term RTC correction validation
- birthday function / birthday melody
- extended hardware testing of all 45 selectable music tracks

## 16. Firmware versioning

`main_v3.py` contains an explicit application version and prints it on startup.

Current expected startup line:

```text
Speaking Timer-Clock v3.3.1 starting
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

Last documented state: v3.3.1. Core clock/timer/date/time functions are hardware-tested. Alarm navigation and spoken alarm phrase were hardware-tested in v3.2.0; selectable signal/music behavior introduced in v3.3.0 and ST/MO long-press manual current-time speech introduced in v3.3.1 require hardware confirmation. This document should be updated whenever user-visible behavior changes.
