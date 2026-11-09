# Confirmed hardware test evidence

This summary covers the assembled Pico 2 W clock on COM8. Each section identifies its evidence source: direct COM8 verification, user tests or reconstructed historical project records. Software-only results and remaining physical checks are stated separately.

## External LCD illumination acceptance

Build ab06727 (1.0.0-rc.1+25.gab06727) was installed with separate approval. On first boot with the existing v10 records, COM8 reported `mode=OFF day=100% night=20% applied=0%`. Pre/post snapshots matched for all five alarms, all five birthdays, audio output preferences, both volume levels and the Bluetooth catalog and selected speaker. The Flash guard was OK, Portable resumed A2DP and NTP synchronized.

The user confirmed that illumination switched on and off, brightness adjustment worked, and there was no audible interference during the test. AUTO transitions at 07:00 and 22:00 and persistence after a v11 save and restart remain hardware checks.

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
