# Current roadmap

This is the authoritative roadmap for the C firmware on main. Earlier MicroPython plans and migration logs are historical; unchecked boxes in those files are not the current task list.

## First release: 1.0.0-rc.1

Implemented for the first release candidate:
- One combined Bluetooth device list per MAC, with known devices first.
- Preserve saved speaker names when discovery returns an unknown-name fallback.
- Resume the preferred connection when leaving Bluetooth browsing without choosing a new target.
- Asynchronous Wi-Fi join and NTP time reference, hourly resynchronization and offline RTC operation.
- Europe/Berlin CET/CEST conversion, including the last-Sunday March/October transitions.
- Flash v8 Wi-Fi provisioning with v1-v7 read compatibility and unchanged sector boundaries.
- Dynamic alarm music selection using detected folder 08 count, supporting tracks 1..255.
- Firmware version/build identity and updater verification of that identity when a sidecar is present.
- Native tests and GitHub CI for time/DST, NTP validation, Bluetooth catalog behavior, alarm scheduling and Flash migration.
- Current English controls, network setup, release notes and hardware acceptance instructions.

Before declaring v1.0.0 stable:
- Test Wi-Fi/NTP alongside A2DP and MP3 on the assembled clock.
- Verify v7/v8 -> v9 save/reboot with existing alarms, daily defaults, separate volumes, network settings and known speakers intact.
- Test unavailable access point, unavailable NTP server and later recovery.
- Verify Bluetooth list deduplication, name retention and return after leaving discovery.
- Verify alarm selection above track 45 using files present on both cards.
- Verify the new updater build-identity check on Windows.
- Complete an overnight run covering scheduled speech, timer and alarms.
- Confirm deletion remains effective after power loss and boot with Nest selected.

## Next functional milestones

Control restoration stage 1 is implemented. Hardware logs confirm accelerated selection, LOCAL timer start/finish/cancel and GP28 entry/back; remaining precise-field and player-mute checks are in TESTING.md. The user also confirmed timer routing to the selected Bluetooth speaker after changing its independent output preference.

Stage 2 is implemented, awaiting hardware acceptance: brand-neutral BT labels, normal/shuffle/repeat-one, immediate Next/Previous with bounded history, consistent physical controls and mode/count display. Position-preserving pause needs separate LOCAL/Bluetooth transport work; the old MicroPython pause restarted the track.

1. Alarm snooze and weekday schedules are implemented, awaiting device acceptance: Daily/Mon-Fri/Sat-Sun/custom days, GP28 three-minute snooze, explicit cancellation and Flash v9 migration. See TESTING.md.
2. Music sleep timer and position-preserving pause on LOCAL/Bluetooth; preserve existing playback modes, no-repeat shuffle and audio priorities.
3. Spoken date and nearest-alarm information, plus a dedicated startup indication. The two-page LCD About screen is implemented.
4. Birthday/reminder events after checking available recordings.
5. Offline RTC drift calibration if hourly NTP correction is insufficient for extended offline use.
6. Complete the electrical schematic for the installed Pico 2 W wiring and power modules.

Each milestone requires focused tests and a hardware acceptance step. These items are planned, not implemented by the first release candidate.

## Deferred tail, in approved order

1. Investigate initial Bluetooth switch status 0x56 and optimize switching latency. Existing five-second retry is retained.
2. Physical external LCD illumination, followed by GP6 PWM, OFF/ON/AUTO modes and night dimming. Hardware is not installed.
3. German spoken timer-completion phrase, last. The recording is currently absent. Keep the existing signal behavior until the correct recording is supplied and verified.

The intermittent double Bluetooth sound is deferred until the planned feature work is finished, at the user's request. The completed passive-capture findings remain in HARDWARE_TEST_EVIDENCE.md; the cause is unresolved.

## Existing behavior retained

Separate DFPlayer/Bluetooth volume levels remain intentional. MAC addresses remain internal identifiers and USB diagnostics; the LCD displays names. SD access remains read-only. SSP confirmation remains automatic, and legacy PIN-only speakers are unsupported.
