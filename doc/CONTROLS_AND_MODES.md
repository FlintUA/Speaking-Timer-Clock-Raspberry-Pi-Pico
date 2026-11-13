# Current C firmware controls and modes

This document describes the current C firmware working tree on `codex/phone-control`, including the fifteen-birthday and 23:59:59 timer expansion. Stable v1.0.0 retains five birthdays and the four-hour timer limit. The previous MicroPython controls document is retained in [history](history/MICROPYTHON_CONTROLS_AND_MODES.md). Use [HARDWARE_PICO2W.md](HARDWARE_PICO2W.md) for all pin assignments.

## Normal clock screen

| Control | Action |
| --- | --- |
| Volume encoder | Adjust the active audio output; LOCAL and Bluetooth levels are separate |
| Volume encoder button | Toggle sound on/off |
| Timer encoder | Adjust the single idle countdown in minutes, with accelerated steps of 1/5/10 minutes, bounded to 1..1439 minutes; use the precise editor for seconds through 23:59:59 |
| Timer encoder button | Start or cancel the countdown |
| TIMER_MODE (GP28) | Open the hours/minutes/seconds editor; press again to cancel its draft |
| SETTINGS | Open settings |
| MINUS short press on release | Show and announce the date while the clock is idle |
| MINUS hold, at least 900 ms, on release | Show and announce the next scheduled/snoozed alarm while the clock is idle |
| ALARM short press | Open/close the alarm editor |
| ALARM hold, at least 900 ms | Open the music player when idle |
| MODE short press | Switch VOICE/CHIME |
| MODE hold, at least 900 ms | Announce current time when idle |

While an alarm rings, ALARM stops it, TIMER_MODE/GP28 snoozes it for three minutes and volume remains adjustable. Timer/alarm/audio ownership can block settings or playback actions.

The precise timer editor starts from the selected duration. TIMER or SETTINGS advances HOUR -> MIN -> SEC; confirming SEC commits the duration and starts the countdown. MINUS goes to the previous field, or cancels from HOUR. GP28 and the 20-second idle timeout cancel without changing the selected duration. Values wrap within each field; the limit is 23:59:59 and a zero duration cannot be confirmed. The editor is unavailable during a running countdown, alarm, player or another menu. Quick minute adjustment normally discards sub-minute precision and clamps at 1..1439 whole minutes; use the precise editor for seconds. A clockwise detent at an already selected 23:59:59 leaves that maximum unchanged. Consecutive same-direction detents less than 260 ms apart reach 5-minute steps on the third detent and 10-minute steps on the fifth. A pause or direction reversal resets acceleration.

## Date, next alarm and startup

MINUS short press on the idle clock shows the full date for four seconds and requests speech in the selected language. MINUS hold (at least 900 ms) shows the nearest strictly future enabled alarm occurrence, honoring weekday masks and calendar boundaries; equal scheduled times choose the lowest slot. Pending RAM snoozes are compared against the weekly schedule and can win. Already-due deferred alarm playback is separate from this forecast. With no eligible occurrence, LCD shows NEXT ALARM / None enabled and does not request a nonexistent spoken phrase. USB say-date and next-alarm request the same information.

Date speech uses weekday, day, month and an available year recording. Weekday is computed from the date rather than trusting the RTC weekday field. RU uses folders 06/03/04/05; DE uses 16/13/14/15. Year recordings cover 2023..2029 (tracks 003..009). Other years are fully shown on LCD but the spoken year is omitted, with a USB explanation. Next-alarm speech adds the Russian alarm-slot phrase 07/001..005, date and time. German has no confirmed slot-number recordings, so it speaks date/time and leaves the slot number on LCD. SPI and DFPlayer cards must both contain the numbered date folders for equivalent routing/fallback.

Information speech follows the Manual output preference and the existing Bluetooth sequence/LOCAL fallback. Mute, playback, timer/alarm and other audio owners suppress speech rather than interrupting them. LCD information remains available; physical MINUS actions retain their existing meanings in menus, music and alarm editors. No alarm or settings values are changed by these requests. A USB request while another screen owns the LCD may remain hidden until the four-second overlay expires.

Startup shows Speaking Clock and the compact tag-derived firmware version for about 1.6 seconds after initialization. This is a nonblocking LCD indication: RTC, Bluetooth, controls and schedulers continue, and active UI/timer/alarm screens have priority. It queues no additional startup audio. About and USB version retain their existing version details. The user broadly accepted date/next-alarm functionality after build 02b4a51 was installed. Detailed language/output/fallback coverage remains in TESTING.md.

## Settings

Turn the timer encoder to choose an item or value. SETTINGS or the timer encoder button enters/confirms. MINUS returns or cancels an uncommitted edit. Idle menus time out after 20 seconds; active Bluetooth discovery/name resolution is allowed to finish.

A hardware blinking character cursor marks the first character of the value being edited. The LCD controller generates the blink without periodic host commands; this is a blinking character cell rather than a blinking underline. It moves between hours/minutes/seconds or day/month/year, and marks the current scalar value in other editors. In the custom weekday editor it marks ON/OFF beside the day name. The same cue is used in the precise timer and alarm editors. Lists, About, music, ringing/snooze and ordinary clock screens hide the cursor; save, cancel and timeout also remove it. The user accepted the earlier steady underline mode; the subsequent hardware blinking mode was also accepted by the user in build 755f55b.

The ordinary profile has twelve settings items, in this order:

1. Language
2. Time
3. Date
4. Quiet mode
5. Half-hour beep
6. Exact-hour word
7. Audio Output
8. Bluetooth
9. Birthdays
10. Display Light
11. About
12. Timer sound

The experimental web profile inserts **Phone Control (11)** before **About (12)**; Timer sound becomes item 13. Phone Control shows the current address, switches between home and direct Wi-Fi, starts browser pairing, toggles home trust, forgets paired browsers and rotates the direct Wi-Fi key. The direct password screen lasts 50 seconds; the pairing code lasts 60 seconds. Home connection progress remains visible while a join/retry is pending. See [phone instructions](../experiments/phone_control_ap/README.md) for access scope, persistence and controls. The ordinary profile has no Phone Control item or web server.

Display Light controls the external LCD LED. OFF switches it off. ON uses Day Brightness at all times. AUTO uses Night Brightness from 22:00 through 06:59 and Day Brightness from 07:00 through 21:59, based on local RTC time. AUTO uses the night level when the RTC is unavailable. The light schedule is independent of audio Quiet mode and mute.

Turn the timer encoder to select a mode or change brightness in five-percentage-point steps from 0% to 100%. Confirm OFF to apply it immediately, confirm the day level to apply ON, or confirm both levels to apply AUTO. Completed edits use the usual deferred Flash autosave. MINUS or menu timeout discards an unfinished edit. New installations and older settings start with OFF, day 100% and night 20%. USB `display-light-status` reports the selected mode, configured levels and applied PWM duty.

Volume is adjusted directly with the left encoder, including while settings are open; there is no separate Volume editor. The encoder follows the active audio output and retains independent LOCAL and Bluetooth levels. The encoder button toggles sound without replacing either stored level. USB vol N and bt-vol N remain available for explicit LOCAL and Bluetooth adjustment.

About has two read-only pages: Speaking Clock / Pico 2 W, then the firmware version / by FlintUA. Turn the timer encoder in either direction to switch pages. MINUS returns to the settings list. The normal 20-second idle timeout applies. The build ID remains available through the USB version command.

Time/date writes occur only after confirming the final field. When network time is configured, later NTP synchronization applies Europe/Berlin time.

Audio Output contains independent preferences for manual speech, hourly voice, timer and alarm. LOCAL fallback is retained when Bluetooth is unavailable.

LOCAL is the built-in DFPlayer output; BT is the currently selected external Bluetooth speaker, regardless of brand. Selecting Portable or Nest under Bluetooth changes the speaker, not these independent output preferences. Music follows Manual. USB commands audio-select bt, auto-audio bt, timer-audio bt and alarm-audio bt use the same preferences; the earlier jbl spelling remains a compatible alias. These are preferred outputs; a missing stream or file can trigger LOCAL fallback.

## Bluetooth

The list contains Scan devices, saved speakers, newly found addresses not already saved, and Disconnect at the end. One MAC appears once. LCD labels show names; unknown names use Saved speaker or Unknown device. USB diagnostics retain MAC addresses.

Press to choose a speaker, then press again to confirm Connect. Selecting Disconnect or starting Scan devices clears the desired connection target, cancels retries and blocks incoming paging/pairing/streams until a new explicit Connect. Ending/cancelling a scan, backing out, timeout and simply closing Bluetooth never restore a disconnected target. Browsing without starting a scan or selecting Disconnect leaves the existing connection policy unchanged.

Known device names and pairing keys are retained. The existing all-FF no-target marker persists OFF through deferred Flash autosave; close settings and wait for autosave=OK before removing power. No Flash format change is required. After reboot OFF remains in effect, including for builds with a legacy default speaker. Application boot connections are held until saved settings load. Explicit Connect re-enables reconnect in RAM; the selected MAC is saved only when its stream becomes ACTIVE. If it never connects, OFF remains saved.

An unplanned link loss with a selected target still retries with the existing backoff. Late timer/signaling/stream callbacks cannot restore OFF or replace the selected peer with an old one. Disconnect does not power down the shared radio or Wi-Fi. BT output preferences and separate volume levels remain intact, using the existing LOCAL fallback. Stop playback before manual disconnect/scan; the existing timer/alarm/audio-busy guard applies. Bluetooth LCD status uses ON for streaming, RETRY for a selected offline target and OFF for no selected target. The user confirmed connection/disconnection, persistent OFF after a power cycle and no automatic return after a subsequent scan on build 7378aa1. Separate cancel/timeout, incoming-peer and link-loss recovery checks remain in TESTING.md.

USB commands: bt-disconnect, bt-scan, bt-scan-stop, bt-devices, bt-known, bt-use N|name|MAC, bt-target, bt-status, bt-forget N|MAC, bt-volume and bt-vol 0..30. Bare bt-forget prints usage and devices.

## Birthdays

Fifteen independent annual dates are available under SETTINGS -> Birthdays on this development branch. Each is numbered 1..15, disabled by default, and initially set to January 1 at 09:00. Existing v1-v13 records retain their first five dates and initialize slots 6-15 disabled. There are no one-time reminders, personal-name fields or new spoken reminder recordings.

Rotate TIMER to select a slot, press TIMER or SETTINGS to edit ON/OFF -> MONTH -> DAY -> HOUR -> MINUTE. Rotate to change each value; confirm the minute to save the whole draft. MINUS steps back, or discards the draft from ON/OFF; MINUS from the slot list returns to settings. The normal 20-second settings timeout discards unfinished edits. Month changes clamp the day to a valid value; February 29 is accepted and fires only in leap years. The blinking cursor marks the edited field.

On the matching local calendar minute the event is claimed once per year. The handled year is saved with the date to suppress repeats after reboot, DST or backward clock correction. Enabling/disabling retains that year; explicitly changing the date or time resets it for the new schedule. All fifteen co-due dates can be retained and delivered sequentially. No catch-up runs for minutes missed while powered off or skipped by a clock adjustment. A match in the first valid RTC minute after boot is consumed without playback.

Birthday melody 07/000.mp3 follows the Manual output preference, with the existing Bluetooth-to-LOCAL fallback; it is language-neutral and does not add synthesized speech. Both cards need the file. Playback waits for an idle clock/audio path, closed menus/player, sound enabled and non-quiet hours. It does not interrupt music, timers or alarms. Waiting expires after 30 monotonic minutes, including while muted or quiet. A consumed event is not retried later that year if it expires; waiting delivery is RAM-only and is lost on reset. Disable or change a date to cancel its waiting notification. On delivery LCD shows BIRTHDAY N and the date for eight seconds.

USB commands (close settings before USB editing):

```text
birthday-status
birthday-set 1 25 12 09 00 on
birthday-set 1 25 12 09 00 off
birthday-test 1
birthday-pending-clear
```

birthday-set uses slot/day/month/hour/minute/on-or-off and rejects invalid dates. birthday-test queues a RAM-only playback test using normal quiet/priority/expiry rules, even for a disabled slot; it does not change the saved date or handled year. It refuses to replace an already waiting occurrence. birthday-pending-clear discards waiting notifications without resetting saved years. BOOTSEL refuses while a birthday notification is waiting. No test command generates a saved date automatically.

Flash v10 added the first five dates and their handled years; v14 adds ten more dates. v1-v9 load with all birthday slots disabled. The user broadly accepted the original five-slot feature in build 7964939; COM8 confirms configured dates saved as v10. On 2026-11-13 the user reported that the latest changes worked correctly. This accepts the expanded functional workflow broadly; timed firing, output-specific melody, expiry, expanded-slot persistence and repeat-suppression/reboot cases remain individually tracked checks.

On this development branch, the browser also accepts `DD.MM` with an unknown birth year or `DD.MM.YYYY` with a known year (1000..9999). Both recur annually by day/month; the birth year is metadata and does not change recurrence. A known year must form a valid calendar date, including leap-day validation. Physical and USB date edits preserve a known birth year; clearing it is available in the browser. Changing only the birth year retains the handled event year. Known birth years in the first five slots require v13 storage; any nondefault slot 6-15 requires v14. See [compatibility](NETWORK_TIME.md#settings-compatibility-and-rollback).

## Alarms

There are five alarm slots. The timer encoder selects a slot and changes editor fields: enabled, hour, minute, days, sound and music track. TIMER confirms/advances; SETTINGS also advances except in the track field, where it starts/stops preview rather than saving. Changes remain a draft until final confirmation.

Days offers Daily, Mon-Fri, Sat-Sun and Custom. In Custom, TIMER steps Monday through Sunday and turning the encoder toggles the displayed day. MINUS goes to the previous day, or back to the Days selector from Monday. At least one day must be selected. In the other alarm fields MINUS returns to the alarm list and discards the draft. The list shows the selected day preset; alarm-status includes the exact mask (bit 0=Monday, bit 6=Sunday).

During ringing, GP28 stops the current sound and schedules one repeat three minutes later. Repeated snooze is supported. The deadline uses monotonic uptime, so RTC/NTP corrections and midnight do not move it. At the deadline, the alarm enters the existing deferred-audio queue: a running countdown, mute or open editor may postpone it further, with the usual 30-minute expiry measured from that deadline. Ready snoozes preempt music through the normal alarm priority path. The repeat uses the original alarm time and current sound/output preferences.

The clock screen shows the next snoozed alarm and remaining time. ALARM cancels all snoozes when the main screen is active; ALARM during ringing still stops only the ringing alarm. Disabling or editing a slot cancels its pending/snoozed occurrence. Snoozes live only in RAM and disappear on restart; weekday schedules are saved in Flash.

Music comes from folder 08. The editor uses the detected DFPlayer folder count, or the cached/default count if unavailable, and preserves any already selected higher track. Storage and explicit USB configuration support 1..255. Mirror the same music files onto the separate SPI card when using Bluetooth.

In the browser alarm editor, Test/Stop beside the music-track field auditions one folder 08 track without applying the alarm draft. It uses the alarm output preference with LOCAL fallback, may replace current ordinary music or another audition, and closes after that track finishes. While audition is active, including pause, the button shows Stop. This audition-specific Stop cannot stop ordinary music if a stale page still shows Stop. Next/Previous leave audition mode and resume normal playlist navigation. Timer, ringing alarm, mute and physical editors can block starting a test. The player Stop can also close an audition. File existence and physical output still need checking on the selected card; selecting a valid number alone does not prove the file exists.

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

The experimental web Quick controls group adds explicit Sound on/off and manually requested time/date/nearest-alarm speech. It shares the physical sound setter and speech paths, preserves separate volumes and saved schedules, and does not automatically resume music on unmute. Sound remains RAM-only and defaults ON after restart. Like the physical sound button, web sound changes are blocked while an alarm rings. Speech requires sound ON, closed physical editors/player, no countdown/ringing alarm and idle audio. See [phone controls](../experiments/phone_control_ap/README.md#quick-controls-substage-2-generally-accepted) for command semantics and installation status.

Committed changes use debounced Flash autosave when idle; wait for autosave=OK before removing power. The cooldown can defer an additional write. Run autosave-status or explicitly save when idle.

Use help for the complete USB command list. Important read-only commands: version, flash-info, alarm-status, audio-settings, bt-known, rtc-debug, audio-health and ntp-status. Serial speed is 115200.

See [network setup](NETWORK_TIME.md), [roadmap](ROADMAP.md) and [hardware evidence](HARDWARE_TEST_EVIDENCE.md) for current implementation and validation boundaries.

## Browser live alarm actions (user acceptance pending)

The experimental page adds Stop alarm, Snooze 3 min and Cancel snoozed repeats. Stop/snooze act only on the displayed ringing occurrence. Cancel removes sleeping/audio-waiting snoozed repeats, preserves saved alarm schedules and leaves any ringing alarm running. Commands use current occurrence/set IDs so stale pages cannot silently affect a later occurrence. Physical buttons retain their existing behavior. See [live alarm actions](../experiments/phone_control_ap/README.md#live-alarm-actions-substage-3-user-acceptance-pending).

## Countdown completion audio (fix; generally accepted)

Installed with separate approval on November 13 as `1.0.0+26.g3717033`. Startup, read-only USB/HTTP and settings-preservation checks passed; the user subsequently confirmed that the fix works correctly. This is general acceptance, without separate timing measurements or output-specific coverage.

Countdown timers and scheduled alarms are separate. Alarm Stop/Snooze actions do not apply to countdown completion. Cancel timer cancels a running countdown. The new **Stop timer sound** action stops only the currently owned completion announcement, without a cancellation phrase or changing saved schedules/levels. It is enabled while that announcement is active; no manual page refresh is required. The status shows **Finished - announcement playing**. Actual on-page punctuation may use a separator dot.

The `/timer` action `stop_sound <timer_sound_id>` uses the existing credential/epoch/id/nonce envelope and a nonzero ID from `overview.timer_sound_id`. Ended or superseded events are refused; another alarm, player or later timer event cannot be stopped by an old ID. The owner is cleared when audio completes, is replaced/appended by another owner, or is interrupted by mute/priority. Bluetooth-to-LOCAL fallback retains the same event ID. No Flash-format change or save is introduced.

The page requests status every 250 ms during the last five countdown seconds and while completion audio or a scheduled alarm is active, versus the usual 1 second (plus response time). This improves observation latency; it does not guarantee immediate response under radio/decoder load. Controls remain state-based. If completion has already ended, Stop timer sound is disabled. The user reported correct operation after installation. End-to-end latency was not measured and the earlier page-jump cause was not isolated; the page contains no full-page reload operation.

## Timer completion message and sound (installation pending)

Message ON/OFF is independent of extra sound NONE/SIGNAL/MUSIC. The default is the existing message plus signal. The Timer sound menu edits message, sound and, for MUSIC, folder-08 track 1-255; only final confirmation saves the whole draft. MINUS/timeouts discard unfinished edits. The browser exposes the message checkbox and separate sound/track selectors with Apply, and USB supports timer-voice on|off and timer-sound none|signal|music N. Changes require stopped countdown and idle audio. Start/cancel speech and timer output routing are unchanged. Stop timer sound interrupts the active completion regardless of which components were selected. See [full controls and storage compatibility](../experiments/phone_control_ap/README.md#timer-completion-choices-installation-pending). German completion speech is unavailable; message-only DE therefore has no playable audio until assets are supplied.
