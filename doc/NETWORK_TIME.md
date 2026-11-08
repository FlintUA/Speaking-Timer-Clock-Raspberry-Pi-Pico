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

## Settings compatibility and rollback

Current main writes Flash v10 without moving sectors. It retains v9 alarm weekday masks and adds five annual birthday dates plus their handled years. Existing v1-v9 records remain readable: old alarms keep their weekday masks (daily for v1-v8), and network, output, volume and speaker settings are retained. Birthdays default disabled when loading older records. Loading alone does not migrate records. Settings changes and newly claimed birthday occurrences write v10 through deferred autosave; explicit save writes immediately when the existing safety checks allow it. The 344-byte record still occupies two programmed pages in each clock sector. After both A/B slots contain v10, old v9/v8 firmware cannot read the latest settings and may fall back to an older legacy record. Use a current -NoWifi build for a network-free fallback that understands v10. Do not expect an old UF2 to restore settings changed under v10. Snooze and waiting birthday delivery are RAM-only; birthday dates and handled years are persistent.

## Hardware acceptance status

On November 8, build 7964939 reported configured=YES, synced=YES and Europe/Berlin UTC+1 after local provisioning. Portable stayed A2DP ACTIVE at 44100 Hz, and the user confirmed Bluetooth playback with Wi-Fi enabled. Settings were saved in valid v10 A/B records. See [hardware evidence](HARDWARE_TEST_EVIDENCE.md#november-8-wi-fintp-and-bluetooth-coexistence). Hourly refresh, prolonged playback, access-point outage/recovery, provisioning after reboot and actual DST transitions remain acceptance checks. No offline drift rate has been measured or enabled.
