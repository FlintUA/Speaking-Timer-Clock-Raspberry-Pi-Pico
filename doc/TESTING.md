# Release verification

## Native tests

No Pico SDK is needed for these tests. With a C11 compiler and CMake:

```sh
cmake -S firmware/pico2w_c/tests -B build/host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
```

GitHub CI is configured to run this suite on Windows and Linux for pushes to main, pull requests and manual dispatch. A push to `codex/phone-control` alone does not trigger that workflow; local passing tests are not a claim of a branch CI run.

If a local Windows/MSVC build misses a changed header (for example due to localized include diagnostics), use a fresh build directory or cmake --build build/host-tests --clean-first before rerunning CTest. The three-minute snooze change was verified with a fresh local build after an incremental cache retained an old object. GitHub CI uses fresh checkouts.

The current branch defines seventeen host checks (sixteen compiled C executables plus the CMake Git-version check). If Node.js is available at configuration, CMake also registers `phone_sliders` and `phone_timer_wheels`, for nineteen checks in total. Node.js is optional and adds no npm dependencies; inspect the CTest list to verify whether these checks are included. The seventeen non-browser checks cover:

- Alarm validity, weekday filtering, leap/calendar boundaries, simultaneous slots, duplicate suppression and independent monotonic snooze deadlines/cancellation.
- Calendar conversion, leap days, Europe/Berlin transitions, NTP era rollover, invalid headers and mismatched request nonces.
- MAC parsing, unique combined catalogs and preservation of useful saved names.
- Settings v1-v14 decoding, older-format compatibility and v12/v13/v14 migration, including alarm weekdays, optional birth years and handled years, phone settings, Wi-Fi fields, saved speakers and volume/output preferences. Coverage includes display-light defaults, OFF/ON/AUTO and the 07:00/22:00 transitions, invalid settings with valid CRC, A/B fallback, failed writes and refusal to erase unknown sectors. Simulated Flash erase/program is restricted to clock A/B slots.
- Git-derived versions: no tags, exact lightweight and annotated tags, commits after a tag, tracked changes, unrelated tags and source archives.
- Precise timer draft/confirmation/cancellation, field wrapping and 23:59:59 limit, accelerated minute selection and monotonic one-shot countdown completion.
- Music modes, repeat-one versus explicit Next, backward/forward history, bounded history retention, single-track libraries and shuffle cycle/boundary protection for 2, 7, 8, 78 and 255 tracks.
- LOCAL queue pause/resume using a mock DFPlayer: delayed UART readiness, pause-before-start, rapid pause/resume/pause, volume changes during pause, no EOF advancement during a long pause, delayed BUSY after resume, natural EOF and clearing pending resume for exit/priority interruption. These tests do not validate a physical DFPlayer clone.
- Date speech RU/DE folder ordering, calendar-derived weekday, supported/unsupported year ranges, leap-day validation, ring wrap and all-or-nothing queue capacity. Nearest alarm covers disabled/invalid slots, weekday masks, ties, strict future selection, next-week, leap-day and year boundaries.
- Annual birthday validation, co-due dates, persistent-year duplicate suppression/reboot, backward corrections, next year, leap-day behavior, no missed-minute catch-up and exact monotonic expiry across 32-bit milliseconds.
- Bluetooth connection policy: chosen peer matching, old-peer rejection after switching, OFF during/after scan, repeated stale callback decisions, retry guards for active/connecting/switching states and malformed addresses. Flash roundtrip of the existing OFF marker preserves known devices, network, birthday/alarm settings and independent volumes. Real BTstack event timing and incoming-paging behavior require hardware acceptance.
- Music sleep presets, deadline replacement/cancellation, rounded remaining minutes, exact expiry and a deadline crossing 32-bit milliseconds.
- LCD cursor commands decoded from mocked PCF8574 writes: correct row/column and underline/blink/off controls, no repeated I2C traffic for unchanged focus, restoration after text repaint, bounds and failure handling. These do not validate the appearance of a physical LCD.
- Phone access and commands: pairing lifetime/attempt limits, scoped remembered keys, logout, home trust, same-origin validation, shared sequence/nonces and retries, timer/player/settings/schedule validation and stale-record revisions.
- Mocked network-time integration: asynchronous joining while Phone Control is open, idle deferral of NTP/RTC changes, address acquisition, timeout and retry countdowns. This does not reproduce real radio timing.
- Read-only overview: calendar-derived weekday, nearest scheduled/snoozed alarm, invalid RTC, multiple/due/deferred snoozes, monotonic counter boundaries, JSON overflow guards and Bluetooth-name escaping/UTF-8 handling. Network reference checks distinguish a sample from successful RTC application and failed-application retry.
- Quick-control admission: sound remains allowed in player/menu/countdown, ringing alarms block it, and speech respects mute/editor/timer/player/audio priority. Access tests cover action/endpoint/range validation, scoped authorization, shared-queue contention and cached refusal/application results.

On 2026-11-12, the fifteen-birthday and 23:59:59 timer changes passed all sixteen then-registered host checks in a Visual Studio Debug build. The storage test covers v13 loading without a write, disabled defaults for slots 6-15, a v14 round trip for slots 6 and 15, retained phone/Bluetooth settings, an unsuccessful save that preserves v13, and a damaged v14 record falling back to v13. The birthday test exercises simultaneous claims including slot 15. The web-profile firmware compiled with MP3, Bluetooth and Wi-Fi enabled; installation was still pending at that stage. The recorded ELF had 869,352 bytes of text and 163,804 bytes of BSS; its linker map exposed 512 KiB of main RAM and 4 MiB of Flash. A synthetic maximum-field status JSON with 15 birthdays and five alarms was 2,842 bytes including HTTP headers, below the 4,096-byte response allocation by 1,254 bytes. These are historical stage measurements, not a current worst-case size assertion after further status additions. Four-client load, sustained audio and expanded-record reboot persistence remain individually tracked hardware checks.

The optional `phone_sliders` check executes the production page script with a minimal DOM/network model. It covers in-flight edits, latest-value delivery, independent sliders, lost replies, a failed status request, command-ID conflicts, physical encoder updates, Apply-only language edits and editor guards. It does not render a browser or validate physical audio/brightness. Prior manual JavaScript checks for other browser workflows are described in the phone evidence; they are not additional registered CTest suites.

The current slider-script fixture also checks alarm-track audition commands and Test/Stop state transitions, including paused audition, ordinary music, draft preservation and stale-page stop handling. The optional `phone_timer_wheels` check covers the wheel ranges, keyboard/scroll/click selection, selected row, disabled state and continuous digit scaling. The combined Play/Pause action is tested against live player state.

On 2026-11-13, a clean local MSVC Debug rebuild passed all seventeen registered checks. The initial incremental run stalled in `timer_controls_test`; that process was stopped and all targets rebuilt cleanly. The passing result is from the clean run. The user reported that the latest changes worked correctly, without an exact build ID or per-scenario log. Record this as broad acceptance and retain individual expanded-slot persistence, long-duration, four-client and output/priority checks. No new firmware build or installation was performed for this documentation reconciliation.

## Firmware builds

The Android-style timer wheels passed the standalone Node.js `phone_timer_wheels` check on 2026-11-12, covering limits, keyboard selection, scrolling, clicking, selected-row state and disabled controls. `phone_sliders` also passed after this change, and the web-profile firmware rebuilt successfully. These checks use a DOM model and do not establish actual mobile rendering or touch behavior; those require browser/device acceptance.

Default build:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3
```

Bluetooth-only fallback:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -NoWifi
```

The script supports -BuildDirectory for an ASCII build path and -HelixPath for an existing external decoder checkout. It records the Git revision and marks tracked uncommitted changes as dirty. Release builds must have a clean source revision.

Experimental web profile on `codex/phone-control`:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -PhoneWeb -BuildDirectory C:\pico_clock_compile\phone-clock
```

`-PhoneWeb` requires Wi-Fi. Build-only and separately approved installation are described in the [main README](../README.md#experimental-phone-control-profile). Keep the exact UF2 and sidecar together; `update-pico.ps1` has no `-PhoneWeb` parameter. Detailed phone acceptance is in the [web checklist](../experiments/phone_control_ap/README.md#remaining-web-release-acceptance).

## Earlier web verification baseline (499e78c)

Source `499e78c` passed all sixteen checks with Node.js present. The web and ordinary Bluetooth/MP3/Wi-Fi profiles built successfully with identified, clean source build IDs. UF2 structure, RP2350 family, the E10 marker and image bounds below clock settings were checked. A real 390-pixel browser preview showed four sliders without horizontal overflow. These are automated/desktop checks, not sound-quality measurements.

The approved COM8 installation verified build `499e78c94a0c-bt-mp3-wifi-phone`, Flash guard OK, unchanged valid v12 slot sequences 134/135 and NTP synchronization. Five alarm and five birthday row checksums matched before and after installation. HTTP returned four range inputs and the current saved settings. The user subsequently reported that everything worked. This accepts the slider workflow broadly; the remaining access, power-cycle, optional-year, concurrency and network/output scenarios remain in the phone checklist. See [hardware evidence](HARDWARE_TEST_EVIDENCE.md#phone-control-acceptance-through-build-499e78c).

## Automatic firmware version

Version strings are generated from reachable local Git release tags matching vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease. Both lightweight and annotated tags are supported. The generator runs at configuration and before each firmware build; no manual version edit or cache reset is required.

- Clean checkout at v1.0.0: USB version is 1.0.0; About shows v1.0.0.
- Three commits after that tag: USB version is 1.0.0+3.gHASH; About shows v1.0.0+.
- Tracked local edits append .dirty to the USB metadata; About also shows +.
- No reachable release tag: 0.0.0+gHASH, with + on About.
- No Git metadata or Git executable, including source archives: 0.0.0+unknown. An archive does not claim the current release version. Use a Git checkout for an identified release build.

About reserves the second line for by FlintUA; version text is bounded to the LCD's 16 columns. USB retains the full version. The existing build ID separately identifies source revision and enabled profiles.

Fetch tags before building if the checkout does not have them: git fetch --tags. A shallow checkout must include a reachable release tag, or fetch full history using git fetch --unshallow --tags. Tag creation alone does not alter firmware already installed on a board; rebuild and flash it.

## Device acceptance and follow-up checks

### External LCD illumination

Start with a v10 settings record and compare pre/post alarm, birthday, Bluetooth, audio and NTP status. On first boot, `display-light-status` should report OFF, day 100%, night 20% and applied 0%. Loading alone must not migrate the Flash record.

Open Settings → Display Light (10). Confirm OFF, then test ON at a low level and at 100%. Select AUTO with different day and night levels. Check RTC transitions at 07:00 and 22:00 and verify that toggling audio Quiet mode or mute has no effect on the light. Test MINUS and idle timeout before final confirmation. Check that the previous mode and levels remain in effect.

Wait for Flash autosave after a committed edit, restart and verify the settings, applied level and existing configuration. Check readability in a dark room, listen for PWM noise on DFPlayer, and verify Bluetooth MP3 playback and DS1302 readings while the light is on.

### Bluetooth explicit disconnect/scan policy (basic manual actions accepted)

On installed build 7378aa1, the user confirmed connection/disconnection, then a power cycle followed by scanning without automatic reconnection. OFF persistence and this scan scenario are user-accepted. Other detailed scenarios below remain regression/coverage checks, not individually confirmed results.

With Portable connected and audio idle, choose SETTINGS -> Bluetooth -> Disconnect (last entry, one CCW step from Scan devices) or send bt-disconnect. Expect auto=OFF, LOCAL fallback and both saved names/keys retained. Close settings, wait for autosave=OK, then confirm no reconnect after at least the longest existing retry interval (30 seconds). Power-cycle only after saving and verify OFF persists. Incoming reconnect attempts from Portable must not establish A2DP or start the tone.

From a connected idle state, start discovery. Confirm auto=OFF and the previous link closes; check natural scan completion, MINUS cancellation, exit and the 20-second menu timeout separately. None should restore Portable. Compare bt-known before/after; names/pairing must remain. Save before reboot and repeat OFF verification. When browsing without actually scanning/disconnecting, an existing ON/retry policy should remain unchanged.

Select Portable or Nest and confirm Connect (or use bt-use N/name/MAC). Expect normal connection, auto=ON and target persistence only after ACTIVE. Check saved-device selection without scanning, accidental speaker power loss/recovery, rapid disconnect followed by Connect/scan, and no stale callback reconnect to the previous peer. Verify independent LOCAL/BT volumes, Manual/hourly/timer/alarm preferences, birthdays and Wi-Fi/NTP remain unchanged. Current audio/timer/alarm ownership must refuse disconnect/scan safely. This update does not establish the cause of the intermittent double speaker sound; listen/log separately.


### Annual birthdays (UI/save broadly accepted; timed/audio coverage remains)

Build 7964939 was installed with separate approval. The user broadly accepted the feature, and COM8 confirms configured dates in a v10 record alongside the previous v9 fallback, with prior settings retained. Exact firing/expiry and output-specific melody cases below are still coverage checks.

For the current fifteen-slot branch, retain birthday-status, alarm-status, audio-settings and bt-known snapshots before upgrading. Loading v1-v9 must create fifteen disabled, valid January 1/09:00 defaults; v10-v13 must retain their five dates and initialize slots 6-15 disabled. Loading alone must not write Flash. Compare alarms, masks, volumes, outputs, speaker names/pairing and network status. Never print Wi-Fi passwords.

Open Birthdays (item 9), navigate all fifteen slots and edit ON/OFF, month, day, hour and minute, specifically checking labels and edits for slots 6, 10 and 15. Check blinking cursor, back navigation, draft cancellation/timeout, month clamping and February 29. Save a future occurrence in an extra slot, wait for autosave, reboot and verify its date, optional birth year and all previous settings. Nondefault extra slots require v14; use a v14-compatible ordinary or -NoWifi profile for fallback and consult NETWORK_TIME.md before reverting.

With the clock idle, use birthday-test N on LOCAL and BT (the file 07/000.mp3 must exist on both cards). Confirm the expected birthday melody, label and fallback, with no changes to the saved date/year. Clear pending tests with birthday-pending-clear. Schedule an actual event for a future minute today to check due/last_year, one playback and persistence after reboot. Test two simultaneous slots sequentially. Muted/quiet/music/timer/alarm/menu states must defer instead of interrupting; expiry at 30 minutes is covered by host tests and still needs a timed device check. Restore/disable test dates when finished. BOOTSEL must refuse while a birthday is pending. Yearly and leap-day recurrence should use host tests rather than changing a live clock merely to simulate years.


### Date/next-alarm/startup update (broad user acceptance)

Power-cycle and check the short version splash, responsive controls and subsequent normal clock. Confirm no new startup sound beyond existing Bluetooth behavior. On an idle clock, MINUS short should show/announce the date; hold at least 900 ms should show/announce the next eligible alarm. Check both languages and Manual=LOCAL/BT, required date folders on both cards, full date versus supported spoken year, and LOCAL fallback if a Bluetooth date segment is unavailable. USB say-date and next-alarm offer repeatable requests without changing schedules.

Using existing schedules, compare the forecast with alarm-status and today's weekday. Confirm the earliest enabled match and the date on LCD. For changes to schedules, retain the original values and restore them afterward. Check an active snooze as the nearer occurrence, no-enabled-alarm display, mute (LCD without speech), no interruption of a timer/alarm/player, MINUS back/Previous/hold-to-exit behavior on the existing screens, and intact MODE time announcement. Host tests cover recurrence/calendar logic, but RAM-snooze forecast and actual speech/routing require device acceptance. Build 02b4a51 was installed with separate approval, and the user confirmed the functionality works. The detailed cases above remain regression/coverage checks; the report did not enumerate every case.


### Player pause and sleep update (partial user acceptance)

The user reported pause and sleep-time selection working in build 38182d4 and accepted the explicit OFF display in build fa38eaf. Output-specific coverage and full expiry remain open.

On both Manual=LOCAL and Manual=BT, start a recognizable long track, pause after several seconds with TIMER, wait, and resume. Expect PAUS on LCD and continuation from the same passage rather than the beginning. Check rapid double presses, pause immediately after opening/Next, volume adjustment during pause, EOF after resume, and Next/Previous from pause. Confirm SHUF/NORM/REP1 continue to behave as before.

Press GP28 to select 15, 30, 60, 90 minutes and OFF. Verify OFF is visible on entry and after cycling back to disabled, always in the same three-character position as the remaining minutes beside PLAY/PAUS/MUTE/WAIT/STOP, and track/mode changes keep the deadline. Check a full 15-minute expiry while playing and while paused: silent stop/exit, clock visible, no effect on saved volume/output/alarm settings, Bluetooth stays connected. Confirm OFF cancels, reselection resets the deadline, and exit/reopen resets sleep to OFF. Host tests cover exact expiry without a real-time wait.

While paused, exit with a supported hold or mute and restart; these intentionally discard position. Test an alarm interrupting paused music (alarm must take priority), then reopen the player. Disconnect BT while paused: no automatic LOCAL playback, next TIMER starts the same track from the beginning on LOCAL. Settings/Flash formats and pairing keys must remain unchanged. No pause position or sleep deadline is restored after reboot.


The user confirmed weekday setup/firing and the full delayed repeat on the earlier five-minute build, then reported the three-minute version works after its separately authorized installation.

Cursor acceptance: check time/date H/M/S and D/M/Y positions, precise timer fields, alarm hour/minute, track/sound/day preset and custom-day ON/OFF, language, quiet hours, half-hour and exact-hour toggles. Confirm focus follows a field even when its text does not otherwise change, continues blinking at the selected value after rotation/redraw, and disappears on save/cancel/timeout and all non-editing screens. Check ordinary Bluetooth playback and button responsiveness alongside LCD interaction. The user accepted steady underline focus in build a0d5a9e. The user also accepted hardware blinking focus in build 755f55b. The user accepted removal of the redundant Volume editor in build 4bad974. Regression checks: confirm the profile-specific menu order in CONTROLS_AND_MODES.md, wraparound between About and Language, and access to Audio Output (7), Bluetooth (8), Birthdays (9) and Display Light (10). The ordinary profile has About (11); the web profile adds Phone Control (11) and About (12). Check that the left encoder still adjusts the active output in and outside settings, retains independent LOCAL/BT levels and shows the volume overlay; its button must still toggle sound.

Weekday/snooze stage: the COM8 smoke test confirmed v8 loading with Daily on all slots, unchanged alarm fields/output settings, Portable reconnection, and USB snooze schedule/cancel without a Flash write. Remaining checks: configure Daily, Mon-Fri, Sat-Sun and a custom mask using LCD/USB; test draft cancellation and rejecting an empty custom selection; save/reboot and check masks, alarm times/enabled/sounds, volumes, known speakers and network settings. Use alarm-audio-test N to ring a slot without changing its schedule; physical GP28 should stop it and ring once after three minutes. Verify repeated snooze, ALARM cancellation on the main screen, disabling/editing a snoozed slot, restart cancellation, timer/mute deferral and music preemption. Check both LOCAL and BT. These remaining paths need device acceptance; weekday/migration/snooze host tests have passed.

Restored timer controls require focused device checks: GP28 editor entry/cancel, HOUR/MIN/SEC confirmation using TIMER/SETTINGS, MINUS/back, timeout cancellation, one-second completion and the new 23:59:59 limit. Check fast rotation and direction reversal in quick selection. In the music player test mute/unmute on LOCAL and Bluetooth: music stops, levels remain intact and unmute requires explicit playback. The latest general workflow was accepted on November 13, but these individual physical/output scenarios and a full-length countdown were not enumerated.

For the extended player, check LOCAL and BT with Portable/Nest: automatic play on entry, mode cycling without restarting the current song, NORM advancement, REP1 at EOF, explicit Next in REP1, Previous/forward history, rapid encoder changes, PLAY/PAUSE/resume, mute and all three hold-to-exit paths. Check automatic EOF advancement and Bluetooth loss: current track restarts on LOCAL without a playlist skip. Verify all output menus/statuses say BT and both bt/jbl CLI spellings work. The user accepted the earlier playback/navigation controls; the new pause/sleep transport paths still need hardware acceptance. Host tests do not replace it.

The user subsequently confirmed the extended player works. The additional MINUS hold exit still needs a device check: short release selects Previous; release after at least 900 ms exits without starting another track.

## Long USB Bluetooth capture

Close other COM monitors, then run tools/capture-pico-log.ps1 -Port COM8 -Minutes 30 -LogPath build/diagnostics/bluetooth.log. The capture records all received lines with PC UTC timestamps. It sends only version, bt-status, audio-settings and ntp-status once at startup; it does not scan, pair, play or flash. An optional -StopFile path ends capture when that file appears, releasing the port. Record the audible event time to distinguish A2DP suspend/start, signaling reconnection and speaker-local sounds. Logs under build are ignored by Git.

Choose a new log filename for each run; existing logs are preserved. The SDK labels a suspended A2DP stream as Stream paused. The full raw log is always recorded even if a line is absent from the concise console filter. A COM capture does not record sound itself.

Follow [network acceptance](NETWORK_TIME.md#hardware-acceptance) and [firmware verification](../firmware/pico2w_c/README.md#on-device-verification). In addition:
- Start discovery, select no new device and exit. Reconnect must remain OFF until an explicit Connect.
- Find a saved speaker again; it must appear once and keep its useful name even if discovery reports Unknown.
- Test a music alarm above track 45 with a real matching file.
- Check version after updating and retain the UF2 build-ID sidecar.
- Run the assembled clock overnight with the actual access point, Portable/Nest, RTC, SD and DFPlayer.

Earlier hardware passes are retained in [HARDWARE_TEST_EVIDENCE.md](HARDWARE_TEST_EVIDENCE.md); they do not imply that newly added features have passed device testing.

## November 13 web interface revision

All seventeen host checks passed after continuous timer-digit scaling, combined player controls and alarm track audition were added, and the clean documentation-review run also passed. Browser-script checks cover fractional scroll positions, Play/Pause/Resume action selection and Test preserving the unsaved draft and saved alarm record. Native access checks cover audition bounds, endpoint restrictions and idempotent retries. The MP3/Bluetooth/Wi-Fi/web profile was built during implementation. The user subsequently reported successful testing of the recent improvements on November 13. This is broad acceptance; natural audition completion, output routing, paused Test/Stop and priority scenarios remain individually tracked. The exact installed build ID was not provided, and no firmware was built or flashed during this documentation review.

## Browser About follow-up (installed; user accepted)

The later About implementation adds a read-only `firmware` object to `/status.json`: `name`, `version`, `build`, `board` and `author`. Version and build use the same generated header as USB `version`. The collapsed About section is available before pairing. It shows full metadata using text nodes, updates after status refresh and reports Unavailable on disconnect or when an older response lacks metadata. Long build IDs wrap.

All seventeen checks passed after this addition. The production-script regression verifies metadata rendering, a changed version/build, lost status and recovery, and older responses without the object. A one-off native harness compiled the actual C status serializer and revision functions, populated five alarms/fifteen birthdays and boundary-value status fields, parsed the JSON and matched its version/build against the generated header. That fixture used 2,897 bytes including headers, within the existing 4,096-byte allocation with 1,199 bytes remaining. This is a boundary fixture for this build, not a four-client RAM measurement or an unlimited-version-length bound.

The Bluetooth/MP3/Wi-Fi/web firmware built as `697c062da090-dirty-bt-mp3-wifi-phone`, full version `1.0.0+23.g697c062.dirty`. The dirty marker reflects the existing uncommitted development changes; it is not a uniquely committed release identity. The selected UF2 is 1,742,848 bytes, SHA-256 `1F2A16509C308BB9FB9881D03C0E5CD2496E0FB0256E7C482F9018CC502551D6`. Its RP2350 blocks, E10 marker, image bounds below settings and embedded build identity were checked. After separate user approval it was installed through COM8; startup/Flash, Wi-Fi/NTP, served About metadata and unchanged schedule/settings checksums passed. See hardware evidence for details.

The user subsequently confirmed that About worked on the phone. Long build wrapping and information recovery after reconnect were not individually enumerated. A later clean committed build will have a different identity and requires its own verification before installation.

## Extended overview, substage 1 (installed; generally accepted)

This is a read-only addition to `/status.json` and the existing page. Calendar/snooze projection reads existing state without claiming events, requesting NTP or changing settings. NTP age/RTC application metadata is RAM-only; no Flash format change is needed. Mute, information-speech and live alarm control commands are reserved for subsequent substages.

Eighteen registered checks pass. Native checks cover calendar/tie/weekend selection, missing/invalid RTC, snoozes crossing the 32-bit millisecond boundary, safe name serialization and bounded output. Browser execution covers active/queued/snoozed states, NTP application/retry, direct mode, missing status and reconnection. A real Chrome mobile emulation at 360 pixels confirmed the date/forecast/NTP rows and no horizontal overflow; its status data was a fixture, not a device connection.

A one-off native harness compiled the actual full status serializer, revision functions and overview module with five alarms, fifteen birthdays, boundary-value counters and a 24-byte fully escaped Bluetooth name. Its JSON parsed successfully and occupied 3,597 bytes including headers, leaving 499 bytes within the unchanged 4,096-byte allocation. This is a fixture bound for the current version strings, not an unlimited tag-length or four-client memory proof.

Both web and ordinary Bluetooth/MP3/Wi-Fi profiles build. For this working tree, GNU size reports web text 884,696 bytes/BSS 163,812 bytes and ordinary text 816,528 bytes/BSS 120,192 bytes. These static figures do not measure peak stack/heap or free RAM. The web profile retains the 48 KiB lwIP heap. The final clean Debug run passed all eighteen checks. The build remains based on uncommitted development work and carries the dirty marker; identify the exact UF2 by its hash before installation.

Reviewed web candidate: 1,757,696 bytes, SHA-256 `1CEBED741FD86D248A0ABDAF7A6D38412AF64F4141AC443B4131A5A48FD78E95`, build `697c062da090-dirty-bt-mp3-wifi-phone`. The version/build text is shared with earlier dirty images, so the hash and new overview contract distinguish this candidate. Embedded identity, UF2 blocks/family, E10 marker and Flash bounds below settings passed verification.

The user approved installation of the reviewed artifact. COM8 startup/Flash, home Wi-Fi/NTP and read-only HTTP checks passed; pre/post alarm, birthday and preference checksums matched. The user subsequently accepted the general overview. See HARDWARE_TEST_EVIDENCE.md for observed fields. Individual ringing/snooze, reconnect and network/output combinations were not enumerated and remain focused coverage; broad acceptance does not close every scenario.

## Quick controls, substage 2 (generally accepted)

The `/control` endpoint adds explicit sound state and time/date/next-alarm speech to the existing authenticated shared queue. Nineteen registered checks pass. Tests cover correct/wrong endpoint actions, values and scope, concurrent timer/player requests, idempotent retries and cached detailed results. Production browser-script checks exercise hidden unauthorized controls, sound state/independent volumes, busy/RTC/no-alarm feedback, manual status updates and two lost responses after one accepted speech request. Applied controls wait for the matching snapshot.

A one-off host harness compiled the actual `set_sound` and `service_phone_control` bodies from main.c with stubbed transports and the production guard module. It verified repeated explicit OFF is inert, ON does not restart music, editor/player/alarm guards and RTC/no-alarm return mapping. This is not a physical transport test. Existing speech-plan tests cover phrase construction/routing data.

Both web and ordinary profiles build. A real Chrome fixture at 360 pixels displayed the four enabled quick buttons without horizontal overflow. The full C serializer with the longest new refusal result (`rtc_unavailable`), five alarms, fifteen birthdays and boundary overview fields stayed within 4,096 bytes; the pre-commit dirty-identity fixture used 3,611 bytes (485 bytes remaining). The final clean-identity fixture used 3,599 bytes (497 bytes remaining). Static figures and fixtures do not establish four-client audio/heap performance.

The development changes were consolidated in commit `38a8743a6cea3b2382570dafe93b4293cf18ae9a`. After explicit user approval, version `1.0.0+24.g38a8743` was installed on November 13. USB and HTTP identify the expected clean build. Flash slots are valid, Wi-Fi/NTP and the Bluetooth stream recovered, and saved settings match their pre-install checksum. COM8 was released. See HARDWARE_TEST_EVIDENCE.md for the artifact identity.

Device acceptance: sound on/off from browser and physical button; preserved separate volumes; mute during LOCAL/BT music and no auto-resume; a countdown continues through mute; manual time/date/nearest-alarm speech in RU/DE on both routes; busy, muted, missing RTC and no-next-alarm feedback; safe lost-link recovery; sound changes blocked during ringing. Do not change saved schedules or disconnect the router merely to force failures without a targeted test plan. Installation is verified and the user subsequently confirmed general operation. Individual route/language/failure cases above were not separately reported. Speak next alarm uses the correct upcoming alarm but the existing fired-alarm introduction is misleading; phrase replacement is deferred until functional development and the planned audio refresh.

## Live alarm actions, substage 3 (user acceptance pending)

Nineteen registered tests pass. Expanded native tests exercise same-slot stale occurrence IDs, matching/missing occurrences, changed/empty snooze sets, sleeping-to-audio-waiting transitions, stable revisions during polling, revision rollover, uint32 range checks, endpoint isolation, trusted-home authorization and cached refusal retries. Production browser-script tests cover button availability without RTC/menu/sound prerequisites, successful stop/snooze/cancel, preserved saved schedules, a lost-response snooze retry, stale occurrence/set conflicts and disabling after status loss. Both integrated web and ordinary BT/MP3/Wi-Fi builds pass.

A one-off host fixture compiled the actual `schedule_json` and `fs_open_custom` serializers with production overview/access/calendar code and stubbed allocation/peer checks. Five alarms, fifteen birthdays, escaped Bluetooth names, maximum uint32 IDs/counters and the longest result string produced a valid 3,656-byte HTTP response (440 bytes free) with the preliminary dirty build identity. The installed candidate must use a committed version and its reviewed UF2 hash. This fixture verifies capacity/JSON, not lwIP allocation under real concurrent load.

After separate installation approval:

1. Refresh the phone page. When idle, Stop/Snooze/Cancel are disabled and normal saved schedules remain unchanged.
2. Let a deliberately scheduled alarm ring. Confirm its slot is visible and Stop alarm silences it; the saved enabled state/weekdays remain unchanged.
3. At a later occurrence, choose Snooze 3 min. Confirm sound stops, the repeat countdown appears, and the alarm rings again after three minutes. Stop that occurrence.
4. Snooze another occurrence and choose Cancel snoozed repeats. Confirm the repeat disappears and does not ring at its former deadline; the original schedule stays enabled.
5. Compare physical ALARM/GP28 behavior with the browser. Check LOCAL and Bluetooth when convenient. Test two browsers changing the same occurrence without allowing an old request to affect a new one.

Do not claim these device cases completed from host tests. Approved installation and startup/HTTP/settings checks passed on November 13 as `1.0.0+25.g97fd951`; physical audio/repeat timing and user acceptance are pending. No alarm schedule was changed on the connected device during development.

## Countdown completion audio fix (generally accepted)

All nineteen registered checks pass with expanded command-parser and production-browser tests. Cases include nonzero uint32 event IDs, wrong endpoints, home-peer authorization, cached retries, stale/new/ended completion events, button availability and feedback, countdown-completion versus scheduled-alarm separation, and compatibility with older status. The one-off native harness compiles the actual `clock_audio_clear`, `clock_audio_enqueue`, `timer_finish_arm`, `timer_audio_finished`, `timer_finish_refresh` and `service_phone_timer_sound` helper bodies from main.c with stubbed transports. It passes LOCAL, Bluetooth, LOCAL fallback, stale/ended/new-event refusal, replacing/appending audio, mute, timer/alarm priority and serial rollover cases. Both firmware profiles build. These are software checks, not a physical transport or responsiveness test.

After separate installation approval, refresh the page once to load the new code. Start a short countdown, leave the page open and watch it transition to Finished / announcement playing. Stop timer sound should enable automatically and interrupt current completion audio without a cancellation phrase. Repeat on LOCAL and Bluetooth, allow one completion to finish naturally and confirm the button disables. A stale click must not stop another event. Report any page jump and whether the connection status changed at that moment. Scheduled-alarm Stop/Snooze/Cancel still needs its own alarm-at-a-time test, rather than a countdown test. Approved installation of `1.0.0+26.g3717033` passed startup and read-only HTTP/settings checks on November 13; the user subsequently confirmed correct operation. Specific output, failure and load cases were not individually reported.

## Independent timer completion choices (user acceptance pending)

All nineteen registered checks pass. Speech plans cover both languages, both message states and all three sound choices, including no partial append on capacity failure, invalid values, ring-buffer wrap and unchanged legacy defaults. Flash tests read older records without writes, apply signal/message/track defaults, retain v14 size/offsets, round-trip v15 (including fully silent preferences), preserve extra birthday slots/years, browser credentials, known Bluetooth devices and Wi-Fi, reject invalid fields even with a recomputed CRC, survive write failure, and fall back to the older A/B record after corruption. Parser tests check ranges, wrong endpoints and home-peer authorization; production browser tests check checkbox on/off, sound/track application, busy guards and older-status compatibility. Both profiles build.

The one-off native completion/stop harness compiles the real main-loop finish/ownership handlers with a stubbed planner/transport and verifies all six selection combinations on LOCAL and BT, including no owner for an empty plan; production planner contents are independently checked by speech_plan. The full production HTTP serializer fixture with maximum IDs/counters, five alarms, fifteen birthdays, escaped names and all new preference fields uses 3,734 bytes of 4,096 with the preliminary dirty identity (362 free). Four-client radio/heap latency remains a separate hardware test.

After separate installation approval, verify the legacy defaults before editing. Test RU message only, message+signal, message+one existing music track, signal only, music only and silent completion; start/cancel phrases should retain their behavior. Test Stop timer sound on both signal and music; wait for Settings saved and power cycle to verify selection/track persistence. Check physical menu confirmation/back/timeout and browser controls on the chosen output, then the alternate output when convenient. Verify DE never requests missing completion phrases: selected signal/music still plays; DE message-only is silent until assets exist. Keep other saved schedules/levels unchanged. v15 persistence and physical file playback are not established by host tests.

## Random timer and alarm music (user acceptance pending)

See [RANDOM_EVENT_MUSIC.md](RANDOM_EVENT_MUSIC.md) for exact behavior and device steps. Twenty-one tests pass after a clean host rebuild (needed after extending the snooze structure). New `music_catalog` and `sd_catalog` tests exercise sparse numeric IDs, exclusion/deduplication, singleton/no-repeat choice, scanning beyond the old print limit, read failure and corrupt chain handling. Expanded access/browser/Flash tests cover random modes, collision-free revision bits and v16 configuration round-trip with previous data retained. A one-off harness compiles real main-loop handlers and verifies stable occurrence choices through snooze, timer preemption and BT-to-LOCAL fallback. Both profiles build. Actual cards, physical timing, persistence on hardware and four-client load remain unverified.

## Optional timer pre-warning (user acceptance pending)

All 22 tests pass after clean host rebuild. See [TIMER_PREWARNING.md](TIMER_PREWARNING.md) for exact scheduler, owner/transport harness, v17 migration/corruption and browser/parser coverage, plus the pending physical test sequence. The numeric asset 19/001.mp3 was inspected locally: 11,612 bytes, 0.197333 seconds, 48 kHz mono; asset bytes were not changed. Begin device acceptance with 15-20 seconds so timer-start speech does not consume the warning window. Neither hardware cadence nor v17 power-cycle persistence is claimed from software checks.

## Scalar autosave, mobile checkbox and LOCAL warning fix (installation pending)

See [WEB_SCALAR_SETTINGS.md](WEB_SCALAR_SETTINGS.md) for the control audit and pending phone/audio checks. Expanded production-JS tests exercise select/checkbox/number auto-commit, rapid replacements, lost responses, refusal rollback without later replay and timer Start guards. Real Chrome fixtures at 360/412/960 pixels with default and enlarged text verify native toggling/saving, no overflow, a stable 20px box and retained record confirmation; the old enlarged-text fixture shrinks to zero at 360. Actual warning-owner code passes simulated delayed BUSY onset/completion and prior cadence/zero/cancellation checks. All 22 registered checks and both builds pass. Actual phone operation and acoustic improvement remain unconfirmed.

## Browser player mode and sleep

See [player options](WEB_PLAYER_OPTIONS.md) for immediate mode selection, sleep semantics and focused acceptance. Validate both LOCAL and BT, physical/browser reflection, paused expiry, command retries and audition/closed-player guards. Installation and hardware acceptance are pending.

## Browser automatic announcements and quiet hours

Follow [announcement acceptance](WEB_ANNOUNCEMENTS.md). Check runtime automatic OFF versus reboot ON, saved mode/half-hour/exact/quiet preferences, daytime/overnight/equal-hour ranges, physical-to-browser reflection and normal timer/alarm availability. Do not infer beep acoustic acceptance from status or software tests. Installation and user acceptance pending.
