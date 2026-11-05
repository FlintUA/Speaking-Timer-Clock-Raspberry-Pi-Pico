# Clock v3.10.0 -> native Pico SDK C migration

**Status:** design and preparation; existing clock remains on MicroPython v3.10.0.
**Target:** Raspberry Pi Pico 2 W, Pico SDK; one repository, staged implementation.

This document is the source of truth for migration progress. It is **not**
a claim that any clock function has already been ported to C.

## Current verified states

| Component | Baseline | Status |
| --- | --- | --- |
| Clock, LCD, RTC, timers, menus, alarms and DFPlayer | `source/v3/main.py` + modules | MicroPython v3.10.0; user working baseline |
| Bluetooth Classic SBC/A2DP Source to Nest | `experiments/bluetooth_a2dp_nest/nest_autotest/` | Verified on spare Pico 2 W |
| Automatic A2DP link, 300 ms tone, continuous encoded silence | same | Verified |
| USB serial commands `p`, `s`, `i` | same | Verified by serial log and user |
| Native C integrated clock | `firmware/pico2w_c/` | Not yet implemented |
| WAV/MP3 playback over A2DP, NTP with A2DP | future | Not yet verified |

## Source-to-target migration inventory

| Current Python module | Responsibility | Planned C module |
| --- | --- | --- |
| `main.py` | boot, event routing, controls, UI integration | `app/` scheduler and app state |
| `timer_engine.py` | second-precision countdown, up to 4 hours | `app/timer` |
| `ui.py` | LCD state names, display formatting | `app/ui` + LCD driver |
| `config.py` | config.json, defaults, five alarms, quiet mode | `services/settings` |
| `audio.py` | DFPlayer UART, commands, BUSY, audio queue | `drivers/dfplayer` + `audio/queue` |
| `music_player.py` | normal/shuffle/repeat | `app/music` |
| `ntp_time.py` | Berlin-local time conversion/NTP | `services/time_sync` |
| `wifi_manager.py` | network lifecycle | `services/wifi` |
| `rtc_calibration.py` | calibration persistence & correction | `services/rtc_calibration` |
| `rtc_usb_sync.py` | USB RTC utility | standalone test/utility |
| `library/` drivers | LCD, DS1302, rotary, etc. | `drivers/` |
| Bluetooth A2DP experiment | source stream to Nest | `audio/a2dp` |

**Important:** verify the exact `main.py` runtime behavior and configuration
migration before treating these module boundaries as exhaustive.

## Verified production hardware mapping from source/v3/main.py

| Device / control | Pico pins | Interface |
| --- | --- | --- |
| LCD1602 I2C | GP0 SDA, GP1 SCL | I2C0 at 400 kHz |
| DS1302 | GP2, GP5, GP4 (source constructor order) | three-wire, check driver pin roles |
| DFPlayer Mini | GP16 TX, GP17 RX, GP18 BUSY | UART0 at 9600 baud |
| Volume encoder | GP14, GP15 | GPIO rotary |
| Timer encoder | GP11, GP10 | GPIO rotary |
| Volume press | GP20 | GPIO |
| Timer press | GP19 | GPIO |
| Additional controls | GP21, GP22, GP26, GP27, GP28 | GPIO |

Treat `source/v3/main.py` as authoritative if the wiring changes.

## Behavioral contract for parity

- Countdown uses monotonic milliseconds rather than RTC minute boundaries.
- User-selected language RU or DE must be preserved.
- Five alarms and their assigned sound/track must survive restarts.
- Silence / quiet-hours behavior must be preserved.
- DFPlayer audio must remain usable if Nest is absent.
- Losing Bluetooth must not stall the clock, LCD or timers.
- Wi-Fi/NTP must not corrupt RTC calibration or local time behavior.
- Persisted settings must not be overwritten by incompatible new firmware.
- Avoid blocking delays in audio delivery and the user-interface loop.
- Startup and reconnect must have bounded recovery paths.

## Rollout and test gates

- [x] Stable MicroPython clock baseline v3.10.0 exists.
- [x] Standalone Nest A2DP tone/continuous silence working.
- [x] Serial control `p` / `s` / `i` returns expected status.
- [ ] Pin mapping checked on complete production hardware.
- [ ] Bootable native C clock displaying DS1302 time on LCD1602.
- [ ] Encoders/buttons and timer parity tests.
- [ ] DFPlayer queue, alarms and clock voice announcements.
- [ ] Bluetooth integration with UI and persistent audio routing.
- [ ] Bluetooth disconnect/reconnect and DFPlayer fallback.
- [ ] Wi-Fi/NTP alongside A2DP verified on Pico 2 W.
- [ ] Settings and RTC calibration migration verified.
- [ ] Long-duration testing and full production cutover.

## Repository maintenance rules

1. **No destructive tidy-up:** do not delete `source/v3`, `library`,
   `sound`, `schematic` or `doc` just because the C tree exists.
2. Place new integrated native code in `firmware/pico2w_c`; keep
   `experiments/bluetooth_a2dp_nest` as a reproducible milestone.
3. Documentation should distinguish **verified** from **planned** changes.
4. Do not commit generated `build/` trees, binaries, local Wi-Fi secrets,
   runtime caches or personal backup files.
5. Keep upstream BTstack code out of repository unless appropriate permission
   and licensing requirements have been confirmed.
6. Each migrated module must have a defined test and rollback path.

## Local Git precaution

Development PC contains untracked user files and modified sound assets.
Do **not** use `git clean -fdx` or `git reset --hard` as cleanup.
Always inspect `git status -sb` first; back up or stash only the intended
tracked file when needed. Never add local secrets or private archives.
