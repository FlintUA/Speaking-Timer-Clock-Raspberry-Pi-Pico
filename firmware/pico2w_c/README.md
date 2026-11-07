# Pico 2 W clock C firmware

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

One volume encoder adjusts the active output; its button toggles sound. The timer encoder sets the duration; its button starts/cancels the timer and confirms menu selections.

SETTINGS opens the menu, the timer encoder selects an item, SETTINGS or the timer encoder button confirms, and MINUS goes back. Run help for the complete USB command list. Serial speed: 115200.

## Storage

Clock Flash A/B occupies sectors -6/-5 from the end of Flash; legacy sector -4 is read for migration. BTstack uses -3/-2; the final sector is reserved. CRC-protected v7 records store settings and four names/MACs, while v1-v6 remain readable. BTstack stores pairing keys separately.

A device is added only after an active A2DP stream with a matching MAC. Builds without Bluetooth preserve these fields when saving other settings. SPI SD access is read-only.

## History and limitations

The previous accumulated README is in the [development history](../../doc/history/PICO2W_C_DEVELOPMENT.md). Its early wiring and descriptions of incomplete stages are historical.

CMake flags CLOCK_ENABLE_A2DP and CLOCK_ENABLE_MP3 are opt-in. GP6 PWM is not implemented. Classic A2DP/SBC with automatic SSP confirmation is supported; legacy PIN pairing is not. Helix is checked out separately at the revision documented in the main README.
