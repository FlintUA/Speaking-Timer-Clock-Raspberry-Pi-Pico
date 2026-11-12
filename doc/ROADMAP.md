# Current roadmap

This is the authoritative roadmap for the C firmware. It distinguishes the stable main baseline from experimental work on `codex/phone-control`. Status reviewed on 2026-11-12 against source revision `499e78c` and user reports. Earlier MicroPython plans and migration logs are historical; unchecked boxes in those files are not the current task list.

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

Remaining work, in order:

1. Documentation reconciliation is completed for source revision `499e78c`: current profile/build instructions, storage compatibility, menu numbering and acceptance boundaries are aligned. Historical release notes remain historical.
2. Complete focused hardware checks: two browsers, Sign out/revocation and home-trust disable; remembered access and optional birth years after save/restart; simultaneous physical/browser edits and conflicts; rapid commands and network recovery; sound continuity through refresh/phone disconnect; timer/alarm priorities. Repeat the core workflow in direct/home modes on LOCAL/BT outputs. Follow the [acceptance checklist](../experiments/phone_control_ap/README.md#remaining-web-release-acceptance).
3. Improve the phone interface presentation and navigation after functionality acceptance. Basic field alignment and sliders are already present; the final visual design is not complete.
4. Finish user-tested release evidence, build identified artifacts and prepare a separate web-control release. No merge into main or stable web release is implied by functional acceptance.

The first web release does not require browser controls for playlist mode/sleep, Bluetooth discovery/connection, quiet hours, half-hour/exact-hour options or spoken date/next-alarm requests. These are still available through their existing physical/USB paths where implemented. Additional browser controls require a later scope decision. One-time reminders remain excluded.

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
