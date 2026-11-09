# Changelog

## Unreleased

- Added external LCD illumination on GP6 with hardware PWM, OFF/ON/AUTO, and independent day and night brightness. Flash v11 keeps v1-v10 reads and defaults to OFF on upgrade. The user confirmed switching and brightness adjustment without audible interference on build ab06727. AUTO time transitions and persistence remain hardware checks.

- Added Bluetooth Disconnect and persistent OFF after manual disconnect or starting a scan. Scan completion/cancel/menu exit no longer resume a speaker. Explicit Connect restores normal retries; Forget remains separate. Existing no-target Flash encoding retains known devices, pairing, output preferences and volumes without a format change. Paging, pairing and late stream events are gated by the selected target, and boot connections wait for loaded settings. Policy/storage regression tests pass. The user confirmed connection/disconnection, persistent OFF across a power cycle and no automatic return after a subsequent scan on build 7378aa1. Separate cancel/timeout, incoming-peer and link-loss recovery coverage remains in TESTING.md.

- Added five annual birthday dates under Birthdays (settings item 9; About becomes 10), with complete draft editing and USB status/configuration/test commands. Existing 07/000.mp3 follows Manual routing and LOCAL fallback. Events wait for idle, non-quiet audio for up to 30 minutes. Flash v10 retains v1-v9 loading and adds persistent dates/handled years, with birthdays disabled on migration. One-time reminders are excluded by user request. Host migration/calendar tests pass. The user broadly accepted build 7964939 and COM8 confirmed saved v10 dates with prior settings intact; detailed timed/audio coverage remains in TESTING.md.

- Added idle-clock MINUS short date and hold next-alarm information, plus USB say-date/next-alarm. Date speech uses existing RU/DE recordings; nearest occurrence honors weekday masks, calendar boundaries and RAM snoozes. Manual audio routing/fallback is retained. A nonblocking startup LCD/version splash adds no extra sound. Host tests and repository date-asset checks pass; the user broadly accepted the functionality after build 02b4a51 was installed. Detailed language/output/fallback coverage remains in TESTING.md.

- Show an explicit OFF in the player sleep field instead of a blank. Compact PLAY/PAUS/STOP/WAIT/MUTE labels keep OFF and remaining minutes in the same position within 16 columns. Playback and deadline behavior are unchanged; the user accepted this display follow-up in build fa38eaf.

- Added position-preserving player pause/resume on TIMER for LOCAL (paced DFPlayer pause/resume with queue state retained) and Bluetooth (existing decoder/PCM pause). Added GP28 sleep presets OFF/15/30/60/90 minutes, remaining-minute display and silent stop/exit at a monotonic deadline, including during pause. Both features are RAM-only; playlist modes, audio priorities and Flash format are retained. Host tests pass; physical LOCAL/BT acceptance is pending.

- Removed the redundant Volume settings editor. The nine-item menu now ends with Audio Output (7), Bluetooth (8) and About (9). The left encoder, mute button, separate LOCAL/BT levels, autosave and explicit USB volume commands are unchanged; no settings-format migration is required. The user accepted the new menu after build 4bad974 was installed.

- Added cursor focus for editable LCD values, including precise timer, alarm/day and settings fields. After user acceptance of steady underline focus, switched to the requested hardware blinking character cursor for better visibility. Cursor state is restored after repaint and cached to avoid repeated I2C commands; blinking needs no periodic host updates. Driver tests cover command sequencing and failures; the user accepted hardware blinking in build 755f55b.
- Reduced snooze from five to three minutes at the user's request. Deadline, status and USB help use one duration constant; no settings-format change is needed. The new duration awaits hardware acceptance.
- Added weekday schedules (Daily/Mon-Fri/Sat-Sun/custom) and five-minute GP28/USB snooze with monotonic deadlines, cancellation and existing audio priorities. Flash v9 reads v1-v8 and retains daily behavior for old alarms. Current-format rollback needs a v9-capable build. Device acceptance is pending.
- Added MINUS hold (at least 900 ms) to exit music while retaining short-press Previous. Added timestamped, read-only COM capture for intermittent Bluetooth diagnosis.
- Replaced brand-specific JBL output labels with BT. Added bt CLI spellings while retaining jbl aliases and the existing Flash layout.
- Restored NORM/SHUF/REP1 music modes, immediate Next/Previous with bounded history, automatic play on entry, hold-to-exit and mode/count/output display. Track changes wait for transport readiness instead of discarding the request. Position-preserving pause is supplied by the later player update described above. Device validation is pending.
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

Bluetooth switch-latency optimization and the missing German timer-completion recording remain at the end of the roadmap, in that order. See [ROADMAP.md](doc/ROADMAP.md) for other planned functional milestones.
