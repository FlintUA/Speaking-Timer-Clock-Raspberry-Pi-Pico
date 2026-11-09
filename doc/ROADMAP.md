# Current roadmap

This is the authoritative roadmap for the C firmware on main. Earlier MicroPython plans and migration logs are historical; unchecked boxes in those files are not the current task list.

## First release: 1.0.0-rc.1

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

Before declaring v1.0.0 stable:
- Initial NTP synchronization and Bluetooth playback with Wi-Fi enabled are confirmed on build 7964939. A periodic RTC refresh was observed on build 7378aa1 before Portable connected. Extend coverage to an overnight run and check refresh during Bluetooth playback.
- The v11-capable firmware loaded the existing v10 settings and synchronized NTP. The user confirmed that brightness, alarms, birthdays, the selected speaker and audio settings survived a save and restart. Host tests cover the v10 -> v11 record migration and v1-v10 reads.
- Test unavailable NTP server and later recovery.
- Manual connection/disconnection, OFF across a power cycle, no automatic return after a subsequent scan and automatic recovery after a Portable power cycle are user-confirmed on build 7378aa1. Remaining detailed checks: scan cancel/timeout, late callbacks and speaker-initiated reconnect; retain list deduplication/name retention.
- Verify alarm selection above track 45 using files present on both cards.
- The Windows updater verified the installed build ID for ab06727 against the selected UF2 sidecar.
- Complete an overnight run covering scheduled speech, timer and alarms.
- Boot with Nest selected is user-confirmed on build 7378aa1. Confirm that a forgotten device remains deleted after power loss.

Wi-Fi access-point loss and recovery on the assembled clock is deferred at the user's request. The available router controls did not provide a way to disconnect only the Pico for this test.

## Next functional milestones

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
