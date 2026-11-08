# Pico 2 W clock C firmware

Release candidate: 1.0.0-rc.1. Current controls are documented in [CONTROLS_AND_MODES.md](../../doc/CONTROLS_AND_MODES.md), with network provisioning in [NETWORK_TIME.md](../../doc/NETWORK_TIME.md). See [TESTING.md](../../doc/TESTING.md) for native tests and release acceptance.

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

The left volume encoder adjusts the active output directly, including in settings; its button toggles sound. LOCAL and Bluetooth levels remain separate. There is no separate Volume menu item; see the current [ten-item settings list](../../doc/CONTROLS_AND_MODES.md#settings). The timer encoder sets the duration; its button starts/cancels the timer and confirms menu selections.

SETTINGS opens the menu, the timer encoder selects an item, SETTINGS or the timer encoder button confirms, and MINUS goes back. Run help for the complete USB command list. Serial speed: 115200.

## Storage

Clock Flash A/B occupies sectors -6/-5 from the end of Flash; legacy sector -4 is read for migration. BTstack uses -3/-2; the final sector is reserved. CRC-protected v10 records retain alarm weekday masks, four names/MACs and Wi-Fi credentials and add five annual birthdays with handled years. Formats v1-v9 remain readable. v10 still uses two programmed pages within each existing clock sector. Old alarms retain their masks (daily for v1-v8), and older records load disabled birthdays. Loading does not rewrite Flash. BTstack stores pairing keys separately. Older v9/v8 firmware cannot read the newest records after both A/B slots migrate; use a current -NoWifi build for a compatible fallback.

A device is added only after an active A2DP stream with a matching MAC. Builds without Bluetooth preserve these fields when saving other settings. SPI SD access is read-only.

## History and limitations

The previous accumulated README is in the [development history](../../doc/history/PICO2W_C_DEVELOPMENT.md). Its early wiring and descriptions of incomplete stages are historical.

CMake flags CLOCK_ENABLE_A2DP, CLOCK_ENABLE_MP3 and CLOCK_ENABLE_WIFI are explicit options. The standard build script enables A2DP/Wi-Fi and adds MP3 with -Mp3; -NoWifi preserves a Bluetooth-only fallback. GP6 PWM is not implemented. Classic A2DP/SBC with automatic SSP confirmation is supported; legacy PIN pairing is not. Helix is checked out separately at the revision documented in the main README.
