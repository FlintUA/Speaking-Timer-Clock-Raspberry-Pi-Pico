# Changelog

## Unreleased - codex/phone-control

Experimental work after stable v1.0.0; no stable web-control release is claimed.

- Added an optional saved five-second timer warning, default OFF, through the physical Timer sound editor, browser checkbox and USB. Single 19/001.mp3 cues use timer output/volume; late/busy ticks are skipped and mute/cancel/zero stop the bounded owner. LOCAL cues avoid the ordinary no-BUSY queue fallback; BT loss does not queue a late replay. Conditional v17 is 640 bytes in the same three pages/sectors; v1-v16 loading is write-free and defaults warning OFF. Twenty-two tests and both builds pass; installation pending. See doc/TIMER_PREWARNING.md.
- Added random music for timer completion and each alarm slot 1-5. A cached SPI folder-08 numeric-file catalog supports numbering gaps and excludes invalid entries; empty/failed catalog uses signal. One chosen track survives snooze, deferral and LOCAL fallback; new occurrences avoid the immediately previous track when possible. Physical, browser and USB configuration use conditional Flash v16 (632 bytes, same sectors); old settings keep random OFF and require no load-time write. Alarm revision bits now represent all three sound modes without collision. Twenty-one checks and both profiles pass; approved installation and read-only startup/catalog/settings checks passed on November 13 as `1.0.0+28.g36286e9`. Hardware acceptance pending. See doc/RANDOM_EVENT_MUSIC.md for mirror-card requirements and acceptance.
- Added independent saved timer completion message ON/OFF, extra sound None/Signal/Music, and folder-08 track selection (1-255) through the physical menu, web settings and USB. The existing message+signal default, RU/DE asset limitations, output routing, separate volumes and Stop timer sound are retained. v15 extends the immutable v14 prefix to 624 bytes only for nondefault timer preferences; v1-v14 reads remain write-free. Tests cover all message/sound combinations, migration, corruption/failure fallback and browser/command guards. Approved installation and read-only startup/settings/default checks passed on November 13 as `1.0.0+27.ge5593d1`; user acceptance pending.
- Added a distinct Stop timer sound command and completion-audio status. The command identifies the current countdown completion event, rejects stale/ended events, and preserves independent alarm/player ownership. Status polling accelerates near countdown expiry and during completion/ringing. Alarm stop/snooze remains separate from countdown timers. Nineteen registered checks, an actual-handler ownership harness and both builds pass; approved installation/startup/settings checks passed on November 13 as `1.0.0+26.g3717033`. The user subsequently confirmed correct operation; measured timing and individual output/load cases remain separate checks.
- Added guarded browser alarm actions: Stop, Snooze 3 min and Cancel snoozed repeats. Runtime occurrence IDs and snooze-set revisions reject stale commands, including a later occurrence of the same alarm slot; existing command nonces make retries idempotent. Shared physical handlers preserve saved schedules and require no Flash migration. Software checks and both profiles build; approved installation and startup/settings verification passed on November 13; user acceptance pending.
- Added authenticated `/control` Quick controls: explicit sound on/off and time/date/nearest-alarm information speech. Shared main-loop guards, command retries and physical sound/speech functions preserve volumes and scheduling; operations do not write Flash. Detailed muted/editor/timer/alarm/player/audio/RTC/no-alarm results explain refusals. Nineteen checks and both builds pass; approved installation and read-only startup/settings checks passed on November 13. The user subsequently confirmed general operation; next-alarm wording remains a deferred audio-content issue.
- Made browser Bluetooth device management an explicit future roadmap stage: known/discovered lists, direct saved-device connection, scan/cancel, disconnect and forget. Current status/audio preferences do not connect a speaker.

- Added read-only overview status for validated device date/weekday, sound, active/deferred/nearest alarm, monotonic snoozes, Bluetooth name/state and home/direct Wi-Fi/NTP. NTP reference receipt, successful RTC application and retry are distinct. No control command, Flash schema or audio/scheduler behavior is changed by this substage. Eighteen software checks and both builds pass. Approved installation passed startup/HTTP/settings verification; user acceptance remains pending.

- Added read-only browser About with device, board, full tag-derived version/build and author FlintUA. Metadata comes from the installed firmware's shared version constants via status JSON; reconnect updates it and missing metadata is shown as unavailable. The approved November 13 installation passed USB/HTTP checks; the user subsequently accepted About on the phone.

- Expanded annual birthdays to fifteen across physical/USB/browser editing and scheduling; widened pending masks and added conditional v14 A/B storage (616 bytes, three programmed pages). v1-v13 reads retain the original five entries and initialize slots 6-15 disabled. Nondefault extra slots select v14; loading alone does not write Flash.
- Extended the single countdown to 1..86,399 seconds (23:59:59) with consistent physical/USB/browser limits. Added three scrolling timer wheels with touch, mouse, keyboard and continuous digit scaling/fading.
- Combined browser Play/Pause between Previous and Next, with Stop below, following the later row-layout specification. Added alarm music-track Test/Stop: a one-track audition through alarm routing without saving the alarm draft; audition-specific Stop does not stop ordinary music. No seeking or rotation gesture was added.
- Increased the web profile's response allocation to 4 KiB, lwIP heap to 48 KiB and POST contexts to four for the four-client target. This configuration is not proof of four-client hardware acceptance.
- On 2026-11-13, the user broadly accepted the recent improvements. A clean local Debug rebuild passed seventeen checks. Exact installed build identity and detailed new persistence/concurrency scenarios remain separately unconfirmed; see [evidence](doc/HARDWARE_TEST_EVIDENCE.md#november-13-expansion-user-report).

- Added the optional local HTTP profile, direct WPA2 access-point and home-network modes, live status, LCD connection progress and a 50-second direct-password screen. Browser control requires neither a cloud service nor Internet access.
- Added remembered browser authorization without an inactivity timeout, four scoped browser entries, browser Sign out, physical revocation/key rotation and optional home-subnet trust. Direct mode still requires browser pairing; home trust is off by default.
- Added browser timer and player transport controls, independent LOCAL/BT volume, language, four audio preferences, display-light mode and day/night brightness. HTTP callbacks queue validated commands for the main loop; sequence/nonces and epoch protect retries.
- Added five alarm and five annual birthday editors, stale-record conflict detection and Reload. Birthday dates accept `DD.MM` or `DD.MM.YYYY`; known birth years are optional metadata and do not change annual recurrence. No one-time reminders were added.
- Added v12 phone settings and v13 optional birth-year storage within unchanged A/B sectors. This branch's ordinary and offline profiles read/preserve v1-v13. Stable v1.0.0 cannot read v12/v13 records; see [rollback](doc/NETWORK_TIME.md#settings-compatibility-and-rollback).
- Replaced volume/brightness Apply controls with live sliders. Latest values survive in-flight edits, retry/status failures and command-ID conflicts. Physical encoder changes appear after pending edits are confirmed. Existing deferred autosave is retained.
- General access, timer, player, settings and schedule workflows are user-accepted. Revised date/mobile layout and immediate sliders are broadly accepted through build `499e78c`. Sixteen local checks passed with Node.js; identified web/ordinary profiles built and COM8/HTTP verification passed. Detailed acceptance and release preparation remain in the [roadmap](doc/ROADMAP.md).

The entries below describe historical release development. Intermediate menu counts and pending statuses reflect their original stages; current behavior and acceptance are in the roadmap, controls and hardware evidence.

## 1.0.0

First stable source release for Pico 2 W. See the [release notes](doc/releases/v1.0.0.md) for local build instructions, validation and known follow-up checks.

- Added external LCD illumination on GP6 with hardware PWM, OFF/ON/AUTO, and independent day and night brightness. Flash v11 keeps v1-v10 reads and defaults to OFF on upgrade. The user confirmed switching, brightness adjustment without audible interference and retention after restart on build ab06727. AUTO time transitions are deferred.

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
