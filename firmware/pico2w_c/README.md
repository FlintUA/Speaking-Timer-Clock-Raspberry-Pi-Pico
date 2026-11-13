# Pico 2 W clock C firmware

Stable baseline: v1.0.0. This checkout's `codex/phone-control` branch develops the optional browser-control profile; it is not a new stable release. Current controls are documented in [CONTROLS_AND_MODES.md](../../doc/CONTROLS_AND_MODES.md), with network provisioning and rollback in [NETWORK_TIME.md](../../doc/NETWORK_TIME.md). See [TESTING.md](../../doc/TESTING.md) for host tests and release acceptance, and [phone control](../../experiments/phone_control_ap/README.md) for browser setup and checks.

This is the current project firmware. See the [main README](../../README.md) for build dependencies and updates, and the [current pin map](../../doc/HARDWARE_PICO2W.md) for wiring.

## On-device verification

1. After flashing, run flash-info, alarm-status, audio-settings and bt-status.
2. Check LCD, RTC, both encoders, buttons, LOCAL speech and the timer.
3. Run bt-scan, wait for completion, then bt-devices and bt-use N.
4. Wait for Stream ACTIVE, leave the menu and wait for autosave=OK.
5. Restart the board: bt-known should retain the name/MAC and connection should require no scan.
6. Boot with the speaker off: the clock and LOCAL audio should continue working. Turn the speaker on and check reconnection.
7. Run bt-forget N and restart: the removed preferred device should not reconnect automatically.
8. Check MP3, speech, timer and alarm playback over Bluetooth, connection loss and independent output volume levels.

update-pico.ps1 automatically checks Flash and alarm responses, but does not replace sound and Bluetooth hardware tests. The relocated encoder, volume button, SD/FAT32 and MP3/Bluetooth have passed hardware tests; see [evidence](../../doc/HARDWARE_TEST_EVIDENCE.md). Registry v7 tests confirm discovery, connection, saving, Portable reconnection after power loss, deletion with autosave and Portable <-> Nest switching without scanning, followed by speech. Both switches recovered from initial status 0x56 through an automatic retry after five seconds.

## Controls and diagnostics

The left volume encoder adjusts the active output directly, including in settings. Its button toggles sound. LOCAL and Bluetooth levels remain separate. The timer encoder sets the duration. Its button starts or cancels the timer and confirms menu selections. See the [settings list](../../doc/CONTROLS_AND_MODES.md#settings).

The current development timer limit is 23:59:59 and birthdays have fifteen slots; stable v1.0.0 retains the earlier limits. Browser timer wheels and combined player controls are documented in the [phone guide](../../experiments/phone_control_ap/README.md). Alarm track Test/Stop uses `/player` actions `test_track 1..255` and `test_stop 0`; `player_test` identifies an audition. These commands do not apply the unsaved alarm draft. A general player Stop remains separate and closes either ordinary playback or an audition.

The web snapshot also contains a read-only `overview`: validated date/weekday, global sound, alarm/deferred/snooze forecast, Bluetooth display state/name and network/NTP metadata. The projection and bounded JSON encoder are in `services/phone_overview.c`; hardware reads stay in the main loop. `network_time_reference_status` distinguishes a received reference from successful RTC application. Existing controls/Flash encoding are unchanged. Approved installation passed startup/HTTP/settings checks; the user accepted the general overview.

Quick-control substage 2 adds authenticated `/control` actions `sound 0|1`, `say_time 0`, `say_date 0` and `next_alarm 0` to the shared command queue. `services/phone_control.c` centralizes admission guards; main-loop handlers reuse the physical setter/speech functions and report specific refusal results. Applied controls wait for the corresponding snapshot before browser confirmation. No Flash schema/save is introduced. This substage requires separate installation approval and hardware acceptance.

SETTINGS opens the menu, the timer encoder selects an item, SETTINGS or the timer encoder button confirms, and MINUS goes back. Run help for the complete USB command list. Serial speed: 115200.

## Storage

Clock Flash A/B occupies sectors -6/-5 from the end of Flash. Legacy sector -4 is read for migration. BTstack uses -3/-2, and the final sector is reserved. This branch reads CRC-protected v1-v14 records. Writes select v11 (348 bytes) with blank phone settings and no known birth years, v12 (504 bytes) with phone settings and no known birth years, v13 (512 bytes) when one of the first five birth years is known, or v14 (616 bytes) when slots 6-15 differ from their disabled defaults. v11-v13 use two programmed pages and v14 uses three in the existing clock sector. Loading does not rewrite Flash. Ordinary and `-NoWifi` builds from this branch preserve phone settings and all birthday entries. Stable v1.0.0 cannot read v12-v14; see rollback instructions before reverting. BTstack stores pairing keys separately.

A device is added only after an active A2DP stream with a matching MAC. Builds without Bluetooth preserve these fields when saving other settings. SPI SD access is read-only.

## History and limitations

The previous accumulated README is in the [development history](../../doc/history/PICO2W_C_DEVELOPMENT.md). Its early wiring and descriptions of incomplete stages are historical.

CMake flags CLOCK_ENABLE_A2DP, CLOCK_ENABLE_MP3 and CLOCK_ENABLE_WIFI are explicit options. The standard build script enables A2DP/Wi-Fi and adds MP3 with -Mp3; -NoWifi preserves a Bluetooth-only fallback. External LCD illumination on GP6 uses the Pico SDK hardware PWM library in every build. Classic A2DP/SBC with automatic SSP confirmation is supported; legacy PIN pairing is not. Helix is checked out separately at the revision documented in the main README.

`CLOCK_ENABLE_PHONE_WEB` defaults to OFF. `build-clock-bt.ps1 -Mp3 -PhoneWeb` enables the integrated local HTTP server and appends `-phone` to the build ID. It requires Wi-Fi and the pico-examples DHCP server. The web profile configures a 48 KiB lwIP heap and four POST contexts for four simultaneous clients; the ordinary profile retains 8 KiB. Four stored browser authorization entries are a separate limit. Hardware concurrency and audio continuity remain to be tested. Only the web profile adds Phone Control before About in the physical settings menu. Both home and direct modes serve the same embedded page; switching selects one network mode at a time.
