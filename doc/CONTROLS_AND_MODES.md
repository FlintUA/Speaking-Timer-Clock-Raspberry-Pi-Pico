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

While an alarm rings, ALARM stops it and volume remains adjustable. Timer/alarm/audio ownership can block settings or playback actions. GPIO events for other original front-panel labels may be logged without an assigned new action.

The precise timer editor starts from the selected duration. TIMER or SETTINGS advances HOUR -> MIN -> SEC; confirming SEC commits the duration and starts the countdown. MINUS goes to the previous field, or cancels from HOUR. GP28 and the 20-second idle timeout cancel without changing the selected duration. Values wrap within each field; the limit is exactly 04:00:00 and a zero duration starts as one second. The editor is unavailable during a running countdown, alarm, player or another menu. Quick minute adjustment discards sub-minute precision; use the editor for seconds. Consecutive same-direction detents less than 260 ms apart reach 5-minute steps on the third detent and 10-minute steps on the fifth. A pause or direction reversal resets acceleration.

## Settings

Turn the timer encoder to choose an item or value. SETTINGS or the timer encoder button enters/confirms. MINUS returns or cancels an uncommitted edit. Idle menus time out after 20 seconds; active Bluetooth discovery/name resolution is allowed to finish.

Items: Language, Time, Date, Quiet mode, Half-hour beep, Exact-hour word, Volume, Audio Output, Bluetooth and About.

About has two read-only pages: Speaking Clock / Pico 2 W, then the firmware version / by FlintUA. Turn the timer encoder in either direction to switch pages. MINUS returns to the settings list. The normal 20-second idle timeout applies. The build ID remains available through the USB version command.

Time/date writes occur only after confirming the final field. When network time is configured, later NTP synchronization applies Europe/Berlin time.

Audio Output contains independent preferences for manual speech, hourly voice, timer and alarm. LOCAL fallback is retained when Bluetooth is unavailable.

## Bluetooth

The list contains Scan devices, saved speakers, then newly found addresses not already saved. One MAC appears once. LCD labels show names; unknown names use Saved speaker or Unknown device. USB diagnostics retain MAC addresses.

Press to choose a speaker, then press again to confirm. Backing out or timing out after discovery resumes the preferred target. Connection establishment is asynchronous; only an active stream with the selected MAC makes a new target eligible for autosave.

USB commands: bt-scan, bt-scan-stop, bt-devices, bt-known, bt-use N|name|MAC, bt-target, bt-status, bt-forget N|MAC, bt-volume and bt-vol 0..30. Bare bt-forget prints usage and devices.

## Alarms

There are five daily alarm slots. The timer encoder selects a slot and changes editor fields: enabled, hour, minute, sound and music track. TIMER confirms/advances. In the track field, SETTINGS starts/stops preview rather than saving; MINUS goes back.

Music comes from folder 08. The editor uses the detected DFPlayer folder count, or the cached/default count if unavailable, and preserves any already selected higher track. Storage and explicit USB configuration support 1..255. Mirror the same music files onto the separate SPI card when using Bluetooth.

Existing alarms remain independent of quiet-hour automatic speech. Concurrent due events and timer priority use the existing deferred-alarm handling and safety timeout.

Snooze and weekday schedules are planned, not part of this release.

## Music player

Hold ALARM to enter while idle. The timer encoder chooses a track; TIMER or SETTINGS starts/stops it. MINUS or ALARM exits. The player uses a no-repeat shuffled playlist and advances after a completed track. Volume remains available.

The volume encoder button toggles sound in the player and alarm editor as well as on the main/settings screens. Muting stops active music on either output and shows MUTED in the player. Unmuting does not restart music; press TIMER to start it. Separate LOCAL/Bluetooth levels are retained. During a ringing alarm the existing policy is unchanged: use ALARM to stop it; rotating volume remains available.

The output follows the manual audio preference when entering the player. Bluetooth requires matching folder 08 files on the SPI FAT32 card. A lost Bluetooth link falls back to LOCAL. UI stop is not a position-preserving pause; separate mp3-pause/mp3-resume commands exist for direct playback diagnostics.

## Persistence, diagnostics and version

Committed changes use debounced Flash autosave when idle; wait for autosave=OK before removing power. The cooldown can defer an additional write. Run autosave-status or explicitly save when idle.

Use help for the complete USB command list. Important read-only commands: version, flash-info, alarm-status, audio-settings, bt-known, rtc-debug, audio-health and ntp-status. Serial speed is 115200.

See [network setup](NETWORK_TIME.md), [roadmap](ROADMAP.md) and [hardware evidence](HARDWARE_TEST_EVIDENCE.md) for current implementation and validation boundaries.
