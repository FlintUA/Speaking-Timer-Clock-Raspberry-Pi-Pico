# Confirmed hardware test evidence

This summary covers the assembled Pico 2 W clock on COM8. Each section identifies its evidence source: direct COM8 verification, user tests or reconstructed historical project records. Software-only results and remaining physical checks are stated separately.

## November 13 extended overview installation

The user separately approved installation of substage 1. The reviewed UF2 was 1,757,696 bytes, SHA-256 `1CEBED741FD86D248A0ABDAF7A6D38412AF64F4141AC443B4131A5A48FD78E95`. COM8 startup/build verification passed with `697c062da090-dirty-bt-mp3-wifi-phone`; image end was `0x100d67f4`. The identical dirty identity of earlier candidates makes the hash and new overview fields important for identification. The user subsequently reported that the overview was generally working correctly and asked about Bluetooth connection from the browser. This is broad acceptance, not individual confirmation of every ringing/snooze/network combination; browser Bluetooth management remains planned.

Flash guard remained OK; v12 slots retained valid sequences 152/153. Pre/post checksums matched for five alarms, fifteen birthdays and volume/language/output/light preferences. Wi-Fi returned in HOME mode and NTP synchronized. Read-only HTTP verified all new page IDs and overview data: 2026-11-13/Friday, sound ON, no active/deferred/snoozed alarm, next slot 4 at 15:00, Bluetooth OFF with no name, home network ready and a successfully applied NTP reference at UTC+1. No timer, audio, settings or alarm action was issued during these checks. COM8 was released. User phone rendering, physical state transitions and actual ringing/snooze display remain pending; no four-client/audio test is claimed.

## November 13 About installation

With separate user approval on November 13, the reviewed About UF2 was installed through COM8. SHA-256: `1F2A16509C308BB9FB9881D03C0E5CD2496E0FB0256E7C482F9018CC502551D6`, 1,742,848 bytes. USB reported `1.0.0+23.g697c062.dirty`, build `697c062da090-dirty-bt-mp3-wifi-phone`. The pre-install firmware shared that dirty identity, so the UF2 hash and newly served About metadata distinguish this installation. Image end changed from `0x100d457c` to `0x100d4afc`.

The updater verified startup and Flash guard OK. v12 A/B sequences 150/151 remained valid and unchanged. Home Wi-Fi and NTP recovered. Pre/post HTTP checksums matched for all five alarms, fifteen birthdays and the ten volume/language/output/light preferences. `/status.json` returned Speaking Clock, Pico 2 W board identity, FlintUA and the version/build matching USB. The served page contained the About disclosure and its metadata fields. These were read-only checks; no setting, timer or playback command was sent. COM8 was released. The user subsequently confirmed About worked on the phone. This accepts About, not the later expanded overview candidate.

## November 13 expansion user report

On 2026-11-13, the user reported that several recent improvements had been completed, tested and worked correctly, and requested documentation reconciliation and next-step planning. The inspected working tree is based on `697c062` with uncommitted firmware, tests, interface and documentation changes. No exact installed build ID or detailed test log accompanied that report, and no COM8 capture or new installation was performed during this documentation review.

The corresponding source contains fifteen birthdays, a 1..86,399-second countdown, three scrolling timer wheels with continuous digit scaling/fading, row-based Previous/Play-Pause/Next plus Stop, and alarm-track Test/Stop. The latest row layout is specified in WEB_CONTROL_SPEC.md and supersedes the earlier circular proposal. Record the user's report as broad functional acceptance; do not assign an invented clean build ID or claim that every listed feature was individually tested.

Separate checks remain for slot 15 persistence and timed delivery, v14 on real Flash after save/restart, optional-year persistence, a full-length countdown, both audio outputs, priority guards, audition completion/Stop after pause, and four simultaneous browsers in home/direct modes. Existing stable-release persistence evidence does not automatically close these new scenarios.

Software verification on this date: a clean MSVC Debug rebuild passed all seventeen registered CTest checks, including the production-script slider/audition and timer-wheel checks. The initial incremental run stalled in the timer-controls executable; it was stopped and the suite rebuilt cleanly before recording the passing result. This follows the existing warning about stale header-dependent objects. No firmware source was changed for this documentation task. This test run is not a real four-client HTTP or Bluetooth audio test.

## Phone-control acceptance through build 499e78c

Documentation reconciled on 2026-11-12. These results belong to the experimental `codex/phone-control` branch, not stable v1.0.0.

| Build | Evidence |
| --- | --- |
| `10c22e15f5e9-bt-mp3-wifi-phone` | User confirmed home access without repeated pairing and direct reconnection without re-entering the Wi-Fi password. |
| `b37ec35760a0-bt-mp3-wifi-phone` | User accepted the LCD progress indication when returning from direct to home mode. Exact transition duration was not measured. |
| `44ca0814caac-bt-mp3-wifi-phone` | User accepted browser player transport controls without observed problems. |
| `aa20a9cc4962-bt-mp3-wifi-phone` | User accepted browser settings. |
| `3d87e9900de5-bt-mp3-wifi-phone` | User accepted the general five-alarm/five-birthday editors and requested a single birthday date field. |
| `d51dd7adf5b8-bt-mp3-wifi-phone` | User found revised dates/mobile fields generally satisfactory, then requested volume/brightness sliders. This does not individually confirm known-year Flash persistence. |
| `499e78c94a0c-bt-mp3-wifi-phone` | User reported that everything worked after the four live sliders were installed. This is broad functional acceptance. |

For the last installation, the candidate UF2 was 1,722,880 bytes with SHA-256 `55A42206341DDD2B1E72ED8F94FA31A22B39BAE42A02BD5FF8F65B6D6F49F8AF`. The updater verified `1.0.0+22.g499e78c` and the exact build ID on COM8. SD/FAT32 and LCD initialized. Post-install diagnostics reported Flash guard OK, valid v12 A/B sequences 134/135, timer/alarm/player idle, no pending save, home network address present and NTP synchronized in Europe/Berlin. Bluetooth was inactive during this automated verification; it is not a BT playback test.

Pre/post serial checksums matched for all five alarm and five birthday rows. No browser setting command was sent during the automated check. Read-only HTTP returned four range inputs and saved levels LOCAL 12, BT 11, day 100%, night 100%. This confirms the served UI and unchanged existing configuration, not persistence of a newly edited birth year or slider value through another power cycle.

All sixteen host checks passed with Node.js available, and both web and ordinary profiles built. The production-script slider regression covers network/retry/conflict cases in simulation; a real mobile-width browser preview had no horizontal overflow. Remaining device acceptance includes two browsers/revocation, same-record conflicts, optional-year save/restart, sliders after save/restart and the explicit direct/home by LOCAL/BT combinations. See the [remaining checklist](../experiments/phone_control_ap/README.md#remaining-web-release-acceptance). Router outage, full AUTO light transitions and the intermittent Bluetooth double sound remain deferred.

## External LCD illumination acceptance

Build ab06727 (1.0.0-rc.1+25.gab06727) was installed with separate approval. On first boot with the existing v10 records, COM8 reported `mode=OFF day=100% night=20% applied=0%`. Pre/post snapshots matched for all five alarms, all five birthdays, audio output preferences, both volume levels and the Bluetooth catalog and selected speaker. The Flash guard was OK, Portable resumed A2DP and NTP synchronized.

The user confirmed that illumination switched on and off, brightness adjustment worked, and there was no audible interference during the test. In a separate restart test, the user confirmed that the chosen brightness returned and the alarms, birthdays, speaker and audio settings were retained. This is user-reported persistence on the v11-capable firmware. The Flash slot version was not read again after this restart. AUTO transitions at 07:00 and 22:00 are deferred.

## November 9 Bluetooth recovery, saved Nest selection and periodic NTP sync

On build 7378aa1 (1.0.0-rc.1+22.g7378aa1), the user turned JBL Link Portable off and back on while the clock remained powered. The user reported that Portable reconnected automatically in about one second after switch-on and played the clock's audio. The timing is a user estimate from this test.

In a separate user test, the clock was restarted with Google Nest Audio selected and the speaker left powered. The user reported immediate automatic reconnection and successful audio playback. This confirms that the preferred Nest selection survived a clock restart.

In a separate passive COM8 capture on the same build, NTP reported a successful RTC synchronization at 15:21:46 CET, before Portable connected. This confirms a periodic refresh under normal network conditions. Wi-Fi outage recovery and periodic synchronization during active Bluetooth playback remain unverified.

## November 9 additional Bluetooth audio devices

These are short user-reported checks on build 7378aa1.

| Device | Observed behavior |
| --- | --- |
| SilverCrest SSTB 10 C2 | Music played normally. The clock's volume control had no audible effect, while the speaker's own buttons worked. The user estimated that the first quarter-second of time and date announcements was clipped. |
| Logitech PRO X 2 | Playback worked, announcement beginnings were audible, and the clock's volume control worked. |

Absolute Volume support and the cause of the clipped speech on the SSTB 10 C2 remain unverified.

## November 9 timer completion at the hourly boundary

In the user-supplied COM8 log, the timer finished at 16:59:59 RTC and started two MP3 segments over Bluetooth. At 17:00:00 RTC, the scheduled announcement logged `skipped=TIMER_PRIORITY`. Both timer segments reported zero underruns and playback completed at about 17:00:10 RTC. The user heard the timer announcement only. This verifies timer priority at an hourly boundary. An idle hourly announcement and a scheduled alarm were not observed in this excerpt.

In a separate daytime test with Portable connected, the user confirmed normal hourly speech at 18:00 and a scheduled Daily alarm at 18:02. This result is user-reported and has no accompanying COM8 transcript.

## November 8 explicit Bluetooth disconnect acceptance

Build 7378aa1 (1.0.0-rc.1+22.g7378aa1) was installed with separate approval. Pre/post read-only COM8 snapshots confirmed identical five alarm configurations, five birthday entries, four output preferences, independent volume levels and both known speakers with paired=YES. Flash v10 A/B sequences 88/89 and the legacy record were retained; Flash guard was OK. Portable returned to A2DP ACTIVE at 44100 Hz under the previously enabled policy, and NTP reported configured=YES/synced=YES after reboot. COM8 was released.

The user first confirmed connecting and disconnecting work. They then reported disconnecting power, powering the clock again and performing a scan, with no automatic reconnection. This confirms persistent OFF across a power cycle and no automatic return in that tested post-reboot scan scenario. The report did not separately cover scan cancellation/timeout, speaker-initiated reconnect rejection, or later recovery after an accidental link loss. Those remain targeted checks in TESTING.md. The cause of the intermittent double speaker sound remains unconfirmed.

## November 8 Wi-Fi/NTP and Bluetooth coexistence

After local provisioning with configure-wifi.ps1 on build 7964939, a direct read-only COM8 check reported configured=YES, radio=READY and synced=YES for Europe/Berlin, with offset=3600 and last_sync_age=16s. RTC output continued. Portable remained A2DP ACTIVE at 44100 Hz. Flash guard was OK, both A/B records were valid v10 (sequences 88/87), and autosave was IDLE. Credentials were neither read back nor included in the diagnostic output. COM8 was released afterward.

The user then confirmed Bluetooth playback works with Wi-Fi enabled. This establishes initial NTP synchronization and basic audio coexistence, not an overnight run, hourly resynchronization, network-outage recovery, reboot persistence of provisioning, DST on hardware or a measured offline drift rate. The sanitized local transcript is build/diagnostics/wifi-first-status.log (ignored by Git).

## User acceptance before the three-minute snooze change

The user confirmed weekday alarm setup/firing and the delayed repeat work on the five-minute version. They then requested a shorter three-minute delay. After build 91d2fc7 was installed with separate approval, the user reported that it works correctly. This is user acceptance rather than a supplied timed measurement or complete serial transcript.

## November 8 LCD cursor acceptance

Build a0d5a9e (1.0.0-rc.1+11.ga0d5a9e, BT/MP3/Wi-Fi) was installed with separate approval. COM8 verification confirmed the exact build, Flash guard, unchanged five alarm entries, separate LOCAL/BT volumes, both known speakers and Portable A2DP ACTIVE at 44100 Hz. The user confirmed that steady underline focus works, then requested a more visible blinking cursor and selected hardware character-cell blinking. Build 755f55b (1.0.0-rc.1+12.g755f55b) was then installed with separate approval. COM8 confirmed the expected version, unchanged five alarm entries and output volumes, both saved speakers, and Portable A2DP ACTIVE. The user confirmed that hardware blinking is more visible. Build 4bad974 (1.0.0-rc.1+13.g4bad974) subsequently removed the redundant Volume menu item. It was installed with separate approval; COM8 confirmed its version, the same five alarms, separate volume levels, both saved speakers and Portable A2DP ACTIVE. The user then confirmed the updated menu works. Build 38182d4 (1.0.0-rc.1+15.g38182d4) added position-preserving player pause and the sleep timer. It was installed with separate approval; COM8 confirmed the version, unchanged five alarm entries, separate LOCAL/BT volume levels, both known speakers and Portable A2DP ACTIVE. The user reported that pause and sleep-time selection work and noted the blank sleep field at OFF. This report does not establish separate LOCAL/BT coverage or full 15-minute expiry. Build fa38eaf (1.0.0-rc.1+16.gfa38eaf) then added explicit OFF display and was installed with separate approval. COM8 verified its version, the same five alarms, LOCAL=16/BT=13, both known speakers and Portable A2DP ACTIVE. The user confirmed OFF is now clear. Full sleep expiry and output-specific pause coverage remain unconfirmed. Build 02b4a51 (1.0.0-rc.1+17.g02b4a51) then added spoken date, nearest-alarm information and the startup LCD/version splash. It was installed with separate approval; COM8 confirmed its version, the unchanged five alarms, both known speakers and Portable A2DP ACTIVE. The user reported that the new functionality works. This is broad user acceptance, not a supplied transcript covering every language/output, snooze forecast or missing-file fallback case.

## November 8 birthday firmware acceptance

Build 7964939 (1.0.0-rc.1+19.g7964939, BT/MP3/Wi-Fi) was installed with separate approval. The guarded updater confirmed the exact build, Flash guard and all five alarms. The subsequent user report was that the functionality works; a read-only COM8 snapshot also confirmed four user-enabled dates and one disabled slot, a valid v10 record (sequence 87) alongside the previous v9 fallback (sequence 86), unchanged alarm schedules/masks and audio preferences/levels, both saved speakers and Portable A2DP ACTIVE at 44100 Hz. Personal birthday dates are not reproduced here.

This confirms installation, UI configuration and a v10 save on hardware, plus broad user acceptance. It does not establish a separate timed birthday firing, yearly-repeat suppression across reboot, full 30-minute expiry, or audible LOCAL/BT melody coverage. Those remain focused coverage checks in TESTING.md. COM8 was released after the read-only check. Wi-Fi/NTP remained unconfigured; network synchronization and an offline drift rate have not yet been measured on this board.

## November 8 weekday/snooze firmware smoke test

Build 7119b5e (1.0.0-rc.1+7.g7119b5e, BT/MP3/Wi-Fi) was installed through the guarded updater. The exact build ID, Flash guard and alarm status were verified on COM8. All five pre-update enabled/time/sound/track fields were compared with the post-update snapshot and matched. Every old alarm loaded as Daily, mask 0x7f. LOCAL volume 16, BT volume 15, all four BT output preferences and both paired speakers remained present; Portable reconnected with A2DP ACTIVE at 44100 Hz.

A RAM-only test submitted alarm-audio-test 1 followed immediately by alarm-snooze. The log recorded BT alarm TEST start, STOP reason=SNOOZE_5_MIN, one sleeping snooze with remaining=300s, then successful alarm-snooze-cancel with count=0. Final audio-health showed no active timer, alarm, playback or pending occurrence. Saved schedules were unchanged. Flash A/B records remained v8 sequences 68/69, demonstrating that boot and snooze alone did not migrate/write settings.

This validates installation, v8 loading with daily defaults and USB snooze scheduling/cancellation. It does not validate physical GP28 operation, waiting the full five minutes, LCD custom-day editing, weekday firing on real dates, or v9 save/reboot on the board. Those acceptance steps remain in TESTING.md. Local transcripts: build/diagnostics/before-weekday-update.log, weekday-update.log and weekday-snooze-smoke.log (ignored by Git).

## November 8 passive COM8 Bluetooth capture

A direct, read-only 1800-second capture ran from 10:52:37 to 11:22:37 UTC (11:52:37-12:22:37 Europe/Berlin). The installed firmware reported 1.0.0-rc.1+4.g6d8beba.dirty, with Portable selected and A2DP ACTIVE at 44100 Hz. Manual/hourly/timer/alarm outputs all preferred BT; Wi-Fi was unconfigured. The speaker was connected to power, as confirmed by the user.

No stream pause/start/release, signaling release, reconnect, AVRCP disconnect or TONE event was recorded during this window. The 12:00 RTC hourly announcement ran at 11:01:19 UTC, completed with underruns=0, and returned to silence without restarting A2DP. A read-only status query after capture still showed A2DP ACTIVE/SILENCE, no music/timer/alarm and no pending save.

The user asked specifically about a possible double sound around 12:15. Neither the PC 12:14-12:16 CET interval nor the RTC 12:14-12:16 interval contains a non-RTC event. This is a text capture, not an acoustic recording: it cannot confirm whether the sound occurred or identify its source. Absence of logged profile transitions does not exclude speaker-local sounds or lower-level audio/controller issues. The cause remains unresolved.

The raw local capture is stored under build/diagnostics/bt-before-minus.log and is ignored by Git. This capture predates the additional MINUS-hold exit firmware; it does not validate that physical gesture.

## November 8 timer controls and output routing

The user log covering 11:23:45-11:26:50 confirms accelerated TIMER selection with 1/5/10-minute steps, reset to one-minute steps on reversal, a 60-second LOCAL countdown with start/finish audio, cancellation audio, GP28 editor entry and MINUS exit from its first field. It does not confirm the complete HOUR/MIN/SEC path or muting during music.

Portable connected and played folder 08 track 012 with underruns=0. Timer audio remained LOCAL because its output preference is independent. The user subsequently changed the timer output and confirmed it works on the external speaker. The new extended player and BT labels were implemented after this log and are not hardware-validated by it.

## About screen

The user confirmed the two-page About menu works after commit d8f2c57: Speaking Clock / Pico 2 W and firmware version / by FlintUA. This confirms the existing screen and controls; the later Git-tag version generator still requires a rebuilt-firmware check on the device.

## Encoders and buttons

The [November 6 audit](C_MIGRATION_AUDIT_2026-11-06.md#fat32-directory--file-read-diagnostics-implemented-2026-11-06) explicitly records user confirmation that the timer encoder works after relocation to GP12/GP13. Volume button GP20 was verified through sound ON/OFF in serial logs and on the LCD; no firmware change was required for that button.

The rotation signals moved from DT/CLK GP11/GP10 to GP12/GP13. Encoder buttons remain GP20 (volume) and GP19 (timer). The November 7 user log includes encoder TIMER CW/CCW and button TIMER PRESS/RELEASE with Bluetooth menu navigation and confirmation.

## SPI microSD

[Commit 85369eb](https://github.com/FlintUA/Speaking-Timer-Clock-Raspberry-Pi-Pico/commit/85369ebe2af2bbf4994ca601ce5b201ccf30d3ee) recorded a successful test on November 6, 2026 at 21:28:

```text
[SD] SPI1 initialized: SDHC/SDXC, bus=3947368 Hz, read-only
[SD] FAT32 detected at LBA 128, 7741440 sectors, sectors/cluster=8
[SD] PASS: SPI sector read + FAT32 boot sector
```

Wiring: GP8 MISO, GP9 CS, GP10 SCK, GP11 MOSI. The connected module is rated for 3.3 V, and the 4 GB FAT32 card contains copies of the DFPlayer files. See [HARDWARE_PICO2W.md](HARDWARE_PICO2W.md) for signal and module power connections.

At 21:39, the same [audit](C_MIGRATION_AUDIT_2026-11-06.md#full-sd-fat32-directory-and-file-lookup-verified-conservative-jbl-level-2026-11-06-2139) recorded 27 root directory entries, files in folder 07 and successful access to 07/001.mp3: 53103 bytes, first cluster 1476, ID3 header. The RTC kept running and A2DP was ACTIVE at 44100 Hz.

## MP3 and Bluetooth

The same audit records a November 7 test at 07:43 confirming MP3 reads from SPI/FAT32 and Helix decoding: 4096 PCM frames, 48000 Hz, 192000 bits/s.

In the [Pico2W clock chat](chatgpt-conversation://6aedc619-12a0-83eb-8dfe-f136af0ced90), the user supplied sd-list 08 output containing MP3 files and confirmed audible music on Nest, followed by successful connection and music on Portable.

The attached log in the [Pico2W clock, part 2 chat](chatgpt-conversation://6aef4851-6a7c-83eb-b02c-62756a5107b1) shows completed playback of /01/016.mp3 and /02/044.mp3, conversion from 48000 to 44100 Hz and underruns=0. It also records rotation of both encoders and button presses.

## Known devices v7 and reconnection: November 7 hardware test

The user identified two speakers: JBL Link Portable (name Portable, MAC 04:21:44:FA:A9:0E) and Google Nest Audio (name Nest).

The 19:25-19:27 log confirms discovery, Portable selection, A2DP/SBC at 44100 Hz, AVRCP, device acceptance after Stream ACTIVE and subsequent autosave=OK. The bt-devices command shows one known entry named Portable with paired=YES.

The user confirmed power disconnection and successful automatic reconnection. In the 19:30 log, COM8 disconnects and returns; the connection to 04:21:44:FA:A9:0E is restored, A2DP starts at 44100 Hz and AVRCP opens. The supplied excerpt contains no scan commands.

After reconnection, a manual 19:30 announcement successfully plays /01/019.mp3 and /02/030.mp3. Both segments are converted from 48000 to 44100 Hz and finish with underruns=0; the final result is BT-SPEECH COMPLETE, all segments delivered. The RTC continues running.

Discovery, connection, saving the selected Portable and reconnection after power loss are therefore hardware confirmed. Power disconnection is user confirmed; the log shows restored connectivity and subsequent audio.

## Deletion test and remaining checks

The user tested bt-forget 1 on November 7 at 19:40. Before deletion there was one Portable entry with paired=YES. The command printed Forgotten, A2DP and AVRCP disconnected, and routing switched to LOCAL. Five seconds later, autosave=OK appeared. A subsequent bt-known showed count=0 and an empty preferred target. The user confirmed successful deletion. This excerpt does not test whether deletion remains effective after another power cycle.

The current menu can show Portable twice: once as a known device and once as a scan result with the same MAC. This is duplicate presentation, not a second speaker.

## Two saved speakers: switching without scanning (November 7, 19:46-19:47)

The user confirmed switching between saved Portable and Nest without scanning. The supplied log shows Portable -> Nest, then Nest -> Portable: selection of entries 2 and 3 in the Bluetooth menu, direct MAC connection and successful A2DP/SBC at 44100 Hz plus AVRCP in both directions. No BT-SCAN starts in this excerpt.

Each switch is followed by a complete manual time announcement: 19:46 on Nest and 19:47 on Portable, two MP3 segments each, underruns=0. Nest has a recorded autosave=OK. At the end of the excerpt, saving the subsequent Portable selection is still pending, so this excerpt does not confirm persistence of that final selection.

For both switches, the first direct connection attempt immediately after release of the previous channel returned 0x56. In the installed BTstack, this is BTSTACK_MEMORY_ALLOC_FAILED. The automatic retry after 5000 ms returned 0x00 and restored the connection. A possible relationship to incomplete resource release from the previous channel needs separate investigation; the log does not establish the cause. The clock continued running during the wait.

The user separately confirmed that the LCD displays speaker names without MAC addresses. Remaining checks are persistence of deletion after power loss and booting with Nest selected. Portable reconnection after power loss is already verified.

## Quick controls: approved installation, November 13, 2026

After explicit user approval, the integrated phone/BT/MP3/Wi-Fi profile was installed through COM8. The reviewed UF2 was 1,766,912 bytes, SHA256 `EA6990E10B989BA309D00BEDE2D89F71970EAAAAD0A5B2B5E3F386AB23A0AC7F`, built from commit `38a8743a6cea3b2382570dafe93b4293cf18ae9a`.

Post-install USB and HTTP reported version `1.0.0+24.g38a8743` and build `38a8743a6cea-bt-mp3-wifi-phone`, without a dirty suffix. Both Flash slots remained valid with sequences 162/161 and schema v12; the image guard was OK. Home Wi-Fi returned at `192.168.2.116`, NTP reported synchronized Europe/Berlin time, and Bluetooth recovered an ACTIVE 44100 Hz stream. No timer, alarm, MP3 or speech was active during verification.

Read-only HTTP checks confirmed the overview and all five quick-control element IDs. Five alarms, fifteen birthday records and ten persistent preference fields produced the same pre/post SHA256: `E25CFB7A3D7C26410D9DD8FFD51DECD420D762A80F7E1602B8765D63001A75A0`. The idle, unsaved timer selection reset from 540 to 60 seconds on reboot; this was not an active countdown or a persistent preference.

No HTTP control actions or audible test commands were sent during post-install verification. COM8 was closed and released. User acceptance of sound on/off and time/date/nearest-alarm speech is pending, as are the separately listed four-client/audio-load cases. Stable main and the v1.0.0 release were not changed.

### Quick-control user acceptance and deferred wording

After installation, the user reported that everything works. This is general acceptance of the tested quick controls, not individual evidence for every route, language, failure case or four-client load. The user specifically confirmed that Speak next alarm refers to the upcoming alarm, but reported a fired-alarm introduction before its date and time. The user deferred phrase replacement until functional development is complete and audio assets are refreshed. No firmware or audio change, rebuild or installation was performed for this feedback.

## Live alarm controls: approved installation, November 13, 2026

After explicit user approval, the integrated phone/BT/MP3/Wi-Fi UF2 from commit `97fd95185e3f26cb010e03e30336738b36456d24` was installed through COM8. Artifact: 1,774,592 bytes; SHA256 `40E22B5221059E507C6D4C49518C316537D8F7273C8072F57F47B13C79E60AE5`. The updater reported SUCCESS. USB and HTTP identify clean version `1.0.0+25.g97fd951`, build `97fd95185e3f-bt-mp3-wifi-phone`.

Startup verified SD/FAT32, LCD, RTC and inputs. Flash guard was OK with image_end `0x100d883c`; settings slots remained valid at sequences 162/161, schema v12. Five alarms, fifteen birthdays and ten persistent preferences have identical pre/post checksum `E25CFB7A3D7C26410D9DD8FFD51DECD420D762A80F7E1602B8765D63001A75A0`. Home HTTP returned at `192.168.2.116`; NTP was synchronized for Europe/Berlin and the Bluetooth stream was ACTIVE at 44100 Hz after restart.

Read-only HTTP confirmed the three new alarm actions and runtime IDs: no active alarm, active_id=0, no snoozes, snooze_revision=1. No control action or audible command was sent and no schedule was edited during verification. COM8 was closed/released. Physical stop, three-minute repeat and cancellation behavior remain for user acceptance; four-client/audio-load evidence remains separate. The final clean-identity serializer fixture used 3,644 bytes of its 4,096-byte HTTP buffer (452 bytes free).

### Clarification after live alarm installation

The user reported inactive alarm-action buttons while a short countdown completion announcement was playing, and an apparent page refresh/jump. On clarification, the tested event was a countdown timer, not a scheduled alarm. This does not validate or invalidate the separately installed scheduled-alarm Stop/Snooze actions, which remain pending physical acceptance. Inspection confirmed that the existing web Cancel timer did nothing after countdown.running became false. The page polls JSON and has no full-page reload operation; the cause of the apparent jump is not established. A separate completion-audio Stop fix is prepared but has not been installed. No device schedules were changed and no audible device commands were sent during this investigation.

## Scheduled-alarm snooze/cancellation acceptance and timer fix installation

On November 13, the user confirmed the scheduled alarm repeated after three minutes and that cancellation worked. This is user acceptance of those reported actions on the preceding installed `1.0.0+25.g97fd951`, not individual evidence for every output, concurrency case or stale-request scenario.

With separate explicit approval, the countdown completion-audio fix from commit `3717033d06c48c6a82381372d03bc06f76f33847` was installed through COM8. Reviewed UF2: 1,777,664 bytes, SHA256 `DC4B326319887B1BC98C23AADEEA29505B0748707C43B178644B493F3024A5EC`. The updater reported SUCCESS; USB and HTTP identify clean version `1.0.0+26.g3717033`, build `3717033d06c4-bt-mp3-wifi-phone`. Flash guard was OK, image_end `0x100d8ea4`, and the two settings slots remained valid at sequences 170/171, schema v12.

Five alarms, fifteen birthday records and ten persistent preferences match their immediately pre-install checksum `B418CC407F1AC737DDCB0129194A78C2A94E9212B847ECF6AAEBABAC65387BC4`. This checksum differs from earlier installations because the user changed settings during testing; it is not an installation-induced difference. The idle unsaved timer selection reset from five seconds to the default sixty seconds on restart. Home HTTP returned at `192.168.2.116` and Europe/Berlin NTP synchronized. Bluetooth remained OFF under the saved explicit-connect policy; no connection was initiated by the verification.

Read-only HTTP confirmed Stop timer sound and timer_sound_id=0, with no active alarm or pending snoozes. No web control or audible test command was sent. COM8 was closed/released. User testing of the timer completion stop, observed latency and reported page jump remains pending.

### Countdown completion stop: user acceptance

After installation of `1.0.0+26.g3717033` and the requested countdown-stop check, the user confirmed that it works correctly. This closes general acceptance of the separate Stop timer sound fix. The report does not include measured response latency, individual LOCAL/Bluetooth runs, stale-request/concurrent-client testing or a separately isolated cause for the earlier page jump. Scheduled-alarm three-minute repeat and cancellation were separately accepted in the preceding report.

## Independent timer completion choices: approved installation, November 13

After explicit approval, the integrated web/BT/MP3/Wi-Fi image from commit `e5593d11ac93d5aca3ada52fe1fd44ba42f85824` was installed through COM8. Reviewed UF2: 1,786,368 bytes, SHA256 `8E0F2CF1D00353C5DCDE9F8E1C5882A2F663D29523B2CEB950AC99BD5B928A0E`. The updater reported SUCCESS. USB and HTTP identify clean version `1.0.0+27.ge5593d1`, build `e5593d11ac93-bt-mp3-wifi-phone`.

Startup checked SD/FAT32, LCD, RTC, radio and inputs. Flash guard was OK with image_end `0x100d9f04`. Both settings slots remain valid at sequences 172/173, schema v12: merely loading this build did not migrate or save them. Five alarms, fifteen birthday records and ten existing persistent preferences match their immediately pre-install checksum `B418CC407F1AC737DDCB0129194A78C2A94E9212B847ECF6AAEBABAC65387BC4`. The unsaved idle timer selection reset from five to sixty seconds on reboot.

Read-only HTTP verified the new checkbox/select/track controls and defaults timer_voice=1, timer_sound=0, timer_track=1. Home HTTP returned at `192.168.2.116`, Europe/Berlin NTP synchronized, and Bluetooth remained OFF under the saved policy. No connection, timer, audio or settings command was sent during verification. COM8 was closed/released. Actual combinations, selected-card music, physical menu edits, Stop timer sound and v15 persistence remain for user testing. The final clean-identity serializer fixture used 3,722 bytes of the 4,096-byte HTTP buffer (374 bytes free).

## Random event music: approved installation, November 13

After explicit user approval, the integrated phone/BT/MP3/Wi-Fi image from commit `36286e9c23276c1f0a83814d9e4b267ee8014858` was installed through COM8. Reviewed UF2: 1,792,512 bytes, SHA256 `8DAE8BE21EDB1C87BE5B3608DEB390A190F81A3CCB6D13CF123A25BFE6F77914`. The updater reported SUCCESS. USB and HTTP identify clean version `1.0.0+28.g36286e9`, build `36286e9c2327-bt-mp3-wifi-phone`.

Startup cached 78 numeric folder-08 music files from the SPI FAT32 card before radio startup. This verifies discovery/catalog availability on that card, not equality with the DFPlayer card or decoding of every file. LCD, RTC, radio and inputs initialized. Flash guard was OK with image_end `0x100dab64`; settings slots remained valid at sequences 174/173, schema v12. Installing/loading did not migrate records or enable random mode.

Five alarm records (enabled/time/days/sound/fixed track), fifteen birthdays and thirteen previous persistent preferences match their pre-install checksum `685E629D03B0B2D2E34A95ECBB3D1D13044505060DF2BFEEE5C9D3F6C8DB0449`. The checksum excludes alarm API revision tokens because their lossless bit allocation changed to represent RANDOM without collisions. New timer_random defaulted to 0; timer_voice=1, timer_sound=0 and timer_track=1 were retained. Home HTTP returned at `192.168.2.116`, Europe/Berlin NTP synchronized and Bluetooth remained OFF under the saved policy.

Read-only HTTP confirmed timer_random controls and random alarm selection. No connection, timer, alarm or settings action was sent; COM8 was closed/released. Actual random playback, per-occurrence selection, same-track snooze, LOCAL/BT output and v16 save/power-cycle persistence remain for user testing. The final clean-identity full-status fixture used 3,739 bytes of 4,096 (357 free).

### Random-music feedback and pre-warning asset confirmation

After the random-music installation the user reported correct operation. This is general acceptance, without individual per-slot, output or persistence transcripts. The user later confirmed that both cards are identical and music numbering is contiguous. Sparse numeric catalog support is retained. For the proposed five-second warning, the user identified beep_01.mp3 as a short single beep and beep_02.mp3 as the half-hour double beep. The supplied screenshot shows the original beep_01.mp3 at 11,612 bytes. The repository's mapped 19/001.mp3 has the same size and local ffprobe duration 0.197333 seconds (48 kHz mono). No audio asset was edited, no settings/audio command was sent to the device, and the new warning has not yet been installed or accepted on hardware.
