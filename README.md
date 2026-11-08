# Speaking Clock for Raspberry Pi Pico 2 W

The main project is C / Pico SDK firmware for **Pico 2 W (RP2350)**. MicroPython sources and the original Pico schematic are retained as historical material.

Current tagged release candidate: **1.0.0-rc.1**. Main also contains subsequent player, weekday/snooze and Flash v10/birthday changes. See [release notes](CHANGELOG.md), the [current roadmap](doc/ROADMAP.md) and [verification instructions](doc/TESTING.md). New features require on-device acceptance before the stable release.

Firmware versions are derived automatically from local Git release tags. About shows the tag version with + for later changes; USB version includes the commit metadata. See [version rules](doc/TESTING.md#automatic-firmware-version), including tag fetching and archive fallback.

The clock uses an I2C LCD1602, DS1302 RTC, DFPlayer Mini, two rotary encoders and five additional buttons. For Bluetooth playback, Pico reads MP3 files from a separate SPI1 microSD card, decodes them with Helix and sends SBC audio to a Bluetooth Classic A2DP speaker.

## Features and validation

- Countdown timer, five alarms with selectable weekdays and three-minute snooze, Russian and German speech, and quiet hours.
- Menus for time, date, language and audio outputs, with hardware blinking cursor focus in editors; USB diagnostics.
- Local DFPlayer and Bluetooth playback for speech, timer, alarms and music, with local fallback.
- Music player with normal/shuffle/repeat-one modes, Next/Previous history, position-preserving pause and a 15/30/60/90-minute sleep timer. Pause and sleep require hardware acceptance; see [controls](doc/CONTROLS_AND_MODES.md#music-player).
- Idle-clock MINUS short/hold requests spoken date/next alarm; a nonblocking startup screen shows the firmware version. The user confirmed this functionality after build 02b4a51 was installed.
- One volume encoder controls the active output. DFPlayer and Bluetooth volume levels are stored separately.
- Bluetooth discovery, selection by name/MAC, automatic SSP pairing, up to four known devices and direct connection to the saved MAC.
- Optional Wi-Fi/NTP correction with Europe/Berlin CET/CEST and offline RTC fallback.
- Five annual birthday dates with menu/USB editing and the existing melody; user-accepted configuration/save, with timed/audio coverage tracked in TESTING.md. One-time reminders are excluded.
- Flash settings v10 reads formats v1-v9, stores birthday dates/handled years, alarm weekday masks and Wi-Fi provisioning, and supports alarm music tracks through 255. BTstack stores pairing keys separately. See [migration and rollback](doc/NETWORK_TIME.md#settings-compatibility-and-rollback).
- Read-only FAT32 and MP3 access on the SPI microSD card.

Hardware tests confirm the relocated timer encoder, volume button, SPI SD/FAT32, MP3 reads and Bluetooth playback; see [test evidence](doc/HARDWARE_TEST_EVIDENCE.md). Registry v7 tests cover discovery, saving, Portable reconnection after power loss, deletion with autosave and Portable <-> Nest switching without scanning, followed by speech on both speakers. Switching initially returned status 0x56, then recovered through an automatic retry after five seconds; details are in the evidence summary. GP6 is reserved for external illumination; its PWM driver is not implemented.

The release candidate adds unique device lists, saved-name retention and return to the preferred speaker after leaving discovery. The initial switch retry behavior is deliberately retained. New code validation is recorded separately from those earlier hardware passes.

## Current wiring

Use the [Pico 2 W pin map](doc/HARDWARE_PICO2W.md). It takes precedence over the old schematic image and MicroPython mappings.

The timer encoder moved from DT=GP11 / CLK=GP10 to **GP12 / GP13**. SD SPI1 uses GP8-GP11. Encoder buttons are GP20 for volume and GP19 for the timer. The Pico and DFPlayer cards are independent.

## Updating on Windows

Close the serial monitor in your repository working directory. When first switching from develop/v3:

```powershell
git status --short
git fetch origin
git switch main
git merge --ff-only origin/main
```

Switch branches with a clean working tree after preserving local changes. If no local main exists, use `git switch --track origin/main`.

Subsequent updates:

```powershell
git pull --ff-only
powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Build -Bluetooth -Mp3
```

The default port is COM8; use `-Port COM7` to select another port. Software BOOTSEL entry requires firmware that already supports bootsel arm/confirm; the first installation requires manual BOOTSEL entry. The updater checks Flash and alarm responses after flashing, but does not test sound quality or pairing.

Run `version` to see the firmware version and source build ID. Keep the generated `speaking_clock_c.build-id.txt` beside its UF2; the updater uses it to verify the installed source revision.

## Dependencies and build

The script expects Pico SDK 2.3.1, pico-examples, CMake 4.3.4, ARM GCC, Ninja, pioasm and picotool under `%USERPROFILE%\.pico-sdk`. MP3 support requires:

```powershell
git clone https://github.com/Lefucjusz/Helix-MP3-Decoder.git third_party/Helix-MP3-Decoder
git -C third_party/Helix-MP3-Decoder checkout e4f5adadb5126708144e55a862f59e318cbffb51
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3
```

Building does not flash the board. Output: `firmware/pico2w_c/build-bt/speaking_clock_c.uf2`. On Windows, use a short path without non-ASCII characters, such as `C:\Users\Flint123\projects\Speaking-Timer-Clock-Raspberry-Pi-Pico`.

Wi-Fi is included by default. Provision credentials locally using:

```powershell
powershell -ExecutionPolicy Bypass -File .\configure-wifi.ps1 -Port COM8
```

See [network setup and time correction](doc/NETWORK_TIME.md). Use `-NoWifi` with the build/updater script for a Bluetooth-only fallback build.

## Bluetooth

1. Put the speaker in discovery mode, then select Scan devices in SETTINGS > Bluetooth or run `bt-scan`.
2. After scanning finishes, run `bt-devices`, followed by `bt-use N`, `bt-use Portable` or `bt-use AA:BB:CC:DD:EE:FF`.
3. Wait for `[BT-SELECT] stream ACTIVE`, leave the menu and wait for `[FLASH] autosave=OK`.
4. After restart, the clock connects directly to the saved MAC without scanning. Retry intervals are 5, 10, then 30 seconds.
5. `bt-known` shows saved names and pairing-key availability. `bt-forget N` removes a device and its key. Bare `bt-forget` prints usage and the device list.

Menu and command numbering lists known devices first, then new scanned addresses. Each MAC appears once. Devices with identical names require selection by number or MAC. When all four slots are occupied, the earliest added entry is replaced. Leaving discovery without selecting another speaker resumes the preferred target.

The LCD shows the speaker name. If unavailable, it shows Saved speaker or Unknown device. MAC addresses remain in USB logs for diagnostics and are used internally for identification and connection.

Supported: Classic A2DP Sink/SBC. BLE-only, HFP and Google Cast are not supported by this path. SSP confirmation is automatic; legacy PIN pairing is unsupported. If a speaker has forgotten its bond, remove the saved entry and pair again.

## Repository layout

| Path | Purpose |
| --- | --- |
| [firmware/pico2w_c](firmware/pico2w_c/) | Current C firmware and diagnostics |
| [doc/HARDWARE_PICO2W.md](doc/HARDWARE_PICO2W.md) | Current wiring |
| [doc/CONTROLS_AND_MODES.md](doc/CONTROLS_AND_MODES.md) | Current C firmware controls |
| [doc/NETWORK_TIME.md](doc/NETWORK_TIME.md) | Wi-Fi/NTP and Europe/Berlin |
| [doc/ROADMAP.md](doc/ROADMAP.md) | Prioritized current backlog |
| [doc/history](doc/history/) | Previous READMEs and development history |
| [source/v3](source/v3/) | Historical MicroPython firmware |
| [experiments/bluetooth_a2dp_nest](experiments/bluetooth_a2dp_nest/) | BTstack A2DP integration generator |
| [schematic](schematic/) | Original Pico schematic |
| [sound](sound/) | Audio assets |
| [Documentation conventions](doc/DOCUMENTATION.md) | English-only documentation policy |

## Licenses

Project: [GPL-3.0](LICENSE). External Helix sources are not included; the wrapper and core retain their GPL/RPSL/RCSL terms. Firmware distribution must also account for Pico SDK and BTstack licenses.
