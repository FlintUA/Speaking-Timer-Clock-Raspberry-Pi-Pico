# Wi-Fi/NTP time correction

The default Bluetooth build now includes Wi-Fi/NTP. The clock joins a configured 2.4 GHz network asynchronously, requests UTC from pool.ntp.org and writes Europe/Berlin local time to DS1302. Configuration is optional: without credentials, the clock keeps using its RTC.

## Local setup on Windows

Close any serial monitor. Build and flash the current firmware, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\configure-wifi.ps1 -Port COM8
```

The helper checks firmware support, prompts for SSID and password locally, sends them over USB and confirms saving. It does not display the password or put it in shell history. Credentials are stored in the device settings Flash; they are not encrypted. Do not publish serial-terminal input that contains credentials.

The current provisioning interface accepts an ASCII SSID of 1..32 characters, excluding |, and an empty password for an open network or an 8..64-character ASCII WPA2 key. The Pico wireless interface uses 2.4 GHz. For the 64-character PSK form, use hexadecimal digits.

Manual USB alternative:

```text
wifi-config YOUR_SSID|YOUR_PASSWORD
save
ntp-sync
ntp-status
```

Use actual values only in your local terminal. Do not send them to a public issue or chat. The first | separates SSID and password. The password may itself contain |.

## Behavior

- Wi-Fi connection attempts are asynchronous, with a 15-second join timeout and delayed retries.
- DNS/UDP NTP requests have a 10-second timeout. Failed requests retry after one minute.
- Successful samples are refreshed hourly.
- Reply validation checks server address/port, NTP version/mode, leap/stratum state, request nonce and a plausible supported timestamp. NTP is a conventional unauthenticated network time source.
- NTP callbacks stage a UTC sample. RTC writes run from the main loop only when timer, alarm, menus and audio are idle.
- Corrections of at most one second are treated as already synchronized.
- An ordinary significant correction consumes the current scheduler boundary rather than replaying missed announcements/alarms. The expected spring DST jump permits the new 03:00 boundary; after a backward correction, repeated civil time is suppressed until the previous clock time is reached.
- DST changes also wait for the same idle window as other RTC writes; they can be delayed while audio or a timer owns the device. Events in skipped minutes are not replayed.
- The monotonic countdown timer is independent of RTC adjustments.
- After an NTP sample, a RAM UTC anchor also allows DST conversion during a temporary network outage. On an offline reboot, DS1302 continues with its stored local time; a fresh NTP reference is needed to resolve a transition that occurred while the board was off.

Commands:
- ntp-status: configuration/radio/synchronization state and current UTC offset; no password.
- ntp-sync: request a fresh sample when the device is idle and online.
- wifi-clear: remove saved credentials, then wait for autosave=OK or use save.
- version: firmware version, source build ID and board.

Manual date/time editing remains available. Subsequent NTP synchronization corrects it to the network reference.

## Europe/Berlin

CET is UTC+1; CEST is UTC+2. Summer time starts at 01:00 UTC on the last Sunday of March and ends at 01:00 UTC on the last Sunday of October. The implementation uses these current rules for the supported RTC calendar, rather than hardcoded dates for one year.

References: [German federal time-change FAQ](https://www.bundeswirtschaftsministerium.de/Redaktion/DE/FAQ/zeitumstellung/faq-zeitumstellung.html), [EU 2022-2026 transition schedule](https://eur-lex.europa.eu/legal-content/DE/TXT/?uri=CELEX%3A52021XC0427%2801%29), [Raspberry Pi networking libraries](https://www.raspberrypi.com/documentation/pico-sdk/networking.html). A future legal change to time-zone rules would require a firmware update.

## Hardware acceptance

1. Capture version, flash-info, alarm-status, audio-settings and bt-known before changing network settings.
2. Provision the network and confirm save=OK.
3. Check Wi-Fi Connected, Validated UTC sample received and RTC synchronized messages.
4. Compare the LCD with a trusted Europe/Berlin clock.
5. Play speech/music on Portable and Nest while the network is enabled; check underruns and controls.
6. Restart and confirm network credentials, alarms, independent volume levels and known speakers survive.
7. Turn off the access point and confirm the clock continues operating, then restore it and check recovery.
8. Check scheduled events after a time correction. Host tests cover DST boundaries; actual RTC/network coexistence still needs device acceptance.

To build the previous Bluetooth-only radio profile, pass -NoWifi to build-clock-bt.ps1 or update-pico.ps1. The network credentials are preserved opaquely in that profile.

## Experimental phone-control network modes

The optional `-PhoneWeb` build on `codex/phone-control` serves the same local HTTP interface in either home STA mode or direct WPA2 AP mode. It requires Wi-Fi and does not run both modes concurrently. Home mode uses the saved network and NTP service; direct mode provides its own network at `192.168.4.1` and does not provide Internet access or fetch NTP. RTC-based clock operation continues. Returning home resumes asynchronous joining and normal NTP handling. The physical Phone Control screen shows join/address/retry progress and may remain open while joining; applying an NTP sample still waits until idle.

The integrated direct password is generated once and reused after a successful settings save, unlike the standalone AP prototype's per-boot password. Browser authorization is separate from the Wi-Fi key. The browser can remember pairing without an inactivity timeout; optional home trust permits unpaired clients on the home subnet. See [phone access and revocation](../experiments/phone_control_ap/README.md#timer-control-from-the-phone). Numeric HTTP addresses are used; no cloud service or Internet connection is required for browser control.

## Settings compatibility and rollback

Stable main/v1.0.0 writes Flash v11 without moving sectors. It adds display-light settings and retains alarm weekday masks, annual birthday dates and handled years, network, output, volume and speaker settings. Formats v1-v10 remain readable. Alarms from v1-v8 default to Daily, birthdays from v1-v9 default to disabled, and display illumination defaults to OFF when loading v1-v10. Loading alone does not migrate records. Settings changes and newly claimed birthday occurrences write v11 through deferred autosave. An explicit save writes immediately when the existing safety checks allow it. The 348-byte record still occupies two programmed pages in each clock sector. Once both A/B slots contain v11, earlier firmware can only read a legacy record if one exists. A `-NoWifi` build at v1.0.0 understands v11, but not the development formats below. Snooze and waiting birthday delivery are RAM-only.

The `codex/phone-control` branch reads v1-v14 and chooses a write format from the actual settings:

| Format | Selection and content | Record bytes |
| --- | --- | --- |
| v11 | Phone settings are blank, birth years are unknown and slots 6-15 remain at disabled defaults; stable baseline fields | 348 |
| v12 | Phone settings are present, birth years are unknown and slots 6-15 remain at disabled defaults; adds direct key, four scoped browser entries and home trust | 504 |
| v13 | At least one birth year in slots 1-5 is known and slots 6-15 remain at disabled defaults; retains phone fields | 512 |
| v14 | At least one of birthday slots 6-15 differs from its disabled default; retains the v13 prefix and adds ten dates and optional birth years | 616 |

Formats v11-v13 use two programmed 256-byte pages; v14 uses three pages in one existing 4 KiB A/B sector. The sector offsets, ownership checks, CRC and sequence selection do not change. v1-v12 load with unknown birth years; v1-v13 initialize slots 6-15 disabled. Loading alone never writes or migrates Flash. Both the ordinary profile and `-NoWifi` built from this branch read v12-v14 and preserve phone fields and known birth years even without the web server. A save with any nondefault slot 6-15 writes v14 to the alternate A/B sector, leaving the previous slot intact as fallback. Merely opening or viewing an extra slot does not select v14. If all extra slots later return completely to defaults, the writer can select v11-v13 again according to the remaining fields.

Stable v1.0.0 cannot read v12-v14. Earlier `codex/phone-control` builds cannot read v14; they may load an older A/B record and appear to lose slots 6-15 and later changes. Once both slots use a newer format, reverting to an older release can load an older legacy record or defaults rather than current settings. Record intended settings before rollback and use a v14-compatible build from this branch for a non-web/offline fallback. Do not equate disabling the web profile with reverting the source to v1.0.0. Snoozes and pending birthday playback remain RAM-only in every profile.

## Hardware acceptance status

On November 8, build 7964939 reported configured=YES, synced=YES and Europe/Berlin UTC+1 after local provisioning. Portable stayed A2DP ACTIVE at 44100 Hz, and the user confirmed Bluetooth playback with Wi-Fi enabled. Settings were saved in valid v10 A/B records. See [hardware evidence](HARDWARE_TEST_EVIDENCE.md#november-8-wi-fintp-and-bluetooth-coexistence). Hourly refresh, prolonged playback, access-point outage/recovery, provisioning after reboot and actual DST transitions remain acceptance checks. No offline drift rate has been measured or enabled.
