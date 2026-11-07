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

## Required audio output contract - unified routing

The device has **one logical audio program**, comprising every sound produced by the clock: Russian/German speech and time announcements, click/chime/startup sounds, alarms, timer signals, and continuous shuffled music. All of it must follow the **selected output**, not only music.

User-selectable destinations:
- `LOCAL`: existing DFPlayer Mini -> built-in amplifier/speakers.
- `BLUETOOTH / NEST`: Pico 2 W A2DP Source -> Google Nest Audio.
- `BLUETOOTH / JBL`: Pico 2 W A2DP Source -> JBL Link Portable.
- Future `CAST / NEST`, `CAST / JBL`, or `CAST / SPEAKER GROUP`: Wi-Fi Cast sender/helper path, separate implementation.

Only one active output at a time in the initial version; simultaneous local+remote playback is not promised. Preserve one application-facing audio API (enqueue/play/stop/volume/status/priority) with backend routing. Do not conflate A2DP control with DFPlayer UART commands; digital PCM must be generated or acquired for wireless output. Treat LOCAL as safe default and fallback when Bluetooth connection fails; consider local alarm/timer fallback even if a remote device is selected but not reachable. Never discard/interrupt critical alarms silently. Remote reconnection and output changes must not block RTC, menus or timer deadlines.

Both target speakers support Bluetooth reception and Chromecast built-in per their published specifications; device pairing and Wi-Fi/Cast behavior still need integration tests. For initial wireless integration, add selectable A2DP peers without changing flash v3; use RAM-only selection or a separate versioned persistence migration when ready.

Acceptance tests: output choice persists only when explicitly designed, every sound category reaches the selected output, mute/volume/STOP behavior is consistent, alarms interrupt music, remote disconnect falls back locally and records a status, and clock continues running with no speaker available.

## A2DP hardware milestone - 2026-11-06: JBL Link Portable

**User verified on bare spare Pico 2 W (COM7):**
- Experimental combined `speaking_clock_c` with `CLOCK_ENABLE_A2DP=ON` builds and boots.
- Bluetooth connection established to JBL Link Portable.
- A2DP SBC stereo 44.1 kHz configured; stream established and started.
- AVRCP channel opened and absolute volume events received.
- Generated 300 ms tone was audible on JBL, followed by continuous encoded silence.
- Clock RTC polling continued while Bluetooth was operating. Missing DS1302 messages are expected on spare board.
- Nest Audio is currently not discoverable/does not enter pairing mode; postpone Nest troubleshooting. **JBL is the sole active test target** until otherwise requested.

**Next acceptance gate:**
1. On JBL, verify repeated `bt-tone`, `bt-silence` and `bt-status` after an established A2DP stream; test reconnect after JBL power-cycle.
2. Explicit JBL peer selection and controlled pairing (current upstream demo auto-selects first audio-class device and auto-confirms SSP). Do not hardcode observed MAC into public firmware.
3. Connect A2DP output status to `Audio Router`; keep `LOCAL` as safe default, with no flash schema migration yet.
4. Introduce a real PCM source for voice/timer/music. DFPlayer UART commands **cannot** supply PCM to Bluetooth. Perform a digital audio-source feasibility test before committing to MP3 decode or analogue capture hardware.
5. Only then test the full clock assembly with `CLOCK_ENABLE_A2DP`; preserve the known-good build and BOOTSEL recovery.

**Do not conflate a working A2DP carrier/tone with complete wireless sound routing.**

## Next bench test - flash-resident PCM clip to JBL

- `bt-clip` plays an original fixed 400 ms two-note chime, stored as 8000 Hz signed 8-bit mono PCM in `clock_pcm_clip.h`.
- A2DP callback converts PCM to 16-bit stereo samples at the currently negotiated rate (simple nearest-neighbor resampling); no SD, DFPlayer, decoder or external hardware required.
- `bt-tone` remains available as the known-good 300 ms sine test.
- `bt-silence` stops either source, while the A2DP transport continues with digital silence.
- `bt-status` shows `PCM_CLIP` while the clip is playing and `SILENCE` afterwards.
- The PCM sample is a temporary proof-of-concept and **not** a production speech/music decoder. Resampling quality and output gain need listening tests.
- Build only with `build-clock-bt.ps1` and flash `firmware/pico2w_c/build-bt/speaking_clock_c.uf2` to the spare Pico 2 W; leave production clock untouched.
- Bench acceptance: JBL pairs; `bt-status` ACTIVE; `bt-clip` produces audible two-note sound, returns to silence; `bt-tone` still works; `bt-silence` still works; 30+ seconds of stable link.

## JBL discovery vs known-address connection - next test

User observed that `JBL Link Portable` stays paired in Google Home, but the experimental A2DP clock sees `No Bluetooth speakers found, scanning again...` until the user manually allows Bluetooth discovery. This is consistent with the upstream example relying on Bluetooth inquiry, which only finds discoverable devices.

Add `bt-connect XX:XX:XX:XX:XX:XX` (USB serial) to initiate an outgoing A2DP connection directly to a user-supplied known BD_ADDR. This command does **not** re-pair and should work with non-discoverable, previously paired sinks when link security is accepted. The existing `bt-status`, `bt-clip`, `bt-tone` and `bt-silence` remain available.

**Lab procedure**: keep JBL powered on and NOT in discovery mode; boot the spare Pico, issue `bt-connect` with the BD_ADDR observed in the earlier successful pairing log, inspect `[BT] Direct connect ... status=`, then A2DP connection/stream events, and retry `bt-clip`. A successful initiation status is *not* proof that the remote accepted the connection; verify stream ACTIVE and audible output.

**Caveats**: do not commit paired MACs to source; the address is supplied interactively. This initial diagnostic command does not persist the target or provide retry/reconnect scheduling. If it works, next implement controlled boot-time auto-connect with bounded backoff and Bluetooth events, then couple actual A2DP status to Audio Router. Existing auto-scanning remains a fallback when no direct command is sent.


## Boot-time direct A2DP target - experimental gate

**Verified:** On bare spare Pico 2 W, USB `bt-connect 04:21:44:FA:A9:0E` succeeded without Google Home discovery mode. A2DP reached ACTIVE and a PCM clip played.

**Next build:** `powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -JblAddress '04:21:44:FA:A9:0E'`. Target address stays in the local CMake cache and generated build file, not committed to firmware source. When the Bluetooth HCI controller reaches WORKING, firmware attempts a direct connection to the target instead of waiting for inquiry. Without the parameter, previous scanning behavior is preserved. The first version only handles startup initiation (and immediate fallback if rejected); robust asynchronous failure/reconnect retry remains to implement after hardware validation.

**Acceptance:** With JBL ON and not discoverable, reboot spare Pico and capture full first ~20 seconds log; look for `[BT] Boot direct connect target`, A2DP connection, stream ACTIVE, and `bt-clip`. If connect fails asynchronously, preserve log and add bounded retry later. Do not flash the production clock until this stage is stable.

## Experiment: asynchronous JBL reconnect after A2DP signaling loss

Implementation in generated BTstack sample patch:
- On `A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED`, reset signaling CID, schedule a BTstack run-loop timer, and retry the configured boot target.
- On `A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED` failure, clear CID and schedule another attempt.
- Retry delay uses 5 seconds, then 10 seconds, then 30 seconds repeatedly, without `sleep_ms()` or blocking clock input/RTC.
- On successful signaling connection, cancel any pending timer and reset delay sequence.
- When no `-JblAddress` target is configured, automatic retries are disabled and the existing discovery mode remains.
- Stream/tone/PCM tests remain unchanged. `bt-clip` is expected to report unavailable during disconnect, and work again after `Stream started`.

**Not yet proven on hardware.** Build using the usual PowerShell command with `-JblAddress`, flash only spare Pico. Test while connected: turn off JBL and wait for `Signaling released` and `Reconnect scheduled in 5000 ms`; leave Pico powered, turn JBL on (without Google Home discovery). Confirm a later `Reconnect attempt`, A2DP stream ACTIVE and audible `bt-clip`. Keep capture of failures/timeouts.

**Remaining limits:** no HCI connection-attempt watchdog yet; if remote never completes an outgoing connection, backoff may stall. Audio Router still reports LOCAL and does not receive live BT state. No DFPlayer MP3 decode or alarm fallback yet. Do not promote to production until tests pass.


## Audio Router - first live A2DP status bridge (experimental)

Implementation gate: Add read-only `clock_a2dp_stream_active()` from the generated BTstack A2DP source; main-clock event loop polls it and updates `audio_router_bluetooth_state()` only when the selected logical destination is `BT_JBL`. A2DP media STREAM_STARTED makes the effective logical route `BT_JBL`, stream release or reconnect makes it revert to `LOCAL`, and successful A2DP recovery promotes it again.

New UART diagnostics:
- `audio-select jbl`: set RAM-only requested target to BT_JBL and immediately sample media stream readiness. Requires CLOCK_ENABLE_A2DP and does not change Bluetooth peer or firmware settings.
- `audio-select local`: select LOCAL (RAM-only).
- `audio-route`: show requested/effective and `STREAM_ACTIVE`/OFFLINE. Logs transitions while JBL is selected.

**This is a diagnostic-only readiness bridge.** The clock's DFPlayer still physically plays speech, chimes, timers, alarms and music even when the logical Audio Router reports effective BT_JBL. Do not use this as evidence of wireless clock-audio routing. No persistent config/schema migration. Production build remains A2DP OFF. Standalone `bt-clip` / `bt-tone` still feed JBL directly; test commands only.

Bench acceptance: build/flash spare Pico; observe A2DP ACTIVE; send `audio-select jbl` and `audio-route` -> requested=BT_JBL/effective=BT_JBL/STREAM_ACTIVE. Turn JBL off without resetting Pico -> effective=LOCAL/OFFLINE. Turn JBL on, await reconnect -> effective=BT_JBL/STREAM_ACTIVE. Send `audio-select local` -> effective=LOCAL. Confirm `bt-clip` remains operational.

Next task: design actual digital audio-source and safe scheduling backend before tying audio_queue to remote route. For alarm safety, LOCAL remains effective when remote unavailable.


## Hardware verified: Audio Router live status / JBL reconnect (2026-11-06)

User hardware log on bare spare Pico 2 W confirmed:
- `audio-select jbl` accepted and `audio-route` returned `requested=BT_JBL effective=BT_JBL bluetooth=STREAM_ACTIVE`.
- On speaker disconnect, A2DP stream and signaling were released, 5s reconnect scheduled and `[AUDIO-ROUTE] bluetooth=OFFLINE effective=LOCAL` logged.
- First retry yielded A2DP HCI status `0x04`; second scheduled retry (10s) successfully established a new signaling CID `0x03` and streaming.
- Live route promoted back to `[AUDIO-ROUTE] bluetooth=STREAM_ACTIVE effective=BT_JBL`.
- Existing UART PCM clip played successfully before the disconnect. Logs do not demonstrate real clock voice/timer/alarm/music routing and do not demonstrate `audio-select local` test.

**Decision for next gate:** Keep a single logical sound API but do not implement a misleading DFPlayer-to-Bluetooth "switch". DFPlayer's UART-based MP3 decoder does not provide PCM to the Pico. Before activating physical route change, evaluate audio asset/PCM storage and decode method, and implement a self-contained source or sampled PCM track playback through A2DP. All critical alarms must remain audible via LOCAL during incomplete integration, and no flash layout migration yet.

## GPIO hardware expansion decision - SPI1 microSD and LCD PWM (2026-11-06)

User confirmed the assembled clock uses removable Dupont leads and that moving timer encoder wiring is easy. Reserve the Pico 2 W hardware SPI1 bus for a **second** microSD card used by Pico as an MP3 digital source; DFPlayer Mini and its original microSD remain unchanged for local fallback. Reserve GP6 (physical pin 9) for the LCD1602 backlight PWM output with a transistor switch.

Planned wiring:
- Timer encoder DT: GP11 (physical pin 15) -> **GP12 (pin 16)**.
- Timer encoder CLK: GP10 (pin 14) -> **GP13 (pin 17)**.
- SPI1 SD DO/MISO -> **GP8 (pin 11)**, CS -> **GP9 (pin 12)**, CLK -> **GP10 (pin 14)**, DI/MOSI -> **GP11 (pin 15)**.
- LCD backlight PWM -> **GP6 (pin 9)**.
- SD module VCC is **not specified** until breakout regulator/level shifting is identified. Never apply 5V signals to Pico GPIO.

Existing drivers still use GP11/GP10 for the timer encoder, so the SD bus cannot be wired without coordinated firmware changes. **No hardware changes or firmware edits were made in this documentation commit.** Keep production clock operational; update C and MicroPython configurations at the appropriate rollout stage. See `doc/C_MIGRATION_PLAN.md` for full pin table, verification and rollback checklist.


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


## Hardware verified: main Pico 2 W SPI1 microSD FAT32 bring-up (2026-11-06 21:28)

User reports the assembled main clock on USB COM8 with the new 3.3 V SPI1 SD module on GP8 MISO, GP9 CS, GP10 SCK, GP11 MOSI, and timer encoder physically remapped to GP12/GP13. The new SD card contains a duplicate of DFPlayer MP3 files.

Observed live serial transcript:
```text
[RTC] 21:28:55 06.11.2026
[SD] SPI1 initialized: SDHC/SDXC, bus=3947368 Hz, read-only
[SD] FAT32 detected at LBA 128, 7741440 sectors, sectors/cluster=8
[SD] PASS: SPI sector read + FAT32 boot sector
[RTC] 21:28:56 06.11.2026
```
Verified: SD initialization, SPI sector read, recognizable FAT32 volume geometry, continuing RTC activity. FAT cluster size = 4096 bytes (8 × 512). This does **not** yet prove file traversal, opening/reading MP3 files, software MP3 decoding, A2DP streaming from SD, or encoder functionality on relocated GPIO. SD remains read-only. Preserve DFPlayer fallback and existing audio setup.

Next gate: add read-only FAT32 root/subdirectory traversal and diagnostic listing/opening of an exact file (e.g. `/07/001.mp3`), then a bounded sequential-read test before integrating a decoder.


## FAT32 directory / file read diagnostics implemented (2026-11-06)

After successful live SPI1 `sd-test` on the **assembled main clock COM8**, new experimental C firmware provides read-only FAT32 8.3 directory and file inspection:
- `sd-test` must run first to initialize SPI1 and probe FAT32.
- `sd-list` lists root entries; `sd-list 07` lists short names in folder 07 (up to 64 entries). Long filename entries are skipped, only DOS 8.3 names are shown.
- `sd-file 07/001.mp3` resolves first-level subdirectory 07 and exact short file name, prints length, first cluster, and first 16 bytes read from first data sector. This does **not** decode or play MP3.
- New `drivers/sd_fat32.c/.h`, linked via CMake; bounded directory FAT-chain scan (64 clusters), 512-byte read-only buffer. No writes and no flash settings migrations.
- Encoder **already confirmed by user** to be functioning after GP12/13 relocation. Volume push/button GP20 was also verified functioning: `toggle_sound()` logs ON/OFF and LCD displays ON/OFF. **No button firmware change needed**.
- Before flashing the main clock, save `flash-info` and alarms and ensure new image preserves reserved flash config region. Test build first, and keep rollback UF2 available.
- **Pending**: local compile, hardware FAT32 listing, exact file read, and SD+Bluetooth simultaneous test.


## One-command production-hardware update preference (2026-11-06)

User confirmed that the assembled clock is on `COM8` and uses the previously existing `update-pico.ps1` firmware updater, which enters BOOTSEL through serial `bootsel arm` / `bootsel confirm` and verifies the running firmware without a physical BOOTSEL press.

Enhanced `update-pico.ps1` with optional `-Bluetooth` and `-JblAddress` switches. The preferred update workflow for the A2DP-enabled clock is:

```powershell
git pull --ff-only
powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Build -Bluetooth
```

This invokes `build-clock-bt.ps1`, selects `build-bt/speaking_clock_c.uf2`, then uses the updater's existing serial BOOTSEL, UF2 copy and postflash verification. The original `-Build` without `-Bluetooth` remains the non-Bluetooth `build-ninja` path. Close serial monitor before running; do not confuse the two build variants.

The assembled clock's volume encoder push was tested by the user and correctly toggles sound ON/OFF in serial log and on LCD; no adjustment required. FAT32 directory and file commands were committed but are not yet hardware validated.


## Full SD FAT32 directory and file lookup verified; conservative JBL level (2026-11-06 21:39)

The user ran the experimental clock firmware on assembled **Pico 2 W COM8** and reported:
- `sd-test`: SPI1 bus 3947368 Hz, SDHC, FAT32 starts at LBA 128, 7741440 sectors, 8 sectors/cluster, PASS.
- `sd-list`: 27 root entries; music and numbered directories visible.
- `sd-list 07`: `000.MP3` through `016.MP3` visible, file sizes reported.
- `sd-file 07/001.mp3`: 53103 bytes, first cluster 1476, first bytes `49 44 33 03 00 00 ...` (ID3 metadata header); PASS.
- `bt-status`: A2DP ACTIVE, SILENCE, 44100 Hz; RTC ticks continued throughout.

This proves FAT32 directory traversal and first MP3 sector access but **not full-file reading, decoding, or routing audio from SD into JBL**.

To protect against loud playback during initial A2DP tests, the experimental BTstack source-generation patch was updated to multiply the output by 0.1 (integer PCM samples divided by 10) for both the short tone and PCM test clip, before SBC encoding. This is **not** an AVRCP/absolute-volume adjustment on the JBL speaker itself, and does not change the DFPlayer local amplifier. User should additionally set JBL hardware volume low before testing. Compile and hardware effect of the new attenuation are not yet verified.


## Incremental full-file FAT32 read diagnostics staged (2026-11-06, pending hardware test)

After user confirmed JBL output with conservative 10% PCM gain and successful COM8 `sd-list`/`sd-file`/`bt-status`, introduced an experimental bounded *whole-file* test:

- `sd-stream 07/001.mp3` - opens an 8.3-named file and reads it incrementally, **one 512-byte sector per main loop iteration**, including FAT32 next-cluster traversal. Emits final `[SD-STREAM] PASS bytes=<...> clusters=<...> crc32=<8 hex digits>`.
- `sd-stop` cancels the read. Invalid or premature FAT chains and sector read errors report FAIL. The diagnostic is **read-only** and refuses to start while timer/alarm/music/queued DFPlayer audio is active.
- A strict maximum file size of 2 MiB protects the live clock from unexpectedly huge test inputs.
- This is a **CRC32 transfer test**, not MP3 decoding, playback, or proof of correct bytes without a separately computed reference CRC32. The CRC can later be compared to a host-side `Get-FileHash` alternative or Python `zlib.crc32`.
- Code added in `firmware/pico2w_c/drivers/sd_fat32.c/.h` and `main.c`; no writes to SD, changes to Flash settings, or DFPlayer modifications.
- Test only on the assembled Pico 2 W COM8 after `git pull --ff-only` and `powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Build -Bluetooth`. Close serial monitor first. Then `sd-test`, `sd-stream 07/001.mp3`; watch for `PASS bytes=53103`; follow with `bt-status`, `flash-info`, `alarm-status`.
- Compilation, CRC results, uninterrupted Bluetooth playback and RTC responsiveness **not verified yet**. Keep firmware rollback option. Helix/MP3 decoder integration is a later step.


## Full SD stream verified; Flash A/B redundancy needs investigation (2026-11-06 21:47)

User's COM8 real-device log:
- A2DP connected to `04:21:44:FA:A9:0E`, 44100 Hz stereo SBC, AVRCP absolute volume 29%; test tone completed, streaming silence.
- SPI1 `sd-test`: FAT32 PASS, LBA128, 7741440 sectors, 8 sectors/cluster.
- `sd-stream 07/001.mp3`: **PASS** `bytes=53103 clusters=13 crc32=B81FA766`. Streaming the whole file through FAT cluster chains works; host-side checksum comparison remains pending.
- `bt-status`: ACTIVE SILENCE 44100 Hz, RTC continued.
- `flash-info`: 4 MiB flash, `guard=OK`, `slot0=VALID sequence=33 version=3`, but `slot1=OCCUPIED sequence=0 version=0`. Earlier both slots were VALID.
- `alarm-status` retains configured alarms: 1 OFF 07:00 signal1; 2 ON 08:33 signal1; 3 ON 09:00 signal1; 4 ON 09:26 signal6; 5 ON 10:34 signal1.

**Caution**: The current `settings_store_save` uses the opposite slot as destination and explicitly refuses to erase any unknown nonblank contents. If slot0 is newest VALID and slot1 remains OCCUPIED, saving fails with `SETTINGS_STORE_OCCUPIED`. Do **not** erase/overwrite the suspect sector or force a save. Read-only next diagnostics: `flash-inspect` and `autosave-status` before any further firmware updates; identify cause and safe recovery with an off-device backup.

The confirmed success is full MP3 *file byte read*, **not** MP3 decoding or audible SD-to-A2DP playback. Plan Helix decoder integration only after Flash investigation and robust rollback options.


## Confirmed settings/BTstack flash collision and staged non-destructive fix (2026-11-06 21:49)

User's assembled clock COM8, read-only `flash-inspect` output:
```
[FLASH] slot0 offset=0x003fc000 status=VALID ff=4047 zero=18 other=31
[FLASH] slot0 head16=4b4c4353030000002100000000050001
[FLASH] slot1 offset=0x003fd000 status=OCCUPIED ff=3868 zero=80 other=148
[FLASH] slot1 head16=4254737461636b00534d455200000010
[FLASH] autosave pending=no current=ru,5,OFF debounce=0ms cooldown=0ms retry=0ms
```
`0x42 0x54 0x73 0x74 0x61 0x63 0x6b 0x00` is ASCII `BTstack\0`, confirming that the former settings slot1 (`-3` from flash end) was overwritten by BTstack's own nonvolatile storage. This was **not random corruption**. The clock's valid record at `-4` remains intact (sequence 33); alarm status previously verified. Pico SDK docs state two BTstack flash bank sectors from `-3` and `-2` for RP2350; last sector `-1` reserved for RP2350-E10 workaround.

**Staged source fix** in `firmware/pico2w_c/services/settings_store.c`:
- New clock A/B slots: `-6` (`0x003fa000`), `-5` (`0x003fb000`).
- Old clock slot `-4` (`0x003fc000`) is **READ ONLY** legacy fallback and will never be erased or selected as write destination.
- BTstack-owned `-3` (`0x003fd000`) and `-2` (`0x003fe000`) left untouched; final `-1` untouched.
- Load newest valid record across new A/B and legacy old slot. On subsequent actual user setting change the first new save targets new slot0; never migration-on-boot.
- `flash-info` and `flash-inspect` will show the two new slots and `legacy_slot0`; compile-time check prevents overlap with SDK's BTstack bank offset for A2DP builds.
- No attempt to recover the previous overwritten backup slot. The old slot1 is currently BTstack storage and **must not be erased**.
- **Compilation and hardware verification pending**. Before update, use the scripted build + flash only after validating compile and preserving firmware rollback. Post-update: expect `slot0=BLANK`, `slot1=BLANK`, `legacy_slot0=VALID sequence=33` until a deliberate settings change. Do not issue arbitrary `save` simply to force migration; initial read-only verification is sufficient.

This Flash conflict should take priority over adding an MP3 decoder. Documentation and code changes do not indicate the physical hardware has been modified.


## Main-clock Flash collision fix verified on hardware (2026-11-06 21:54)

User successfully compiled/flashed and tested the A2DP firmware via COM8. Observed:
```text
[FLASH] size=4096 KiB slots=0x003fa000,0x003fb000 image_end=0x1009ce7c guard=OK
[FLASH] slot0=BLANK sequence=0 version=0 offset=0x003fa000
[FLASH] slot1=BLANK sequence=0 version=0 offset=0x003fb000
[FLASH] legacy_slot0=VALID sequence=33 version=3 offset=0x003fc000 READ_ONLY
[FLASH] BTstack banks start=0x003fd000 (reserved, not inspected)
[INFO] A2DP stream: ACTIVE; audio: SILENCE; sample rate: 44100 Hz; samples remaining: 0
```
All five alarm entries remain intact (1 OFF 07:00 signal1; 2 ON 08:33 signal1; 3 ON 09:00 signal1; 4 ON 09:26 signal6; 5 ON 10:34 signal1). RTC continued ticking; JBL A2DP SBC and AVRCP connected. **Verified:** non-overlapping address layout, valid legacy settings fallback, preservation of alarms. **Not yet tested:** first save into new A/B settings slots and subsequent reboot/redundant backup creation; do not claim complete A/B migration until that test. Future MP3 integration should keep the flash guard and 10% test PCM attenuation.


## MP3 decode-only experiment staged (2026-11-06)

Implemented experimental, opt-in `CLOCK_ENABLE_MP3` linked against a separate
checkout of Helix-MP3-Decoder pinned at revision
`e4f5adadb5126708144e55a862f59e318cbffb51`.
- Added seekable read-only FAT32 file API with cluster traversal.
- `audio/mp3_probe.c` decodes up to 4096 stereo PCM frames in bounded 256-frame
blocks through Helix seek/read callbacks; prints rate, bitrate and CPU microseconds.
- USB command `mp3-test 07/001.mp3`; no MP3-to-A2DP connection yet, no SD writes.
- Updater `-Build -Bluetooth -Mp3` explicitly enables this integration;
the existing `-Build -Bluetooth` remains MP3-free.
- Licensing: third-party wrapper GPLv3, underlying RealNetworks MP3 decoder
RPSL/RCSL; vendor source remains outside project and should be reviewed before
redistribution. No assumptions of permissive relicensing.
- Hardware build, PCM output, A2DP coexistence and CPU/RAM performance
are NOT yet confirmed.


## Helix MP3 on Pico 2 W: initial hardware decode PASS (2026-11-07 07:43)

User tested assembled Pico 2 W via COM8 with optional `-Build -Bluetooth -Mp3` image:
```text
[SD] SPI1 initialized: SDHC/SDXC, bus=3947368 Hz, read-only
[SD] FAT32 detected at LBA 128, 7741440 sectors, sectors/cluster=8
[SD] PASS: SPI sector read + FAT32 boot sector
[MP3] Initial frame decoded: rate=48000 Hz bitrate=192000 bps, init=90350 us
[MP3] PASS: pcm_frames=4096 blocks=16 sample_rate=48000 bitrate=192000 total_decode_us=100416 (no audio output)
[INFO] A2DP stream: ACTIVE; audio: SILENCE; sample rate: 44100 Hz; samples remaining: 0
[FLASH] size=4096 KiB slots=0x003fa000,0x003fb000 image_end=0x100a8194 guard=OK
[FLASH] slot0=BLANK sequence=0 version=0 offset=0x003fa000
[FLASH] slot1=BLANK sequence=0 version=0 offset=0x003fb000
[FLASH] legacy_slot0=VALID sequence=33 version=3 offset=0x003fc000 READ_ONLY
```
**Verified:** Helix decodes MP3 bytes read directly from SPI/FAT32 into RAM while A2DP stays connected and RTC advances. The 4096 frames represent 85.33 ms of 48kHz source stereo audio. The measured ~100.42 ms includes ~90.35 ms initial frame decoding and setup, so the incremental 16-block phase cost was ~10.07 ms (not an end-to-end continuous real-time benchmark).

**Next engineering work:** buffer decoded PCM outside BT callback, avoid synchronous SPI/MP3 decode inside BTstack, convert source 48000 Hz to negotiated 44100 Hz via a bounded resampler, integrate a ring-buffer consumer with A2DP and track underruns/CPU budget. Keep 10% initial volume and DFPlayer fallback. Need continuous 30-60s test; initial probe alone does not establish glitch-free playback. New clock A/B slots remain BLANK with valid read-only legacy record.


## MP3-to-JBL PCM ring integration staged (2026-11-07)

After successful on-device Helix decode-only test (48kHz / 192kbps, 4096 frames, ~100ms including initialization), committed an **experimental five-second** MP3 playback path:
- New `audio/pcm_ring.c/.h`: 8192-frame stereo PCM queue at the Bluetooth sink's 44100Hz (~186ms capacity), underrun and consumed-frame counters.
- `audio/mp3_probe.c` now includes a separate `mp3-play` mode, decoding at most five seconds from FAT32 using 256-source-frame batches in the cooperative main loop. It skips the Helix MP3 probe mode while playing, and does not perform SD/MP3 decode inside the A2DP callback.
- Fixed-point/integer phase linear interpolation between source samples converts 48000 Hz or accepts 44100 Hz; decoded PCM is queued as two-channel signed 16-bit. Intermediate multiplication uses 64-bit integer arithmetic to avoid overflow. Only 44.1kHz negotiated A2DP is accepted.
- The BTstack generated-source patch now pops ready PCM from ring in `produce_audio` (ahead of test tone/clip), still applies the existing global 10% PCM output amplitude before SBC.
- Commands: `mp3-play 07/001.mp3`, `mp3-status`, `mp3-stop`. Playback refuses if Bluetooth inactive, sound muted, timer/alarm/music/DFPlayer audio active, and is stopped if these higher-priority clock states arise or A2DP disconnects. SD diagnostic commands refuse during playback, because FAT32 uses a shared sector buffer.
- Startup prebuffer target 4096 stereo frames, max test duration five seconds. Ring underruns and CPU decode time are reported. Source stops and transitions to SBC silence after draining.
- Existing MP3-free build remains functional with no file output; `-Build -Bluetooth -Mp3` is required for the new experiment. Same external pinned Helix decoder checkout.
- **Not yet verified**: compilation, UF2 update, actual JBL audio, audible level, timer priority in live use, underrun counts and longer-session stability. Keep rollback UF2. Before playback run `sd-test` and confirm A2DP stream ACTIVE. Use `mp3-stop` if anomalies arise. No Flash writes, changes to DFPlayer mapping, or mandatory startup MP3 playback.

## First audible MP3 via JBL and doubled PCM gain (2026-11-07 07:52)

The user confirmed **audible MP3 sound** through the JBL Link Portable after `sd-test`, `bt-status` and `mp3-play 07/001.mp3`.
Observed hardware output:
```text
[MP3-PLAY] START /07/001.mp3: 48000Hz 192000 bps -> 44100Hz, max 5 seconds, gain 10%
[MP3-PLAY] Playback started, prebuffer=4233 frames
[MP3-PLAY] active=YES buffered=0/8192 source=48000Hz target=44100Hz decoded=93312 output=85730 consumed=86370 underruns=2 eof=YES decode_us=455596
[MP3-PLAY] COMPLETE; resuming A2DP silence
```
A2DP remained ACTIVE, and DS1302 RTC ticks continued. Helix decoded about 1.944 seconds of this particular audio file; file EOF occurred before the five-second maximum. There were **two underrun events** and 640 more frames consumed than actual file output (tail silence during drain). Continuous glitch-free audio is not yet established. The user explicitly requested doubling audio volume. The shared Bluetooth `produce_audio()` final attenuation is now **20% amplitude for active MP3 PCM ring**, while startup tone and flash PCM clip retain 10% amplitude. Serial MP3 start text updated to 20%. This is +6.02 dB of PCM amplitude, subject to unchanged JBL volume setting. Build and device testing of the new gain are pending. Next focus: underrun handling/tail drain and longer source tests.

## MP3 gain 40% on user request (2026-11-07 07:56)

COM8 actual test of 20% PCM MP3 gain: audible but user reports still too quiet.
`mp3-play 07/001.mp3` completed with decoded 93312 source frames, output
85730 at 44.1kHz, consumed 85730, underruns=1, decode_us=437163.
BTstack stream ACTIVE; RTC continued. One underrun at EOF may be a benign
drain boundary and should be distinguished from true streaming starvation.
User requests doubling gain once more: MP3 amplitude raised from 20% to **40%**
(`int32_t * 2 / 5`), retaining tone/clip at 10%. No AVRCP speaker volume
command was added; local DFPlayer remains unchanged. The logged MP3
gain message was updated to 40%. Hardware test of this new gain pending.


## Full-file MP3 player, pause/resume, EOF handling staged (2026-11-07)

After first audible COM8 test of `/07/001.mp3` at 40% PCM gain, user approved extending experimental player. Changes in `audio/mp3_probe.c/.h`, `audio/pcm_ring.c/.h`, `main.c`:
- Removed the five-second decode budget: `mp3-play <8.3 path>` now reads until Helix reports actual file EOF; at most 256 source PCM frames decoded per main-loop iteration. No hard file duration limit (normal streaming buffers stay bounded).
- Added `mp3-pause`, `mp3-resume` and existing `mp3-stop`. Pause freezes SD reads and PCM queue consumption; BTstack emits silence. Resume continues with preserved decoder and PCM ring state.
- Added explicit terminal EOF flag in PCM ring; one partial SBC packet padded with silence at file end no longer increments `underruns`. Unexpected starvation *before* EOF still increments this diagnostic.
- Existing priority interruptions (alarm/timer/DFPlayer/mute/A2DP disconnect) still stop MP3, with 40% MP3 amplitude only, 10% other BT tests.
- All changes are source-only until an actual rebuild/flash. No real-hardware verification of full-file playback, pause/resume or EOF accounting as of this note.
- Planned COM8 checks: `sd-test`, `bt-status`, `mp3-play 07/001.mp3`, `mp3-status`, `mp3-pause`, `mp3-resume`, `mp3-stop`; then use a file longer than 5 s and watch for continuing audio, RTC stability, no unexpected underruns.


## Full-file MP3 hardware verification, gain to 53.3% (2026-11-07 08:08)

User reports live COM8 test after full-file MP3 and pause/resume implementation:
- `mp3-play 07/001.mp3`: decoded 93312 source frames, output/consumed 85730, `underruns=0`, `eof=YES`, `decode_us=458747`; `COMPLETE` and RTC ticking.
- `mp3-play 07/002.mp3`: decoded 92160, output/consumed 84672, `underruns=0`, `eof=YES`, `decode_us=440524`; `COMPLETE`, RTC ticking.
- Pause and resume commands were issued several seconds **after** the ~2-second file completed, returning correctly `Cannot pause: no active playback` and `Cannot resume: no paused playback`. Pause/resume during active stream is still unverified; use an MP3 longer than 10s.
- After completion, `mp3-status` displays `consumed=0` because the PCM ring reset clears historical counts. Status preservation is a minor diagnostic improvement for later; successful playback's pre-reset log is correct.
- User requested volume +1/3 vs existing 40% gain; now updated `produce_audio` to multiply signed samples by `8/15` (53.33% amplitude, ~+2.50dB), preserving other A2DP tests at 10% and local DFPlayer untouched. Updated UART status label. Await compile and on-device verification.

Long uninterrupted playback, control commands while file playing and Bluetooth disconnect/reconnect still require hardware testing. No clock settings Flash change.


## Hands-free pause/resume MP3 test staged (2026-11-07)

After successful 53.3% PCM playback test of short MP3 with zero underruns,
reviewed current audio architecture. Normal speech, timers, chimes and alarms
are still queued by `audio_queue_t` and sent exclusively through DFPlayer;
`audio-select jbl` currently only chooses a logical destination. Avoid
prematurely hijacking critical alarms; add an independently testable mechanism.

New diagnostic `mp3-pause-test 07/001.mp3` is gated by the same 44.1kHz A2DP
readiness and clock/audio idle checks as `mp3-play`. Starts the existing
full-file Helix decoder and waits until >300ms of output PCM is consumed
before freezing producer and consumer for 1000ms. During pause BTstack
sends SBC silence while the PCM and file cursor remain intact. It resumes
automatically and finishes the file, so it can test pause/resume even when
the existing MP3 clips are only two seconds long.

Also captures last playback's consumed/underrun counters across ring reset,
so post-completion `mp3-status` remains diagnostically useful.
No change to amplitude (53.3%), Flash writes, DFPlayer or alarm routing.
Compilation, hardware pause/resume sound behavior and long-file stability
still require validation before routing automatic speech.


## Bluetooth audio router first live-track experiment staged (2026-11-07)

After proven on-device 53.3% Helix playback with auto pause/resume,
inspected `audio/queue.c`, `audio/router.c`, and current alarm/timer code.
The production audio queue plays exclusively on DFPlayer, requiring explicit
per-track completion/backpressure and failover semantics before routing
automatic speech safely.

Implemented first **explicit test only** `audio-route-test F T`:
- `audio-select jbl` selects the test target; `audio-route-test 7 1`
opens `07/001.mp3` on SPI1 and uses the existing proven MP3 -> PCM ring ->
resample -> BTstack A2DP path. `audio-select local` plus `audio-route-test 7 2`
uses the existing DFPlayer `audio_queue_enqueue` path.
- If Bluetooth is not ready at command time or Helix file open fails, enqueue
the requested track locally. If A2DP drops while a remote test is active,
stop MP3 and enqueue whole track to LOCAL unless an alarm, timer or music
source preempts. Do not replay partially consumed voice on Bluetooth drop.
- The router selection remains **RAM-only**, resets to LOCAL on reboot, with
no changes to Flash slots, alarm configuration, event scheduling,
timer queue or DFPlayer hardware.
- Tracks from the normal `play F T`, `say` commands and automatic events
remain LOCAL and are *not* redirected by `audio-select jbl`.
- COM8 hardware compile, remote/local test, failover on disconnect and
event preemption remain to be verified before activating automatic queue
routing.


## Opt-in manual speech sequence over JBL - staged 2026-11-07

User validated the LOCAL `say 08 25` sequence from folder 01/008 and
02/025 and first BT-vs-LOCAL `audio-route-test` stage. Observed
`A2DP Source: Connection failed, status 0x04`, with a delayed
retry `Direct connect ... status=0x00`; the latter indicates
that a connection request was accepted, not that an active stream
was later established. Root cause unknown.

New logic added to `main.c`:
- `announce_time()` uses the selected JBL only for manual speech
  when the actual A2DP stream is active at 44.1kHz. Otherwise local
  DFPlayer playback remains available.
- Build at most two `audio_track_t` items with the existing
  `speech_enqueue_time()`, preserving RU/DE language and exact-hour
  selection. Do not change persisted settings or the production queue.
- In cooperative main loop, `bt_speech_service()` serially starts
  each MP3 and advances only once `mp3_play_active()` is false
  after complete ring drain. Each MP3 uses 53.3% digital gain.
- A2DP drop / file open failure replays the interrupted fragment and
  remaining fragments locally, unless a higher-priority alarm/timer/
  local-audio event owns the output. Such priority cancels the manual
  sequence instead; no critical alarm route is modified.
- `audio-select local` during manual speech routes the unfinished
  sequence to DFPlayer; `mp3-stop` cancels it. Concurrent MP3 and
  route-test commands refuse while a sequence is pending.
- Automatic hourly, timer, alarm, music, and chime output still uses
  DFPlayer as previously. Reboot continues selecting LOCAL.

**Pending hardware checks:** rebuild with `-Bluetooth -Mp3`;
`sd-test`, `bt-status`, `audio-select jbl`, `say 08 25` to hear
both words with ordered `[BT-SPEECH] SEGMENT 1/2`, `2/2`, and
`COMPLETE`. Then repeat after `audio-select local`. Next test
disconnect/fallback only after verified two-word playback.
