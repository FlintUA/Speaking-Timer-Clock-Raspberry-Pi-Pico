# Changelog

## Unreleased

- Removed the redundant Volume settings editor. The nine-item menu now ends with Audio Output (7), Bluetooth (8) and About (9). The left encoder, mute button, separate LOCAL/BT levels, autosave and explicit USB volume commands are unchanged; no settings-format migration is required. Device acceptance of the menu removal is pending.

- Added cursor focus for editable LCD values, including precise timer, alarm/day and settings fields. After user acceptance of steady underline focus, switched to the requested hardware blinking character cursor for better visibility. Cursor state is restored after repaint and cached to avoid repeated I2C commands; blinking needs no periodic host updates. Driver tests cover command sequencing and failures; the user accepted hardware blinking in build 755f55b.
- Reduced snooze from five to three minutes at the user's request. Deadline, status and USB help use one duration constant; no settings-format change is needed. The new duration awaits hardware acceptance.
- Added weekday schedules (Daily/Mon-Fri/Sat-Sun/custom) and five-minute GP28/USB snooze with monotonic deadlines, cancellation and existing audio priorities. Flash v9 reads v1-v8 and retains daily behavior for old alarms. Current-format rollback needs a v9-capable build. Device acceptance is pending.
- Added MINUS hold (at least 900 ms) to exit music while retaining short-press Previous. Added timestamped, read-only COM capture for intermittent Bluetooth diagnosis.
- Replaced brand-specific JBL output labels with BT. Added bt CLI spellings while retaining jbl aliases and the existing Flash layout.
- Restored NORM/SHUF/REP1 music modes, immediate Next/Previous with bounded history, automatic play on entry, hold-to-exit and mode/count/output display. Track changes wait for transport readiness instead of discarding the request. PLAY/STOP still restarts the track; position-preserving pause is deferred. Device validation is pending.
- Restored GP28 precise timer editing, accelerated 1/5/10-minute selection and sound toggle in the music player/alarm editor. Muting stops LOCAL or Bluetooth music without changing stored output levels. Device acceptance is pending.
- Firmware versions now follow reachable Git release tags automatically at configure/build time. USB includes post-tag/dirty metadata; About uses a compact + marker. Builds without Git metadata are explicitly unidentified.
- Added About at the end of settings: Speaking Clock / Pico 2 W and firmware version / by FlintUA. TIMER rotation switches pages; MINUS returns. Device validation is pending.

## 1.0.0-rc.1

First tagged C firmware release candidate for Raspberry Pi Pico 2 W. Existing hardware-tested capabilities include RTC/LCD, encoders, timer, alarms, LOCAL audio, SPI FAT32/MP3 and Bluetooth playback with Portable/Nest.

### Added

- Optional asynchronous Wi-Fi/NTP with Europe/Berlin CET/CEST conversion and RTC correction while idle.
- Local Windows Wi-Fi provisioning helper; settings Flash v8 with backwards reads of v1-v7.
- Firmware version/build ID and updater verification using the selected UF2's build-ID sidecar.
- Build identity distinguishes Bluetooth/MP3/Wi-Fi profiles as well as the source revision.
- Dynamic alarm track selection and v8 track storage through 255.
- Host tests and Windows/Linux GitHub CI.

### Fixed

- Duplicate saved/scanned Bluetooth addresses in the combined UI/command catalog.
- Unknown discovery labels overwriting saved speaker names.
- Preferred connection not being resumed when leaving completed discovery without selecting a new device.
- Serial overflow handling so an overlong command tail is discarded instead of interpreted separately.
- First-boot updater verification now accepts three blank clock settings sectors with a valid Flash layout.

### Validation and release status

This is a prerelease. Host tests cover alarm scheduling, calendar/DST and NTP packet validation, Bluetooth catalog behavior, and v1-v8 Flash migration/CRC/fallback. Firmware profiles are built before tagging. New network/RTC functionality, v8 hardware persistence, updated UI behavior and updater build verification require on-device acceptance before v1.0.0 stable.

After new saves replace both clock settings slots with v8, old v7 firmware cannot read the latest settings. A current -NoWifi build is the compatible offline fallback.

### Deferred

Bluetooth switch-latency optimization, physical LCD illumination/PWM and the missing German timer-completion recording remain at the end of the roadmap, in that order. See [ROADMAP.md](doc/ROADMAP.md) for other planned functional milestones.
