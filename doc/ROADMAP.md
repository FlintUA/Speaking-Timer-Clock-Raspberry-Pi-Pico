# Current roadmap

This is the authoritative roadmap for the C firmware. It distinguishes the stable main baseline from experimental work on `codex/phone-control`. Status reviewed on 2026-11-13 includes the capacity/status expansion and quick-control follow-up after `697c062`. Hardware evidence identifies installed candidates independently; the latest quick-control substage has not been installed. Earlier MicroPython plans and migration logs are historical; unchecked boxes in those files are not the current task list.

## Release baseline

Version 1.0.0 is the stable C firmware baseline. See the [release notes](releases/v1.0.0.md).

Implemented for the first release candidate:
- One combined Bluetooth device list per MAC, with known devices first.
- Preserve saved speaker names when discovery returns an unknown-name fallback.
- The original release candidate resumed the preferred connection after browsing; current main replaces this with persistent manual OFF/scan behavior until explicit Connect.
- Asynchronous Wi-Fi join and NTP time reference, hourly resynchronization and offline RTC operation.
- Europe/Berlin CET/CEST conversion, including the last-Sunday March/October transitions.
- Flash v8 Wi-Fi provisioning with v1-v7 read compatibility and unchanged sector boundaries.
- Dynamic alarm music selection using detected folder 08 count, supporting tracks 1..255.
- Firmware version/build identity and updater verification of that identity when a sidecar is present.
- Native tests and GitHub CI for time/DST, NTP validation, Bluetooth catalog behavior, alarm scheduling and Flash migration.
- Current English controls, network setup, release notes and hardware acceptance instructions.

Hardware acceptance and remaining coverage:
- Initial NTP synchronization and Bluetooth playback with Wi-Fi enabled are confirmed on build 7964939. A periodic RTC refresh was observed on build 7378aa1 before Portable connected. Extend coverage to an overnight run and check refresh during Bluetooth playback.
- The v11-capable firmware loaded the existing v10 settings and synchronized NTP. The user confirmed that brightness, alarms, birthdays, the selected speaker and audio settings survived a save and restart. Host tests cover the v10 -> v11 record migration and v1-v10 reads.
- Test unavailable NTP server and later recovery.
- Manual connection/disconnection, OFF across a power cycle, no automatic return after a subsequent scan and automatic recovery after a Portable power cycle are user-confirmed on build 7378aa1. Remaining detailed checks: scan cancel/timeout, late callbacks and speaker-initiated reconnect; retain list deduplication/name retention.
- Verify alarm selection above track 45 using files present on both cards.
- The Windows updater verified the installed build ID for ab06727 against the selected UF2 sidecar.
- Daytime acceptance covers timer completion and priority at 17:00, idle hourly speech at 18:00, and the scheduled Daily alarm at 18:02. Overnight operation remains unverified.
- Boot with Nest selected is user-confirmed on build 7378aa1. Confirm that a forgotten device remains deleted after power loss.

Wi-Fi access-point loss and recovery on the assembled clock is deferred at the user's request. The available router controls did not provide a way to disconnect only the Pico for this test.

## Next functional milestones

Phone control is being developed on `codex/phone-control`. Standalone direct and home Wi-Fi prototypes, integrated live status and browser timer control have passed user testing. The user also confirmed home access without repeated pairing, direct Wi-Fi reconnection without re-entering the password, and the LCD connection-progress screen. These experimental results are separate from the stable main release. See the [phone control instructions and evidence](../experiments/phone_control_ap/README.md).

Implemented and broadly user-accepted web functionality:

- Player Play/Pause/Resume/Stop/Next/Previous, accepted on build `44ca0814caac-bt-mp3-wifi-phone`.
- Independent LOCAL/BT volume, four audio output preferences, language and display-light mode/levels, accepted on build `aa20a9cc4962-bt-mp3-wifi-phone`.
- Five alarm and five birthday editors, accepted on build `3d87e9900de5-bt-mp3-wifi-phone`.
- Single birthday date field, optional birth year and mobile form alignment on build `d51dd7adf5b8-bt-mp3-wifi-phone`; the user reported the revised interface generally satisfactory before requesting sliders. This is broad acceptance, not explicit v13 save/restart coverage.
- Immediate volume and brightness sliders on build `499e78c94a0c-bt-mp3-wifi-phone`; the user reported that everything worked after installation. Sixteen host checks passed with Node.js available, including the production slider-script regression. COM8 verified the build, unchanged alarm/birthday records, valid Flash slots and synchronized NTP; read-only HTTP checks found all four sliders. These checks do not replace the remaining hardware scenarios.

### Current expansion status (2026-11-13)

The user reported that the latest changes were tested and worked correctly. Record this as broad functional acceptance, not proof that every migration, output/network combination or four-client scenario was exercised. See [hardware evidence](HARDWARE_TEST_EVIDENCE.md#november-13-expansion-user-report).

| Item | Current source and acceptance |
| --- | --- |
| Fifteen birthdays | Physical, USB and browser paths use 15 slots; pending/claim masks are 16-bit. v14 stores nondefault slots 6-15. General acceptance reported; slot 15 save/restart and timed delivery remain individually unconfirmed. |
| Single countdown through 23:59:59 | Shared 86,399-second limit, physical editor, USB validation and browser wheels are implemented. Zero and 24:00:00 are rejected. A full-length physical run was not reported. |
| Timer wheels | Hours/minutes/seconds, touch/mouse/keyboard operation and continuous digit scaling/fading are implemented. General interface acceptance reported. |
| Player layout | Previous / combined Play-Pause / Next, with Stop below. The latest recorded layout supersedes the earlier circular proposal; rotation and seeking are absent. |
| Alarm track audition | Test/Stop plays one selected track using alarm routing without saving the draft; `player_test` synchronizes the audition state, and `test_stop` affects audition only. General acceptance reported; exact output/priority combinations remain separate checks. |
| Four web clients | Four POST contexts, 48 KiB lwIP heap and ten configured TCP PCBs support the target; four stored browser authorizations remain a distinct limit. Real four-client/audio concurrency is not confirmed by the general report. |
| Browser About | Installed with separate approval on November 13. USB/HTTP metadata and preserved settings verified; the user subsequently accepted About on the phone. |
| Extended overview, substage 1 | Installed with separate approval. Eighteen tests, both builds, startup/HTTP and settings-preservation checks pass. The user subsequently accepted the general overview; individual ringing/snooze and network/output combinations remain focused checks. |
| Live alarm controls, substage 3 | User confirmed the three-minute repeat and cancellation on November 13; other focused cases remain separate. Stop, three-minute snooze and cancellation of snoozed repeats are implemented. Occurrence IDs and snooze-set revisions guard stale pages; existing command IDs/nonces guard retries. Nineteen tests and both builds pass; approved installation and startup/settings verification passed on November 13; user acceptance pending. |
| Quick controls, substage 2 | Explicit global sound on/off and time/date/nearest-alarm speech are implemented through the shared guarded application path. Nineteen checks and both builds pass; approved installation and startup/settings checks passed on November 13; the user confirmed general operation. Next-alarm wording is deferred for the audio refresh. |

Seventeen registered checks passed after the earlier clean local Debug rebuild on 2026-11-13. The later extended overview adds a native calendar/JSON test and passes eighteen checks, including production-script slider/overview and timer-wheel checks. This is software evidence; it does not establish real HTTP concurrency or radio/audio timing.

Remaining work, in recommended order:

1. Preserve an identified source revision and reviewed UF2 for each next build; installation always requires separate approval. Complete focused expanded-slot save/restart, four-client and audio-priority checks when the user is available; router outage testing remains deferred.
2. Quick controls (substage 2) are generally user-accepted. Preserve the separate focused test cases; defer next-alarm phrase replacement until functional development is complete and the audio assets are refreshed.
3. Countdown completion-audio Stop is generally user-accepted on `1.0.0+26.g3717033`; scheduled-alarm three-minute repeat and cancellation are also user-confirmed. Preserve separate focused cases for the installed alarm stop/three-minute snooze/cancel-repeat commands with stale-occurrence protection. The existing physical handlers remain the source of behavior.
4. Install and accept the prioritized [optional five-second warning](TIMER_PREWARNING.md), then complete focused acceptance of the installed random timer/alarm music stage ([details](RANDOM_EVENT_MUSIC.md)) and complete user acceptance of the installed independent timer message / None-Signal-Music / track preferences, then add browser controls for existing player mode/sleep, automatic announcement options, quiet hours and time/synchronization in separate reviewable increments.
5. Implement the dedicated [browser Bluetooth management stage](#browser-bluetooth-management-planned). Speaker selection must become available from the browser; choosing an audio preference alone does not connect a speaker.
6. Add network/access management as a separate stage, including clear mode-switch and revocation consequences.
7. Implement final navigation/presentation, light/dark/system themes and EN/RU/DE interface localization. Keep speech language separate and all assets local.
8. Finish release evidence, resource/four-client measurements and English instructions, then prepare a separate web-control release. No merge into main or stable web release is implied by functional acceptance.

The [expansion specification](WEB_CONTROL_SPEC.md) records the implemented capacity/UI stage and links the broader follow-up scope. One-time reminders, track seeking and new display/audio hardware remain excluded from current implementation.

### Browser Bluetooth management (planned)

This is an explicit future implementation stage, requested again after the overview check. Current browser Bluetooth status is read-only.

- Show up to four known devices and new discovery results by name; use a stable internal address for actions, not a changing row index. MAC addresses belong only in optional diagnostic details.
- Scan with visible progress/completion and cancellation. Preserve the existing intentional disconnect/manual-OFF behavior when discovery starts; ending/cancelling discovery must not silently restore a speaker.
- Connect a known device directly without scanning, or select a discovered device for pairing/connection. Persist a newly selected peer only after its stream becomes active. Preserve known names and address deduplication.
- Disconnect while retaining the known entry and pairing key; keep OFF through autosave/restart until explicit Connect. Forget is a separate confirmed deletion of an entry/key.
- Reuse existing audio/timer/alarm guards, shared authorization, command serialization and retry protection. Audio-output preferences and separate volumes remain independent of speaker connection.
- Explain replacement when the four-device registry is full. Keep current retry behavior; switch-latency optimization remains in the deferred tail.
- Use bounded responses for the device list, preferably separate from the already populated 4 KiB status response. Recheck peak memory and four-client contention.
- Accept with real saved/discovered Portable/Nest connections, cancellation, manual OFF, forget/save/restart, LOCAL fallback and status changes from physical controls. Do not claim support for BLE-only or legacy PIN-only audio devices.

Control restoration stage 1 is implemented. Hardware logs confirm accelerated selection, LOCAL timer start/finish/cancel and GP28 entry/back; remaining precise-field and player-mute checks are in TESTING.md. The user also confirmed timer routing to the selected Bluetooth speaker after changing its independent output preference.

Stage 2 playback/navigation was accepted by the user. Pause and sleep selection were subsequently reported working; full timed expiry and explicit output-specific coverage remain unconfirmed. Implemented controls include brand-neutral BT labels, normal/shuffle/repeat-one, immediate Next/Previous with bounded history, consistent physical controls and mode/count display. Position-preserving pause is now implemented for LOCAL/Bluetooth; the old MicroPython pause restarted the track.

1. Alarm snooze and weekday schedules are implemented and user-tested: Daily/Mon-Fri/Sat-Sun/custom days, GP28 three-minute snooze, explicit cancellation and Flash v9 migration. See TESTING.md.
2. Music sleep timer and position-preserving pause on LOCAL/Bluetooth are implemented. The user accepted pause, time selection and the explicit OFF field; full expiry and separate LOCAL/BT coverage remain acceptance checks. GP28 cycles OFF/15/30/60/90 minutes; TIMER pauses/resumes. Existing playlist modes and audio priorities are retained. See TESTING.md for checks on both outputs.
3. Spoken date, nearest scheduled/snoozed alarm and a nonblocking LCD startup/version splash are implemented and broadly user-accepted in build 02b4a51. Idle-clock MINUS short/hold and USB say-date/next-alarm expose date/forecast. Available year recordings cover 2023..2029; German alarm-slot phrases are absent. No new startup sound is queued. The two-page LCD About screen remains available.
4. Five annual birthday dates are implemented and broadly user-accepted in build 7964939; COM8 confirms configured dates saved in v10 and prior settings retained: menu/USB editing, persistent dates/handled years (Flash v10), existing 07/000.mp3 melody via Manual routing, idle/non-quiet delivery with 30-minute expiry. The user explicitly excluded one-time reminders because suitable spoken recordings are absent. February 29 matches only in leap years.
5. Offline RTC drift calibration remains conditional on measured need. The assembled board now has confirmed NTP synchronization and user-verified Bluetooth playback with Wi-Fi enabled. Next verify sustained operation/recovery, then compare a trustworthy reference over several days before choosing a drift rate. A fixed initial clock offset alone is not a rate measurement; the historical MicroPython calibration required at least five days between trusted references. No C drift correction has been added or enabled.
6. Complete the electrical schematic for the installed Pico 2 W wiring and power modules.
7. External LCD illumination on GP6 is implemented. The user confirmed ON/OFF, brightness adjustment, no audible interference and persistence after restart on build ab06727. AUTO time transitions are deferred at the user's request.

Each milestone requires focused tests and a hardware acceptance step. The statuses above distinguish implemented follow-up work from remaining plans; the first release candidate predates these follow-up changes.

## Deferred tail, in approved order

1. Investigate initial Bluetooth switch status 0x56 and optimize switching latency. Existing five-second retry is retained.
2. German spoken timer-completion phrase, last. The recording is currently absent. Keep the existing signal behavior until the correct recording is supplied and verified.

The intermittent double Bluetooth sound is deferred until the planned feature work is finished, at the user's request. The completed passive-capture findings remain in HARDWARE_TEST_EVIDENCE.md; the cause is unresolved.

## Existing behavior retained

Separate DFPlayer/Bluetooth volume levels remain intentional. MAC addresses remain internal identifiers and USB diagnostics; the LCD displays names. SD access remains read-only. SSP confirmation remains automatic, and legacy PIN-only speakers are unsupported.

## Future device hardware

On 2026-11-11, the user selected I2S audio through a MAX98357A amplifier as a logical direction for a likely new device with updated hardware. This is a future hardware milestone. Current web-control development continues on the assembled DFPlayer-based clock.

The proposed audio path is the Pico-connected SPI SD card, the existing software MP3 decoder, a PCM buffer, PIO/DMA I2S output and the MAX98357A speaker amplifier. A shared decode path could serve local audio and Bluetooth and consolidate audio assets onto one SD card. A separate hardware prototype should first verify available GPIO/PIO/DMA resources, amplifier power, speaker compatibility, memory use and sound continuity with Wi-Fi active. Exact wiring, resource allocation and performance remain to be measured.
