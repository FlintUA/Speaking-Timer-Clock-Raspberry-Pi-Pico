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

The seven suites cover:
- Alarm validity, weekday filtering, leap/calendar boundaries, simultaneous slots, duplicate suppression and independent monotonic snooze deadlines/cancellation.
- Calendar conversion, leap days, Europe/Berlin transitions, NTP era rollover, invalid headers and mismatched request nonces.
- MAC parsing, unique combined catalogs and preservation of useful saved names.
- Settings v1-v8 decoding and migration to v9, daily defaults for old alarms, weekday-mask persistence/validation, Wi-Fi fields, output preferences, extended alarm tracks, real v8 A/B-slot migration, A/B fallback after corruption, failed writes and refusal to erase unknown sectors. Simulated Flash erase/program is restricted to clock A/B slots. This is not a physical Flash endurance test.
- Git-derived versions: no tags, exact lightweight and annotated tags, commits after a tag, tracked changes, unrelated tags and source archives.
- Precise timer draft/confirmation/cancellation, field wrapping and four-hour limit, accelerated minute selection and monotonic one-shot countdown completion.
- Music modes, repeat-one versus explicit Next, backward/forward history, bounded history retention, single-track libraries and shuffle cycle/boundary protection for 2, 7, 8, 78 and 255 tracks.

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

The user confirmed weekday setup/firing and the full delayed repeat on the earlier five-minute build. The requested three-minute timing still needs device acceptance after an authorized update.

Weekday/snooze stage: the COM8 smoke test confirmed v8 loading with Daily on all slots, unchanged alarm fields/output settings, Portable reconnection, and USB snooze schedule/cancel without a Flash write. Remaining checks: configure Daily, Mon-Fri, Sat-Sun and a custom mask using LCD/USB; test draft cancellation and rejecting an empty custom selection; save/reboot and check masks, alarm times/enabled/sounds, volumes, known speakers and network settings. Use alarm-audio-test N to ring a slot without changing its schedule; physical GP28 should stop it and ring once after three minutes. Verify repeated snooze, ALARM cancellation on the main screen, disabling/editing a snoozed slot, restart cancellation, timer/mute deferral and music preemption. Check both LOCAL and BT. These remaining paths need device acceptance; weekday/migration/snooze host tests have passed.

Restored timer controls require device checks: GP28 editor entry/cancel, HOUR/MIN/SEC confirmation using TIMER/SETTINGS, MINUS/back, timeout cancellation, one-second completion and four-hour limit. Check fast rotation and direction reversal in quick selection. In the music player test mute/unmute on LOCAL and Bluetooth: music stops, levels remain intact and unmute requires explicit playback. These paths have not yet been accepted on the device.

For the extended player, check LOCAL and BT with Portable/Nest: automatic play on entry, mode cycling without restarting the current song, NORM advancement, REP1 at EOF, explicit Next in REP1, Previous/forward history, rapid encoder changes, PLAY/STOP, mute and both hold-to-exit paths. Check automatic EOF advancement and Bluetooth loss: current track restarts on LOCAL without a playlist skip. Verify all output menus/statuses say BT and both bt/jbl CLI spellings work. These new control/transport paths still need hardware acceptance; host playlist tests do not replace it.

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
