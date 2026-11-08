# Release verification

## Native tests

No Pico SDK is needed for these tests. With a C11 compiler and CMake:

```sh
cmake -S firmware/pico2w_c/tests -B build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
```

GitHub CI runs this suite on Windows and Linux.

If a local Windows/MSVC build misses a changed header (for example due to localized include diagnostics), use a fresh build directory or cmake --build build/host-tests --clean-first before rerunning CTest. The three-minute snooze change was verified with a fresh local build after an incremental cache retained an old object. GitHub CI uses fresh checkouts.

The twelve suites cover:
- Alarm validity, weekday filtering, leap/calendar boundaries, simultaneous slots, duplicate suppression and independent monotonic snooze deadlines/cancellation.
- Calendar conversion, leap days, Europe/Berlin transitions, NTP era rollover, invalid headers and mismatched request nonces.
- MAC parsing, unique combined catalogs and preservation of useful saved names.
- Settings v1-v9 decoding and migration to v10, daily defaults for old alarms, weekday-mask persistence/validation, Wi-Fi fields, output preferences, extended alarm tracks, real v8/v9 A/B-slot migration, disabled birthday defaults and date/year roundtrip, rejection of invalid birthday records even with recomputed CRC, A/B fallback after corruption, failed writes and refusal to erase unknown sectors. Simulated Flash erase/program is restricted to clock A/B slots. This is not a physical Flash endurance test.
- Git-derived versions: no tags, exact lightweight and annotated tags, commits after a tag, tracked changes, unrelated tags and source archives.
- Precise timer draft/confirmation/cancellation, field wrapping and four-hour limit, accelerated minute selection and monotonic one-shot countdown completion.
- Music modes, repeat-one versus explicit Next, backward/forward history, bounded history retention, single-track libraries and shuffle cycle/boundary protection for 2, 7, 8, 78 and 255 tracks.
- LOCAL queue pause/resume using a mock DFPlayer: delayed UART readiness, pause-before-start, rapid pause/resume/pause, volume changes during pause, no EOF advancement during a long pause, delayed BUSY after resume, natural EOF and clearing pending resume for exit/priority interruption. These tests do not validate a physical DFPlayer clone.
- Date speech RU/DE folder ordering, calendar-derived weekday, supported/unsupported year ranges, leap-day validation, ring wrap and all-or-nothing queue capacity. Nearest alarm covers disabled/invalid slots, weekday masks, ties, strict future selection, next-week, leap-day and year boundaries.
- Annual birthday validation, co-due dates, persistent-year duplicate suppression/reboot, backward corrections, next year, leap-day behavior, no missed-minute catch-up and exact monotonic expiry across 32-bit milliseconds.
- Music sleep presets, deadline replacement/cancellation, rounded remaining minutes, exact expiry and a deadline crossing 32-bit milliseconds.
- LCD cursor commands decoded from mocked PCF8574 writes: correct row/column and underline/blink/off controls, no repeated I2C traffic for unchanged focus, restoration after text repaint, bounds and failure handling. These do not validate the appearance of a physical LCD.

## Firmware builds

Default build:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3
```

Bluetooth-only fallback:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -NoWifi
```

The script supports -BuildDirectory for an ASCII build path and -HelixPath for an existing external decoder checkout. It records the Git revision and marks tracked uncommitted changes as dirty. Release builds must have a clean source revision.

## Automatic firmware version

Version strings are generated from reachable local Git release tags matching vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease. Both lightweight and annotated tags are supported. The generator runs at configuration and before each firmware build; no manual version edit or cache reset is required.

- Clean checkout at v1.0.0: USB version is 1.0.0; About shows v1.0.0.
- Three commits after that tag: USB version is 1.0.0+3.gHASH; About shows v1.0.0+.
- Tracked local edits append .dirty to the USB metadata; About also shows +.
- No reachable release tag: 0.0.0+gHASH, with + on About.
- No Git metadata or Git executable, including source archives: 0.0.0+unknown. An archive does not claim the current release version. Use a Git checkout for an identified release build.

About reserves the second line for by FlintUA; version text is bounded to the LCD's 16 columns. USB retains the full version. The existing build ID separately identifies source revision and enabled profiles.

Fetch tags before building if the checkout does not have them: git fetch --tags. A shallow checkout must include a reachable release tag, or fetch full history using git fetch --unshallow --tags. Tag creation alone does not alter firmware already installed on a board; rebuild and flash it.

## Device acceptance before stable release

### Annual birthdays (UI/save broadly accepted; timed/audio coverage remains)

Build 7964939 was installed with separate approval. The user broadly accepted the feature, and COM8 confirms configured dates in a v10 record alongside the previous v9 fallback, with prior settings retained. Exact firing/expiry and output-specific melody cases below are still coverage checks.

Before upgrading, retain alarm-status, audio-settings and bt-known snapshots. After the v10-capable firmware starts, birthday-status must show five disabled, valid January 1/09:00 defaults when loaded from v9; loading alone must not write a v10 record. Compare alarms, masks, volumes, outputs, speaker names/pairing and network status. Never print Wi-Fi passwords.

Open Birthdays (item 9), navigate all five slots and edit ON/OFF, month, day, hour and minute. Check blinking cursor, back navigation, draft cancellation/timeout, month clamping and February 29. Save a future occurrence, wait for autosave, reboot and verify the date plus all previous settings. Flash v10 rollback requires a v10-capable build; use the current -NoWifi profile if needed.

With the clock idle, use birthday-test N on LOCAL and BT (the file 07/000.mp3 must exist on both cards). Confirm the expected birthday melody, label and fallback, with no changes to the saved date/year. Clear pending tests with birthday-pending-clear. Schedule an actual event for a future minute today to check due/last_year, one playback and persistence after reboot. Test two simultaneous slots sequentially. Muted/quiet/music/timer/alarm/menu states must defer instead of interrupting; expiry at 30 minutes is covered by host tests and still needs a timed device check. Restore/disable test dates when finished. BOOTSEL must refuse while a birthday is pending. Yearly and leap-day recurrence should use host tests rather than changing a live clock merely to simulate years.


### Date/next-alarm/startup update (broad user acceptance)

Power-cycle and check the short version splash, responsive controls and subsequent normal clock. Confirm no new startup sound beyond existing Bluetooth behavior. On an idle clock, MINUS short should show/announce the date; hold at least 900 ms should show/announce the next eligible alarm. Check both languages and Manual=LOCAL/BT, required date folders on both cards, full date versus supported spoken year, and LOCAL fallback if a Bluetooth date segment is unavailable. USB say-date and next-alarm offer repeatable requests without changing schedules.

Using existing schedules, compare the forecast with alarm-status and today's weekday. Confirm the earliest enabled match and the date on LCD. For changes to schedules, retain the original values and restore them afterward. Check an active snooze as the nearer occurrence, no-enabled-alarm display, mute (LCD without speech), no interruption of a timer/alarm/player, MINUS back/Previous/hold-to-exit behavior on the existing screens, and intact MODE time announcement. Host tests cover recurrence/calendar logic, but RAM-snooze forecast and actual speech/routing require device acceptance. Build 02b4a51 was installed with separate approval, and the user confirmed the functionality works. The detailed cases above remain regression/coverage checks; the report did not enumerate every case.


### Player pause and sleep update (partial user acceptance)

The user reported pause and sleep-time selection working in build 38182d4, but did not provide output-specific coverage or confirm a full expiry. Explicit OFF display is a follow-up change pending installation.

On both Manual=LOCAL and Manual=BT, start a recognizable long track, pause after several seconds with TIMER, wait, and resume. Expect PAUS on LCD and continuation from the same passage rather than the beginning. Check rapid double presses, pause immediately after opening/Next, volume adjustment during pause, EOF after resume, and Next/Previous from pause. Confirm SHUF/NORM/REP1 continue to behave as before.

Press GP28 to select 15, 30, 60, 90 minutes and OFF. Verify OFF is visible on entry and after cycling back to disabled, always in the same three-character position as the remaining minutes beside PLAY/PAUS/MUTE/WAIT/STOP, and track/mode changes keep the deadline. Check a full 15-minute expiry while playing and while paused: silent stop/exit, clock visible, no effect on saved volume/output/alarm settings, Bluetooth stays connected. Confirm OFF cancels, reselection resets the deadline, and exit/reopen resets sleep to OFF. Host tests cover exact expiry without a real-time wait.

While paused, exit with a supported hold or mute and restart; these intentionally discard position. Test an alarm interrupting paused music (alarm must take priority), then reopen the player. Disconnect BT while paused: no automatic LOCAL playback, next TIMER starts the same track from the beginning on LOCAL. Settings/Flash formats and pairing keys must remain unchanged. No pause position or sleep deadline is restored after reboot.


The user confirmed weekday setup/firing and the full delayed repeat on the earlier five-minute build, then reported the three-minute version works after its separately authorized installation.

Cursor acceptance: check time/date H/M/S and D/M/Y positions, precise timer fields, alarm hour/minute, track/sound/day preset and custom-day ON/OFF, language, quiet hours, half-hour and exact-hour toggles. Confirm focus follows a field even when its text does not otherwise change, continues blinking at the selected value after rotation/redraw, and disappears on save/cancel/timeout and all non-editing screens. Check ordinary Bluetooth playback and button responsiveness alongside LCD interaction. The user accepted steady underline focus in build a0d5a9e. The user also accepted hardware blinking focus in build 755f55b. The user accepted removal of the redundant Volume editor in build 4bad974. Regression checks: confirm the ten-item menu order in CONTROLS_AND_MODES.md, wraparound between About and Language, and access to Audio Output (7), Bluetooth (8), Birthdays (9) and About (10). Check that the left encoder still adjusts the active output in and outside settings, retains independent LOCAL/BT levels and shows the volume overlay; its button must still toggle sound.

Weekday/snooze stage: the COM8 smoke test confirmed v8 loading with Daily on all slots, unchanged alarm fields/output settings, Portable reconnection, and USB snooze schedule/cancel without a Flash write. Remaining checks: configure Daily, Mon-Fri, Sat-Sun and a custom mask using LCD/USB; test draft cancellation and rejecting an empty custom selection; save/reboot and check masks, alarm times/enabled/sounds, volumes, known speakers and network settings. Use alarm-audio-test N to ring a slot without changing its schedule; physical GP28 should stop it and ring once after three minutes. Verify repeated snooze, ALARM cancellation on the main screen, disabling/editing a snoozed slot, restart cancellation, timer/mute deferral and music preemption. Check both LOCAL and BT. These remaining paths need device acceptance; weekday/migration/snooze host tests have passed.

Restored timer controls require device checks: GP28 editor entry/cancel, HOUR/MIN/SEC confirmation using TIMER/SETTINGS, MINUS/back, timeout cancellation, one-second completion and four-hour limit. Check fast rotation and direction reversal in quick selection. In the music player test mute/unmute on LOCAL and Bluetooth: music stops, levels remain intact and unmute requires explicit playback. These paths have not yet been accepted on the device.

For the extended player, check LOCAL and BT with Portable/Nest: automatic play on entry, mode cycling without restarting the current song, NORM advancement, REP1 at EOF, explicit Next in REP1, Previous/forward history, rapid encoder changes, PLAY/PAUSE/resume, mute and all three hold-to-exit paths. Check automatic EOF advancement and Bluetooth loss: current track restarts on LOCAL without a playlist skip. Verify all output menus/statuses say BT and both bt/jbl CLI spellings work. The user accepted the earlier playback/navigation controls; the new pause/sleep transport paths still need hardware acceptance. Host tests do not replace it.

The user subsequently confirmed the extended player works. The additional MINUS hold exit still needs a device check: short release selects Previous; release after at least 900 ms exits without starting another track.

## Long USB Bluetooth capture

Close other COM monitors, then run tools/capture-pico-log.ps1 -Port COM8 -Minutes 30 -LogPath build/diagnostics/bluetooth.log. The capture records all received lines with PC UTC timestamps. It sends only version, bt-status, audio-settings and ntp-status once at startup; it does not scan, pair, play or flash. An optional -StopFile path ends capture when that file appears, releasing the port. Record the audible event time to distinguish A2DP suspend/start, signaling reconnection and speaker-local sounds. Logs under build are ignored by Git.

Choose a new log filename for each run; existing logs are preserved. The SDK labels a suspended A2DP stream as Stream paused. The full raw log is always recorded even if a line is absent from the concise console filter. A COM capture does not record sound itself.

Follow [network acceptance](NETWORK_TIME.md#hardware-acceptance) and [firmware verification](../firmware/pico2w_c/README.md#on-device-verification). In addition:
- Start discovery, select no new device and exit; the preferred connection must resume.
- Find a saved speaker again; it must appear once and keep its useful name even if discovery reports Unknown.
- Test a music alarm above track 45 with a real matching file.
- Check version after updating and retain the UF2 build-ID sidecar.
- Run the assembled clock overnight with the actual access point, Portable/Nest, RTC, SD and DFPlayer.

Earlier hardware passes are retained in [HARDWARE_TEST_EVIDENCE.md](HARDWARE_TEST_EVIDENCE.md); they do not imply that newly added features have passed device testing.
