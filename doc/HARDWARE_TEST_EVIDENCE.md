# Confirmed hardware test evidence

This summary covers the assembled Pico 2 W clock on COM8. It reconstructs results from project records and user tests; it is not a new hardware run.

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
