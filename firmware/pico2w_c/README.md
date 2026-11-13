# Pico 2 W clock C firmware

Stable baseline: v1.0.0. This checkout's `codex/phone-control` branch develops the optional browser-control profile; it is not a new stable release. Current controls are documented in [CONTROLS_AND_MODES.md](../../doc/CONTROLS_AND_MODES.md), with network provisioning and rollback in [NETWORK_TIME.md](../../doc/NETWORK_TIME.md). See [TESTING.md](../../doc/TESTING.md) for host tests and release acceptance, and [phone control](../../experiments/phone_control_ap/README.md) for browser setup and checks.

This is the current project firmware. See the [main README](../../README.md) for build dependencies and updates, and the [current pin map](../../doc/HARDWARE_PICO2W.md) for wiring.

## On-device verification

1. After flashing, run flash-info, alarm-status, audio-settings and bt-status.
2. Check LCD, RTC, both encoders, buttons, LOCAL speech and the timer.
3. Run bt-scan, wait for completion, then bt-devices and bt-use N.
4. Wait for Stream ACTIVE, leave the menu and wait for autosave=OK.
5. Restart the board: bt-known should retain the name/MAC and connection should require no scan.
6. Boot with the speaker off: the clock and LOCAL audio should continue working. Turn the speaker on and check reconnection.
7. Run bt-forget N and restart: the removed preferred device should not reconnect automatically.
8. Check MP3, speech, timer and alarm playback over Bluetooth, connection loss and independent output volume levels.

update-pico.ps1 automatically checks Flash and alarm responses, but does not replace sound and Bluetooth hardware tests. The relocated encoder, volume button, SD/FAT32 and MP3/Bluetooth have passed hardware tests; see [evidence](../../doc/HARDWARE_TEST_EVIDENCE.md). Registry v7 tests confirm discovery, connection, saving, Portable reconnection after power loss, deletion with autosave and Portable <-> Nest switching without scanning, followed by speech. Both switches recovered from initial status 0x56 through an automatic retry after five seconds.

## Controls and diagnostics

The left volume encoder adjusts the active output directly, including in settings. Its button toggles sound. LOCAL and Bluetooth levels remain separate. The timer encoder sets the duration. Its button starts or cancels the timer and confirms menu selections. See the [settings list](../../doc/CONTROLS_AND_MODES.md#settings).

The current development timer limit is 23:59:59 and birthdays have fifteen slots; stable v1.0.0 retains the earlier limits. Browser timer wheels and combined player controls are documented in the [phone guide](../../experiments/phone_control_ap/README.md). Alarm track Test/Stop uses `/player` actions `test_track 1..255` and `test_stop 0`; `player_test` identifies an audition. These commands do not apply the unsaved alarm draft. A general player Stop remains separate and closes either ordinary playback or an audition.

The web snapshot also contains a read-only `overview`: validated date/weekday, global sound, alarm/deferred/snooze forecast, Bluetooth display state/name and network/NTP metadata. The projection and bounded JSON encoder are in `services/phone_overview.c`; hardware reads stay in the main loop. `network_time_reference_status` distinguishes a received reference from successful RTC application. Existing controls/Flash encoding are unchanged. Approved installation passed startup/HTTP/settings checks; the user accepted the general overview.

Quick-control substage 2 adds authenticated `/control` actions `sound 0|1`, `say_time 0`, `say_date 0` and `next_alarm 0` to the shared command queue. `services/phone_control.c` centralizes admission guards; main-loop handlers reuse the physical setter/speech functions and report specific refusal results. Applied controls wait for the corresponding snapshot before browser confirmation. No Flash schema/save is introduced. After separate user approval, this substage was installed on November 13 as `1.0.0+24.g38a8743`. Startup and read-only HTTP/settings checks passed; the user subsequently confirmed general operation. Next-alarm wording is deferred for the audio refresh.

SETTINGS opens the menu, the timer encoder selects an item, SETTINGS or the timer encoder button confirms, and MINUS goes back. Run help for the complete USB command list. Serial speed: 115200.

## Storage

Clock Flash A/B occupies sectors -6/-5 from the end of Flash. Legacy sector -4 is read for migration. BTstack uses -3/-2, and the final sector is reserved. This branch reads CRC-protected v1-v14 records. Writes select v11 (348 bytes) with blank phone settings and no known birth years, v12 (504 bytes) with phone settings and no known birth years, v13 (512 bytes) when one of the first five birth years is known, or v14 (616 bytes) when slots 6-15 differ from their disabled defaults. v11-v13 use two programmed pages and v14 uses three in the existing clock sector. Loading does not rewrite Flash. Ordinary and `-NoWifi` builds from this branch preserve phone settings and all birthday entries. Stable v1.0.0 cannot read v12-v14; see rollback instructions before reverting. BTstack stores pairing keys separately.

A device is added only after an active A2DP stream with a matching MAC. Builds without Bluetooth preserve these fields when saving other settings. SPI SD access is read-only.

## History and limitations

The previous accumulated README is in the [development history](../../doc/history/PICO2W_C_DEVELOPMENT.md). Its early wiring and descriptions of incomplete stages are historical.

CMake flags CLOCK_ENABLE_A2DP, CLOCK_ENABLE_MP3 and CLOCK_ENABLE_WIFI are explicit options. The standard build script enables A2DP/Wi-Fi and adds MP3 with -Mp3; -NoWifi preserves a Bluetooth-only fallback. External LCD illumination on GP6 uses the Pico SDK hardware PWM library in every build. Classic A2DP/SBC with automatic SSP confirmation is supported; legacy PIN pairing is not. Helix is checked out separately at the revision documented in the main README.

`CLOCK_ENABLE_PHONE_WEB` defaults to OFF. `build-clock-bt.ps1 -Mp3 -PhoneWeb` enables the integrated local HTTP server and appends `-phone` to the build ID. It requires Wi-Fi and the pico-examples DHCP server. The web profile configures a 48 KiB lwIP heap and four POST contexts for four simultaneous clients; the ordinary profile retains 8 KiB. Four stored browser authorization entries are a separate limit. Hardware concurrency and audio continuity remain to be tested. Only the web profile adds Phone Control before About in the physical settings menu. Both home and direct modes serve the same embedded page; switching selects one network mode at a time.

## Browser live alarm actions (substage 3; user acceptance pending)

The authenticated `/control` endpoint accepts `alarm_stop <active_id>`, `alarm_snooze <active_id>` and `snooze_cancel <snooze_revision>` inside the existing credential/epoch/id/nonce envelope. Both values are nonzero uint32 numbers from `overview.alarm`. The main loop checks them against current state before calling the physical `alarm_stop`, `alarm_snooze_start` or `alarm_snooze_cancel_all` handler. These actions bypass the speech/menu/timer guards so a ringing alarm remains stoppable.

`active_id` is newly allocated on each successful ringing start, including a repeated occurrence of the same slot; it is published as zero when not ringing. `snooze_revision` changes when the pending snooze set/deadlines or snoozed deferred occurrences change, including transition to audio waiting. Ordinary countdown ticks do not change it. Command retries retain their ID/nonce and cached result. A reboot changes the existing command epoch; no runtime alarm IDs are persisted.

Results are `conflict` for changed state, `no_ringing` for no active alarm and `no_snooze` for no pending repeats. Cancellation affects sleeping and audio-waiting snoozed repeats only, leaves regular deferred occurrences and any currently ringing alarm alone, and does not disable saved schedules. Stop affects the current ringing occurrence only. No new Flash schema or write is introduced. Hardware acceptance is pending.

## Countdown completion audio (fix; generally accepted)

Installed with separate approval on November 13 as `1.0.0+26.g3717033`. Startup, read-only USB/HTTP and settings-preservation checks passed; the user subsequently confirmed that the fix works correctly. This is general acceptance, without separate timing measurements or output-specific coverage.

Countdown timers and scheduled alarms are separate. Alarm Stop/Snooze actions do not apply to countdown completion. Cancel timer cancels a running countdown. The new **Stop timer sound** action stops only the currently owned completion announcement, without a cancellation phrase or changing saved schedules/levels. It is enabled while that announcement is active; no manual page refresh is required. The status shows **Finished - announcement playing**. Actual on-page punctuation may use a separator dot.

The `/timer` action `stop_sound <timer_sound_id>` uses the existing credential/epoch/id/nonce envelope and a nonzero ID from `overview.timer_sound_id`. Ended or superseded events are refused; another alarm, player or later timer event cannot be stopped by an old ID. The owner is cleared when audio completes, is replaced/appended by another owner, or is interrupted by mute/priority. Bluetooth-to-LOCAL fallback retains the same event ID. No Flash-format change or save is introduced.

The page requests status every 250 ms during the last five countdown seconds and while completion audio or a scheduled alarm is active, versus the usual 1 second (plus response time). This improves observation latency; it does not guarantee immediate response under radio/decoder load. Controls remain state-based. If completion has already ended, Stop timer sound is disabled. The user reported correct operation after installation. End-to-end latency was not measured and the earlier page-jump cause was not isolated; the page contains no full-page reload operation.

## Timer completion choices (user acceptance pending)

Completion message and extra sound are independent saved preferences:

| Preference | Choices | Default |
| --- | --- | --- |
| Message | On / Off | On |
| Extra sound | None / Signal / Music | Signal |
| Music track | 1-255, one file in folder 08 | 1 |

The message plays first when enabled and available, followed by the selected signal or a single music track. Message-only, signal-only, music-only and fully silent completion are valid; silent completion still updates the display/status. Signal and music are alternatives. Start/cancel phrases, the countdown range, separate output volumes and timer routing are unchanged. The existing Stop timer sound action applies to any nonempty completion plan. German completion speech is still missing on the current card; no Russian replacement or missing German track is requested. Selected signal/music still plays.

On the clock, open Settings -> Timer sound (12 in the ordinary profile, 13 in the web profile). Choose the optional five-second warning ON/OFF, Message ON/OFF, then NONE/SIGNAL/MUSIC, and a track for MUSIC. Confirm the last applicable field to save the whole draft. MINUS backs up or cancels; timeout discards uncommitted changes. Existing menu item numbers, including Phone Control and About, are unchanged.

In the browser, use the Timer completion message checkbox and Timer finish sound / Timer music track controls under Settings. Finish editing each field and wait for confirmation before starting a countdown. These controls require idle audio, stopped timer/player and closed physical editors; volume/brightness retain their existing behavior. Settings use the normal deferred Flash autosave. Wait for Settings saved before power cycling.

USB: `timer-sound` reports all three values; `timer-voice on|off`, `timer-sound none`, `timer-sound signal`, or `timer-sound music N` apply changes when idle. Browser `/settings` actions are `timer_voice 0|1`, `timer_sound 0|1|2` (Signal/Music/None), `timer_track 1..255`, with the existing authentication/epoch/id/nonce envelope. No preview or track seeking is added in this stage. The selected file must exist on the relevant audio card.

### Storage compatibility

The v15 record is 624 bytes (three Flash pages), extending the immutable v14 prefix without changing sectors or BTstack storage. Reads of v1-v14 do not write Flash and default to message ON, signal, track 1. Nondefault timer preferences require v15; default preferences retain the existing v11-v14 conditional encodings. All fifteen birthdays, birth years, Wi-Fi/browser credentials, Bluetooth devices and other settings are preserved. A rollback must use a v15-aware build to retain access to v15 records; an older build can ignore those records and refuse further saves. This is a settings-format compatibility requirement, not a change to the updater.

Nineteen registered host tests and both firmware profiles pass. After explicit approval, version `1.0.0+27.ge5593d1` was installed on November 13. Startup/read-only HTTP/settings checks passed, including legacy message ON / signal / track 1 defaults. Actual card playback, selected combinations and v15 save/power-cycle persistence remain pending user acceptance.

## Random timer and alarm music (user acceptance pending)

Timer music can use a saved random flag independently of its message. Each alarm slot 1-5 now selects Signal / fixed Music / random Music. One actual numeric folder-08 file is selected per event, retained on snooze and output fallback, and redrawn for the next occurrence. A missing catalog falls back to signal. Mirror folder 08 on both cards. See [controls, file catalog, v16 compatibility and tests](../../doc/RANDOM_EVENT_MUSIC.md). Approved installation and read-only startup/catalog/settings checks passed on November 13 as `1.0.0+28.g36286e9`; hardware acceptance is pending.

## Optional five-second timer warning (user acceptance pending)

A separate saved checkbox enables single beeps at 5, 4, 3, 2 and 1 seconds. Default OFF; Sound OFF suppresses it. Warning audio uses 19/001.mp3 and timer routing, does not queue late ticks or delay zero, and leaves message/music choices independent. The physical Timer sound editor adds the warning page before its existing fields. See [timing, busy skips, v17 compatibility and tests](../../doc/TIMER_PREWARNING.md). Software checks pass. Approved installation and read-only USB/HTTP/settings checks passed on November 13 as `1.0.0+29.g90b047a`; actual cadence and v17 persistence await hardware acceptance.

## Automatic settings and mobile checkbox fix (installation pending)

Simple Settings controls now apply automatically; Apply remains for complete alarm/birthday records. Nonshrinking checkboxes and wrapping labels fix the narrow-screen/larger-text issue. LOCAL timer-warning playback waits for natural BUSY completion with a longer bounded startup allowance, without changing audio files or saved volumes. See [behavior, evidence and pending device checks](../../doc/WEB_SCALAR_SETTINGS.md).
