# Speaking Timer-Clock v3 - Development Roadmap

This roadmap tracks the next practical development steps for the current v3 clock and the planned Pico W / Pico 2 W evolution.

## Current baseline

- Current application line: v3.7.x
- External DS1302 RTC
- LCD1602 I2C
- DFPlayer Mini local audio
- Five alarms
- Countdown timer
- RU/DE speech
- Automatic RTC calibration support
- Music Player
- Quiet mode and automatic hour / half-hour behavior

## Near-term UX improvements

### 1. Cold-start indication

Goal: make it immediately obvious that the device has performed a real cold boot and that firmware startup has completed.

Planned behavior:

- On cold start show a short splash screen before the normal clock screen.
- Display firmware version, for example:
  - line 1: `TIMER-CLOCK`
  - line 2: `v3.8.0 START`
- Play one short dedicated startup sound after DFPlayer becomes ready.
- Startup sound must be different from alarm, timer and UI-click sounds.
- Do not replay the startup sound during ordinary UI transitions.
- Prefer detecting and distinguishing a cold boot from a software restart where practical.
- Startup indication must not delay normal operation excessively.
- If DFPlayer is unavailable, the LCD startup screen must still appear and the clock must continue booting.

Possible SD-card allocation:
- reserve a new service phrase / sound track for `STARTUP` after confirming a free track number.

Acceptance criteria:
- power cycle clearly shows the firmware version;
- one recognizable startup sound plays once;
- failure of the audio subsystem does not prevent the clock from starting.

### 2. LCD illumination / readability

Hardware context:
- the current LCD came from a printer and has no integrated backlight;
- readability is poor in low ambient light;
- therefore this is an external illumination project, not a normal LCD backlight-pin feature.

Investigation options:

1. Edge-lighting
   - place one or more diffuse LEDs along the edge of the LCD light guide / glass assembly if the mechanical construction allows it;
   - test white, warm-white and amber LEDs;
   - add a diffuser to reduce hotspots.

2. Front illumination
   - low-profile LED(s) above or beside the display;
   - illuminate the LCD surface at a shallow angle;
   - simplest mechanical option if edge-lighting is ineffective.

3. Rear illumination
   - only if the salvaged LCD construction is sufficiently transmissive and there is physical space behind it;
   - test before modifying the enclosure.

Electrical requirements:
- LED power should not be taken directly from a GPIO except for a very small test load;
- use a transistor or MOSFET when current is above a safe GPIO load;
- preferably support PWM brightness control;
- add a current-limiting resistor sized for the selected LEDs;
- illumination should not introduce visible noise into audio or RTC operation.

Future firmware behavior:
- setting: `DISPLAY LIGHT: OFF / ON / AUTO`;
- adjustable brightness if PWM hardware is added;
- optional night dimming;
- optional timeout after user interaction;
- illumination must be independent from the existing quiet-mode audio logic.

Acceptance criteria:
- LCD becomes comfortably readable in a dark room;
- no obvious hotspots in normal viewing position;
- no audible interference in DFPlayer output;
- no effect on DS1302 stability.

## Pico W networking phase

Planned after the current Pico build remains stable:

- migrate current clock core to Pico W without changing existing hardware behavior;
- Wi-Fi connection with timeout and graceful offline fallback;
- NTP reference time;
- DS1302 synchronization / calibration using NTP;
- preserve existing USB / Thonny RTC calibration as fallback;
- later add lightweight web settings or diagnostics.

## Wireless audio experiment

Goal: use JBL Link Portable as a higher-quality wireless speaker while keeping the clock as the music source.

Development order:

1. Pico W Bluetooth pairing experiment.
2. A2DP Source test using generated PCM tone.
3. WAV playback from storage to JBL Link Portable.
4. Playlist controls mapped to the existing Music Player UI.
5. Evaluate MP3 decoding + A2DP.
6. If Pico W resources become limiting, migrate the audio branch to Pico 2 W.

Local DFPlayer output should remain available as a fallback / alternate output.

Potential future UI:

```
MUSIC OUTPUT
LOCAL
JBL
```

## Other planned functions

- alarm snooze;
- alarm weekday schedules;
- sleep timer;
- service / information screen;
- persistent deferred volume save;
- dynamic alarm music-track limit;
- manual spoken date / nearest alarm;
- birthday and reminder events.

## Development principle

Add one finished block at a time, then perform a hardware test before starting the next major feature.
