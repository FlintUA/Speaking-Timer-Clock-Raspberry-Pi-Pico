# Native C migration audit - 2026-11-06

Branch: `develop/v3`. Baseline tested by user: Pico 2 W, COM8, DFPlayer Mini, DS1302, LCD1602.

## Confirmed
- C clock: DS1302, LCD1602, encoders/buttons, countdown, five alarms, RU/DE speech, quiet/half-hour modes, flash settings v3 and automatic USB BOOTSEL update.
- DFPlayer folder 08 returns 78 music tracks. Standalone music player operates with a pre-generated shuffled list navigated by TIMER, plays continuously, and has alarm priority (user hardware confirmation).
- `update-pico.ps1 -Build`: tested end-to-end via COM8 -> RP2350 USB boot drive -> UF2 -> CDC `flash-info`; `-VerifyOnly` also tested.
- Separate `experiments/bluetooth_a2dp_nest/nest_autotest`: previously hardware-tested A2DP Source to Google Nest Audio (SBC 44.1 kHz, AVRCP, generated 300 ms tone). **Not integrated** into main clock.

## Gaps
- Native `firmware/pico2w_c/CMakeLists.txt` does not link BTstack/CYW43; main clock has no Bluetooth audio route.
- Main C tree does not yet include Wi-Fi connection, NTP, DST/local-time handling, RTC learning/calibration port, Cast sender, or an audio/PCM capture/decoder source.
- DFPlayer UART 0x0F commands only control its own decoding/SD playback. They do not expose PCM to RP2350; using Nest requires separate PCM sourcing/decoding or hardware capture.
- Bluetooth and Cast are separate implementations. Bluetooth A2DP is the first milestone; Cast via Wi-Fi should be separately scoped (possibly with LAN server/helper), not assumed to be available on Pico.
- `doc/C_MIGRATION_PLAN.md`, `firmware/pico2w_c/README.md` and `doc/V3_ROADMAP.md` contain outdated provisional status and should be updated to reflect hardware-proven functionality.
- Audio queue uses BUSY pin plus timeout fallback; validate short tracks and clone behavior under continuous playback.
- Flash settings schema v3 and two tail sectors must be preserved. Avoid arbitrary firmware/OTA partition changes.

## Ordered implementation gates

1. **Bluetooth integration skeleton (no audible clock rerouting):** configure BTstack/CYW43 for the native C target and reproduce the prior Nest discovery, pairing, reconnect, 300 ms generated tone and status diagnostics. Keep LCD, RTC, timer, alarms and DFPlayer functional. Handle disconnect without blocking loop. Use a feature flag; no sound on boot by default. Build and hardware test separately.
2. **Audio source design gate:** choose how to obtain 44.1 kHz PCM for A2DP: digital MP3 decoding from storage accessible to Pico vs analogue DFPlayer output captured through an ADC/codec. Determine audio quality, wiring, storage and CPU/RAM constraints before implementation.
3. **Audio routing:** selectable LOCAL/BT; prioritize alarms/timers; test continuous music and reconnect/fallback.
4. **Wi-Fi + NTP:** nonblocking Wi-Fi, proper Europe/Berlin DST and RTC update safeguards; verify coexistence with Bluetooth under load.
5. **RTC calibration:** port MicroPython history, drift learning, safeguards and persistence with explicit migration plan.
6. **Cast feasibility:** evaluate direct Pico sender versus LAN helper (e.g., NAS). Cast receiver normally retrieves media URL; don't conflate with raw A2DP streaming.
7. **UI and hardening:** GP6 illumination, remaining UX parity, persistence and 24-hour soak tests.

## Safety
Never perform automatic flashing or erase settings from a test script without an explicit user hardware test. Keep USB DFPlayer local audio as fallback. No broad git cleanup/reset; preserve user untracked files and local audio data. Avoid vendoring restricted BTstack demo source. No hardcoded Wi-Fi credentials. Any new BT stack feature must be tested for license and Pico SDK 2.3.1 compatibility.

## Reference
- `source/v3/main.py`, `source/v3/music_player.py`, `source/v3/audio.py`, `source/v3/rtc_calibration.py`
- `firmware/pico2w_c/main.c`, `firmware/pico2w_c/drivers/dfplayer.c`, `firmware/pico2w_c/CMakeLists.txt`
- `experiments/bluetooth_a2dp_nest/README.md` and `nest_autotest/`
- Pico SDK networking libraries, BTstack A2DP Source demo, Google Cast developer documentation
