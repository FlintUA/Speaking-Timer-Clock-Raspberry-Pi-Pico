# Current C firmware controls and modes

This document describes main, starting with 1.0.0-rc.1. The previous MicroPython controls document is retained in [history](history/MICROPYTHON_CONTROLS_AND_MODES.md). Use [HARDWARE_PICO2W.md](HARDWARE_PICO2W.md) for all pin assignments.

## Normal clock screen

| Control | Action |
| --- | --- |
| Volume encoder | Adjust the active audio output; LOCAL and Bluetooth levels are separate |
| Volume encoder button | Toggle sound on/off |
| Timer encoder | Adjust an idle countdown in minutes, with accelerated steps of 1/5/10 minutes, bounded to 1..240 minutes |
| Timer encoder button | Start or cancel the countdown |
| TIMER_MODE (GP28) | Open the hours/minutes/seconds editor; press again to cancel its draft |
| SETTINGS | Open settings |
| ALARM short press | Open/close the alarm editor |
| ALARM hold, at least 900 ms | Open the music player when idle |
| MODE short press | Switch VOICE/CHIME |
| MODE hold, at least 900 ms | Announce current time when idle |

While an alarm rings, ALARM stops it, TIMER_MODE/GP28 snoozes it for three minutes and volume remains adjustable. Timer/alarm/audio ownership can block settings or playback actions.

The precise timer editor starts from the selected duration. TIMER or SETTINGS advances HOUR -> MIN -> SEC; confirming SEC commits the duration and starts the countdown. MINUS goes to the previous field, or cancels from HOUR. GP28 and the 20-second idle timeout cancel without changing the selected duration. Values wrap within each field; the limit is exactly 04:00:00 and a zero duration starts as one second. The editor is unavailable during a running countdown, alarm, player or another menu. Quick minute adjustment discards sub-minute precision; use the editor for seconds. Consecutive same-direction detents less than 260 ms apart reach 5-minute steps on the third detent and 10-minute steps on the fifth. A pause or direction reversal resets acceleration.

## Settings

Turn the timer encoder to choose an item or value. SETTINGS or the timer encoder button enters/confirms. MINUS returns or cancels an uncommitted edit. Idle menus time out after 20 seconds; active Bluetooth discovery/name resolution is allowed to finish.

A hardware blinking character cursor marks the first character of the value being edited. The LCD controller generates the blink without periodic host commands; this is a blinking character cell rather than a blinking underline. It moves between hours/minutes/seconds or day/month/year, and marks the current scalar value in other editors. In the custom weekday editor it marks ON/OFF beside the day name. The same cue is used in the precise timer and alarm editors. Lists, About, music, ringing/snooze and ordinary clock screens hide the cursor; save, cancel and timeout also remove it. The user accepted the earlier steady underline mode; the subsequent hardware blinking mode was also accepted by the user in build 755f55b.

The settings menu has nine items, in this order:

1. Language
2. Time
3. Date
4. Quiet mode
5. Half-hour beep
6. Exact-hour word
7. Audio Output
8. Bluetooth
9. About

Volume is adjusted directly with the left encoder, including while settings are open; there is no separate Volume editor. The encoder follows the active audio output and retains independent LOCAL and Bluetooth levels. The encoder button toggles sound without replacing either stored level. USB vol N and bt-vol N remain available for explicit LOCAL and Bluetooth adjustment.

About has two read-only pages: Speaking Clock / Pico 2 W, then the firmware version / by FlintUA. Turn the timer encoder in either direction to switch pages. MINUS returns to the settings list. The normal 20-second idle timeout applies. The build ID remains available through the USB version command.

Time/date writes occur only after confirming the final field. When network time is configured, later NTP synchronization applies Europe/Berlin time.

Audio Output contains independent preferences for manual speech, hourly voice, timer and alarm. LOCAL fallback is retained when Bluetooth is unavailable.

LOCAL is the built-in DFPlayer output; BT is the currently selected external Bluetooth speaker, regardless of brand. Selecting Portable or Nest under Bluetooth changes the speaker, not these independent output preferences. Music follows Manual. USB commands audio-select bt, auto-audio bt, timer-audio bt and alarm-audio bt use the same preferences; the earlier jbl spelling remains a compatible alias. These are preferred outputs; a missing stream or file can trigger LOCAL fallback.

## Bluetooth

The list contains Scan devices, saved speakers, then newly found addresses not already saved. One MAC appears once. LCD labels show names; unknown names use Saved speaker or Unknown device. USB diagnostics retain MAC addresses.

Press to choose a speaker, then press again to confirm. Backing out or timing out after discovery resumes the preferred target. Connection establishment is asynchronous; only an active stream with the selected MAC makes a new target eligible for autosave.

USB commands: bt-scan, bt-scan-stop, bt-devices, bt-known, bt-use N|name|MAC, bt-target, bt-status, bt-forget N|MAC, bt-volume and bt-vol 0..30. Bare bt-forget prints usage and devices.

## Alarms

There are five alarm slots. The timer encoder selects a slot and changes editor fields: enabled, hour, minute, days, sound and music track. TIMER confirms/advances; SETTINGS also advances except in the track field, where it starts/stops preview rather than saving. Changes remain a draft until final confirmation.

Days offers Daily, Mon-Fri, Sat-Sun and Custom. In Custom, TIMER steps Monday through Sunday and turning the encoder toggles the displayed day. MINUS goes to the previous day, or back to the Days selector from Monday. At least one day must be selected. In the other alarm fields MINUS returns to the alarm list and discards the draft. The list shows the selected day preset; alarm-status includes the exact mask (bit 0=Monday, bit 6=Sunday).

During ringing, GP28 stops the current sound and schedules one repeat three minutes later. Repeated snooze is supported. The deadline uses monotonic uptime, so RTC/NTP corrections and midnight do not move it. At the deadline, the alarm enters the existing deferred-audio queue: a running countdown, mute or open editor may postpone it further, with the usual 30-minute expiry measured from that deadline. Ready snoozes preempt music through the normal alarm priority path. The repeat uses the original alarm time and current sound/output preferences.

The clock screen shows the next snoozed alarm and remaining time. ALARM cancels all snoozes when the main screen is active; ALARM during ringing still stops only the ringing alarm. Disabling or editing a slot cancels its pending/snoozed occurrence. Snoozes live only in RAM and disappear on restart; weekday schedules are saved in Flash.

Music comes from folder 08. The editor uses the detected DFPlayer folder count, or the cached/default count if unavailable, and preserves any already selected higher track. Storage and explicit USB configuration support 1..255. Mirror the same music files onto the separate SPI card when using Bluetooth.

Existing alarms remain independent of quiet-hour automatic speech. Concurrent due events and timer priority use the existing deferred-alarm handling and safety timeout.

USB: alarm-days N daily|weekdays|weekends|135 (1=Monday, 7=Sunday); alarm-snooze; alarm-snooze-status; alarm-snooze-cancel. For example, alarm-days 2 135 selects Monday/Wednesday/Friday for slot 2 without changing its enabled state or time. Existing alarm-set/alarm-config commands preserve the selected days. alarm-pending-clear clears both deferred occurrences and future snoozes without disabling schedules.

## Music player

Hold ALARM to enter while idle. Playback now starts automatically when sound is enabled, matching the earlier MicroPython player. Each opening starts in SHUF mode. Mode and navigation history live in RAM and are reset on opening; no settings-format change is required.

| Control in the player | Action |
| --- | --- |
| TIMER button | Pause/resume the current track; start the selected track when stopped |
| TIMER_MODE (GP28) | Cycle sleep OFF -> 15 -> 30 -> 60 -> 90 minutes -> OFF |
| SETTINGS or clockwise TIMER rotation | Next, immediately requests playback |
| MINUS or counterclockwise TIMER rotation | Previous selection from history, immediately requests playback |
| ALARM short press | Cycle SHUF -> REP1 -> NORM -> SHUF without interrupting the current track |
| ALARM, MODE or MINUS hold, at least 900 ms | Stop and exit |
| Volume rotation / button | Adjust active output / toggle sound |

NORM advances through track numbers and wraps at the end. SHUF plays every track once per uninterrupted forward cycle and delays the last seven selections until the tail of the next cycle (fewer for small libraries). REP1 repeats the current track at EOF; explicit Next can still skip it. Previous walks up to 128 selections, and Next can walk forward through that history. Manual navigation can intentionally replay tracks. Changing mode discards forward history. Without earlier history, Previous in NORM wraps by track number; in other modes it restarts the selected track.

MINUS acts on release: a short press selects Previous, while a hold exits without briefly starting a previous track. Both actions remain available.

The first LCD line shows mode and effective output, for example MUSIC SHUF BT. The second shows selected track/count, a compact PLAY/PAUS/STOP/WAIT/MUTE state and a fixed three-character sleep field. OFF explicitly means sleep is disabled, for example 001/045 PLAY OFF. When enabled, this field shows rounded-up remaining minutes, for example 001/045 PAUS 15m. WAIT represents a pending start while transport ownership or the preceding LOCAL pause command settles. Rapid navigation replaces the pending selection with the latest one. Muted navigation selects tracks without starting playback.

The volume encoder button toggles sound in the player and alarm editor as well as on the main/settings screens. Muting stops active music on either output and shows MUTED in the player. Unmuting does not restart music; press TIMER to start it. Separate LOCAL/Bluetooth levels are retained. During a ringing alarm use ALARM to stop or GP28 to snooze; rotating volume remains available.

The output follows the manual audio preference when entering the player. Bluetooth requires matching folder 08 files on the SPI FAT32 card. A lost Bluetooth link falls back to LOCAL and restarts the selected track when the local transport is idle. This does not advance the playlist. TIMER pause preserves the decoder and buffered PCM on Bluetooth, or sends the DFPlayer pause/resume commands without selecting the file again on LOCAL. UART commands remain paced; a rapid resume waits for its pause to be sent. This assumes the installed DFPlayer supports standard resume behavior and still requires hardware acceptance. Next/Previous, exit, mute and higher-priority interruption discard the saved position. If Bluetooth is lost while paused, playback remains paused; the next TIMER press restarts that track on LOCAL because positions cannot be transferred between the two cards.

The sleep timer counts elapsed time even while paused or muted. Selecting a preset starts a fresh deadline; OFF cancels it. It survives track/mode changes within the current player session, but closing the player or rebooting clears it. Expiry stops playback and returns to the clock without an announcement, without disconnecting Bluetooth and without changing alarm, countdown, output or volume settings. Pause and sleep are RAM-only and do not alter the Flash format. Direct mp3-pause/mp3-resume commands remain diagnostic controls, separate from player button state.

## Persistence, diagnostics and version

Committed changes use debounced Flash autosave when idle; wait for autosave=OK before removing power. The cooldown can defer an additional write. Run autosave-status or explicitly save when idle.

Use help for the complete USB command list. Important read-only commands: version, flash-info, alarm-status, audio-settings, bt-known, rtc-debug, audio-health and ntp-status. Serial speed is 115200.

See [network setup](NETWORK_TIME.md), [roadmap](ROADMAP.md) and [hardware evidence](HARDWARE_TEST_EVIDENCE.md) for current implementation and validation boundaries.
