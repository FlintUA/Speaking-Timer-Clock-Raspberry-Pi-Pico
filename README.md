# Speaking Clock for Raspberry Pi Pico 2 W

The main project is C / Pico SDK firmware for **Pico 2 W (RP2350)**. MicroPython sources and the original Pico schematic are retained as historical material.

Current stable release: **[1.0.0](https://github.com/FlintUA/Speaking-Timer-Clock-Raspberry-Pi-Pico/releases/tag/v1.0.0)**. See the [release notes](doc/releases/v1.0.0.md), [current roadmap](doc/ROADMAP.md) and [verification instructions](doc/TESTING.md).

This checkout's `codex/phone-control` branch adds experimental browser control. It has not replaced the stable release. The optional web profile supports direct Wi-Fi or the home network, live status, one 1..86,399-second timer, combined Play/Pause transport controls, settings, five alarms and fifteen annual birthdays with optional birth years. It is designed for four simultaneous browser clients; this requires hardware acceptance. See [phone setup, controls and acceptance](experiments/phone_control_ap/README.md). The ordinary profile does not serve the web interface.

Firmware versions are derived automatically from local Git release tags. The LCD About screen shows a compact version with + for later changes. Browser About shows the full installed version and build ID, using the same generated constants as USB `version`, plus the device, board and author FlintUA. Browser information is read-only and does not require pairing. See [version rules](doc/TESTING.md#automatic-firmware-version), including tag fetching and archive fallback.

The clock uses an I2C LCD1602, DS1302 RTC, DFPlayer Mini, two rotary encoders and five additional buttons. For Bluetooth playback, Pico reads MP3 files from a separate SPI1 microSD card, decodes them with Helix and sends SBC audio to a Bluetooth Classic A2DP speaker.

## Features and validation

- One countdown timer (1..86,399 seconds on this development branch), five alarms with selectable weekdays and three-minute snooze, Russian and German speech, and quiet hours.
- Menus for time, date, language and audio outputs, with hardware blinking cursor focus in editors; USB diagnostics.
- Local DFPlayer and Bluetooth playback for speech, timer, alarms and music, with local fallback.
- Music player with normal/shuffle/repeat-one modes, Next/Previous history, position-preserving pause and a 15/30/60/90-minute sleep timer. Pause and sleep selection were user-tested. Full timed expiry remains a follow-up check. [Controls](doc/CONTROLS_AND_MODES.md#music-player)
- Idle-clock MINUS short/hold requests spoken date/next alarm; a nonblocking startup screen shows the firmware version. The user confirmed this functionality after build 02b4a51 was installed.
- One volume encoder controls the active output. DFPlayer and Bluetooth volume levels are stored separately.
- Bluetooth discovery, selection by name/MAC, automatic SSP pairing, up to four known devices and direct connection to the saved MAC.
- Optional Wi-Fi/NTP correction with Europe/Berlin CET/CEST and offline RTC fallback.
- Fifteen annual birthday dates with menu/USB/browser editing and the existing melody on this development branch. The user broadly accepted the latest changes on 2026-11-13; individual expanded-slot persistence and timed-delivery checks remain in the acceptance checklist. One-time reminders are excluded.
- Stable v1.0.0 uses Flash settings v11. This development branch reads v1-v14 and writes v11-v14 according to the saved fields: v12 adds remembered browser access, direct Wi-Fi password and home trust; v13 adds known birth years for the first five slots; v14 adds ten birthday slots. Sector boundaries are unchanged. Both profiles on this branch preserve these fields. Alarm music tracks up to 255 are supported. BTstack stores pairing keys separately. See [migration and rollback](doc/NETWORK_TIME.md#settings-compatibility-and-rollback).
- External LCD illumination on GP6 supports OFF, ON and AUTO with independent day and night brightness. See [settings](doc/CONTROLS_AND_MODES.md#settings).
- Read-only FAT32 and MP3 access on the SPI microSD card.

The current browser timer uses three scrolling hours/minutes/seconds wheels with smooth digit scaling and keyboard support. Player controls are Previous / combined Play-Pause / Next, with Stop below; the later user-selected row layout replaces the earlier circular proposal. The alarm editor offers Test/Stop to audition one selected music track without saving its draft. See [current implementation status](doc/ROADMAP.md#current-expansion-status-2026-11-13) and the [expansion specification](doc/WEB_CONTROL_SPEC.md).

The generally accepted status substage adds device-supplied date/weekday, global sound, active/deferred/nearest alarm and snoozes, Bluetooth name/state and home/direct Wi-Fi/NTP. Quick-control substage 2 adds explicit sound on/off and time/date/nearest-alarm speech; software checks pass and approved installation/startup checks passed on November 13; the user subsequently confirmed general operation, with next-alarm wording deferred for the planned audio refresh. Live alarm Stop, Snooze 3 min and Cancel snoozed repeats are implemented as substage 3, installed with approval on November 13 as `1.0.0+25.g97fd951`; hardware acceptance is pending. Browser Bluetooth device management remains a later stage. Selecting a preferred Bluetooth audio output does not connect a speaker.

Hardware tests confirm the relocated timer encoder, volume button, SPI SD/FAT32, MP3 reads and Bluetooth playback. Registry v7 tests cover discovery, saving, Portable reconnection after power loss, deletion with autosave and Portable <-> Nest switching without scanning, followed by speech on both speakers. Switching initially returned status 0x56, then recovered through an automatic retry after five seconds. See [test evidence](doc/HARDWARE_TEST_EVIDENCE.md).

Disconnect or starting a scan persists OFF until an explicit Connect while retaining saved speakers and pairing. The initial switch retry remains in place.

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

### Experimental phone-control profile

Use the existing `codex/phone-control` branch with a clean working tree. Build the web profile separately; the updater has no `-PhoneWeb` parameter:

```powershell
git fetch origin --tags
git switch codex/phone-control
git pull --ff-only
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -PhoneWeb -BuildDirectory C:\pico_clock_compile\phone-clock
```

Building does not flash the board. After reviewing the build and approving installation, close serial monitors and install that exact artifact:

```powershell
powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Port COM8 -Uf2 C:\pico_clock_compile\phone-clock\speaking_clock_c.uf2
```

Keep the generated build-ID sidecar with the UF2. Do not use the ordinary `-Build -Bluetooth -Mp3` update command to install the web profile: it configures the ordinary profile. A web build requires Wi-Fi, so `-PhoneWeb -NoWifi` is rejected. Before reverting to stable v1.0.0, read the settings compatibility notes; it cannot read v12-v14 records.

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

Menu and command numbering lists known devices first, then new scanned addresses. Each MAC appears once. Devices with identical names require selection by number or MAC. When all four slots are occupied, the earliest added entry is replaced. Disconnect is the last Bluetooth menu action; it retains devices/pairing and disables reconnect through saved OFF. Starting discovery also disables reconnect. Exiting, cancelling or timing out does not restore the old speaker. Choose and confirm a device to reconnect. Close settings and wait for autosave=OK before power-off. USB bt-disconnect provides the same action; bt-connect MAC is a compatibility alias for explicit selection via bt-use. Unplanned link loss still retries when a target is selected. The user confirmed connection/disconnection, persistent OFF after a power cycle and no automatic return after a subsequent scan on build 7378aa1. Separate cancel/timeout, incoming-peer and link-loss recovery checks remain in TESTING.md.

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
| [experiments/phone_control_ap](experiments/phone_control_ap/) | Phone-control instructions, acceptance evidence and standalone network prototypes |
| [schematic](schematic/) | Original Pico schematic |
| [sound](sound/) | Audio assets |
| [Documentation conventions](doc/DOCUMENTATION.md) | English-only documentation policy |

## Licenses

Project: [GPL-3.0](LICENSE). External Helix sources are not included; the wrapper and core retain their GPL/RPSL/RCSL terms. Firmware distribution must also account for Pico SDK and BTstack licenses.

## Countdown completion audio (fix; installation pending)

Countdown timers and scheduled alarms are separate. Alarm Stop/Snooze actions do not apply to countdown completion. Cancel timer cancels a running countdown. The new **Stop timer sound** action stops only the currently owned completion announcement, without a cancellation phrase or changing saved schedules/levels. It is enabled while that announcement is active; no manual page refresh is required. The status shows **Finished - announcement playing**. Actual on-page punctuation may use a separator dot.

The `/timer` action `stop_sound <timer_sound_id>` uses the existing credential/epoch/id/nonce envelope and a nonzero ID from `overview.timer_sound_id`. Ended or superseded events are refused; another alarm, player or later timer event cannot be stopped by an old ID. The owner is cleared when audio completes, is replaced/appended by another owner, or is interrupted by mute/priority. Bluetooth-to-LOCAL fallback retains the same event ID. No Flash-format change or save is introduced.

The page requests status every 250 ms during the last five countdown seconds and while completion audio or a scheduled alarm is active, versus the usual 1 second (plus response time). This improves observation latency; it does not guarantee immediate response under radio/decoder load. Controls remain state-based. If completion has already ended, Stop timer sound is disabled. Hardware timing and the reported page jump remain unverified; the page contains no full-page reload operation.
