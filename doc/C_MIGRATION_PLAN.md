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

## Reserved GPIO map for the planned microSD / LCD backlight expansion (2026-11-06)

**Decision approved, documentation only - rewiring, drivers and firmware changes NOT yet performed.** Production MicroPython and current C `drivers/controls.c` still use timer encoder DT=GP11, CLK=GP10. Do not connect microSD SPI1 until this encoder has been moved and the firmware mapping has been updated together.

| Signal | Old / current assignment | Planned assignment | Pico 2 W physical pin |
| --- | --- | --- | --- |
| Timer encoder DT | GP11 | **GP12** | **16** |
| Timer encoder CLK | GP10 | **GP13** | **17** |
| SD card DO / MISO, SPI1 RX | Unused | **GP8** | **11** |
| SD card CS / SPI1 CSn | Unused | **GP9** | **12** |
| SD card CLK / SPI1 SCK | Currently timer CLK GP10 | **GP10** | **14** |
| SD card DI / MOSI, SPI1 TX | Currently timer DT GP11 | **GP11** | **15** |
| LCD1602 backlight PWM | Unused | **GP6** | **9** |

Power: SD-card module ground to Pico GND; supply must be chosen only after confirming the exact module's regulator/level-shifter design. Pico GPIO are **3.3 V only**; never feed 5 V logic to GP8-GP11. LCD backlight PWM GP6 must control a suitably rated transistor/MOSFET switch; do not connect the backlight LED directly to a GPIO pin. If using the LCD backpack's existing LED, inspect its A/K connections and existing switching/jumper before wiring.

Preserve: I2C LCD GP0/GP1, DS1302 GP2/GP5/GP4, volume encoder GP14/GP15, UART DFPlayer GP16/GP17 with BUSY GP18, existing button GP19-GP22/GP26-GP28. DFPlayer retains its **own** SD card and independent analog local audio; second card is exclusively for Pico digital MP3 reading and later A2DP playback. MP3 decoding / filesystem / SPI driver are future work.

Next required tasks:
- [ ] Inspect actual SD breakout's power and signal-level design and take wiring photos.
- [ ] Move Dupont timer encoder DT GP11->GP12 and CLK GP10->GP13, keeping both signals in the same orientation.
- [ ] Update **both** `firmware/pico2w_c/drivers/controls.c` and production `source/v3` pin definitions **only when that firmware will be deployed**; verify all other GPIO references before editing. Retain compatibility/testing guidance for boards with old wiring.
- [ ] Connect SD SPI1 GP8/9/10/11 with 3.3 V safe logic; implement SPI1 + FAT32 and test SD card independently.
- [ ] Implement GP6 PWM backlight driver and electrical switch, then day/night control.
- [ ] Bench-test encoder, display, DFPlayer, RTC, SPI SD and A2DP together before production migration.

**Critical**: As of this decision, this is a *planned pin reservation*, not a claim that either current firmware supports the new GPIO mapping.


### Hardware clarification: SD 3.3 V and separate display illumination (2026-11-06)

The user confirmed that the **external SPI microSD module is a 3.3 V module**. Plan its VCC from Pico **3V3(OUT), physical pin 36**, and GND from common ground. All SPI signals GP8/GP9/GP10/GP11 must remain at **3.3 V logic**. The module's stated supply rating does not by itself confirm its exact breakout circuitry or current draw; verify markings/pinout and check power headroom before final connection.

The installed **LCD1602 has no built-in backlight**. Consequently, GP6 (physical pin 9) is reserved for PWM drive of a **separate external LED used to illuminate the display**, rather than the LCD A/K backlight terminals or PCF8574 backpack's backlight control. Use GP6 -> approximately 1 kOhm base resistor -> NPN transistor (e.g. BC547/2N2222), emitter -> GND, collector -> LED cathode, LED anode -> LED current-limiting resistor -> appropriate supply. The previously proposed example uses VBUS 5 V through 220 Ohm for an LED with roughly 3 V forward drop, approximately 9 mA; confirm actual LED ratings and resistor/transistor pinout. Keep common ground; do not drive the LED directly from GPIO. VBUS 5 V is present only with suitable USB-derived power, so check the final clock power wiring before relying on it.

Three planned operating modes remain **OFF, ON, AUTO** (day/night dimming), but PWM driver, scheduling, electrical connections and firmware implementation are **not yet completed**. This clarification does not change the agreed timer encoder relocation GP11->GP12 (DT) and GP10->GP13 (CLK), or planned microSD SPI1 pinout.


## Known audio-language difference: timer completion (RU vs DE, 2026-11-06)

**Documented existing behavior - intentionally not fixed in this iteration.**
- **Russian (RU):** after countdown completion, the spoken phrase `Таймер завершён` plays, together with the double timer-finish sound.
- **German (DE):** after countdown completion, only the double timer-finish sound plays. The corresponding German spoken completion message is missing/not found at the expected location in the current audio asset set.
- User explicitly requested **leave current behavior unchanged** until a later audio-assets repair task. Do **not** substitute a guessed track, synthesize an unapproved announcement, or alter existing timer priority/finish signaling in this change.
- Future action: identify the correct German phrase/audio file, confirm its exact DFPlayer folder/track mapping and RU/DE sequencing, add/repair the asset and verify both language modes on hardware. Also account for this when migrating voice playback from DFPlayer to second microSD + software MP3/A2DP.
- The exact German source track name/location remains **unverified**. This is not a new implementation or evidence the underlying firmware cannot play such an asset.
