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
