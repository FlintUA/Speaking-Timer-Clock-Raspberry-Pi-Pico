# Changelog

## Unreleased

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
