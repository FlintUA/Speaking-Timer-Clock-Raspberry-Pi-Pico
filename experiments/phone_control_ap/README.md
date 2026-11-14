# Phone control: integrated firmware and Wi-Fi prototypes

Current development branch: `codex/phone-control`. Stable v1.0.0 does not include the browser interface. The integrated profile runs the clock, Bluetooth, MP3 and optional NTP alongside local HTTP control. The standalone targets described below are earlier network experiments and replace the clock application when installed.

## Current integrated workflow

1. From the repository root, build `build-clock-bt.ps1 -Mp3 -PhoneWeb`, optionally using `-BuildDirectory C:\pico_clock_compile\phone-clock`. This builds only; `-NoWifi` cannot be combined with `-PhoneWeb`.
2. Install the selected UF2 with `update-pico.ps1 -Port COM8 -Uf2 <absolute-UF2-path>` after approving the build. Keep its `.build-id.txt` sidecar alongside it. The updater has no `-PhoneWeb` switch; `-Build -Bluetooth -Mp3` rebuilds the ordinary profile instead.
3. Use the existing home Wi-Fi setup or provision it with the root `configure-wifi.ps1`. Open **Settings -> Phone Control (11)** on the clock and browse to its numeric HTTP address. **About is item 12** in this profile.
4. For direct operation without a router or Internet, select **Start direct?**, join **SpeakingClock** with the displayed key and open `http://192.168.4.1/`. Direct and home are alternative modes. Returning home disconnects the direct network; reconnect the phone to the home network and use the home address.
5. Pair the browser once in each mode where needed, or enable home trust for code-free control from the home subnet. Close physical settings before issuing control commands. Access rules and remaining checks are detailed below.

Current page functions: live clock/timer/player/output status; one 00:00:01..23:59:59 timer with Set/Start/Cancel; Previous, combined Play/Pause and Next in one row, with Stop centered below; independent LOCAL/BT volume and day/night brightness sliders; language, audio preferences and light mode; five alarm and fifteen annual birthday editors. No rotation gesture or track seeking is provided. Playback mode and sleep controls are installed and generally user-accepted; see [player options](../../doc/WEB_PLAYER_OPTIONS.md). Automatic/quiet-hour/half-hour/exact-hour settings are implemented in the next candidate ([details](../../doc/WEB_ANNOUNCEMENTS.md)); Bluetooth device management and manual time/date remain planned. Spoken-information requests are available in Quick controls.

## Quick controls (substage 2; generally accepted)

The authorized page adds Turn sound on/off, Speak time, Speak date and Speak next alarm. Commands use the authenticated `/control` endpoint with the same credential/epoch/id/nonce envelope and queue as timer/player/settings. Actions are `sound 0|1`, `say_time 0`, `say_date 0` and `next_alarm 0`. They are rejected on other action endpoints.

Sound is an explicit requested state, not a toggle-on-retry. It preserves LOCAL/BT levels and the timer/saved schedules, stops current music without automatically resuming it on unmute, and remains RAM-only with sound ON after reboot. An ordinary alarm first becoming due while sound is OFF is suppressed rather than replayed on unmute; already-deferred events and snoozes retain their existing waiting/expiry rules. Like the physical volume button, the web sound action is allowed in menus/player/countdown but blocked while an alarm is ringing. Live alarm stop/snooze controls are implemented in substage 3 below, separately from these speech controls.

Speech uses the existing manual routing/language/LOCAL fallback. It is refused while muted, editing, counting down, with the player open or otherwise busy. RTC failure and no eligible next alarm have explicit results. Quiet hours do not prevent manually requested information speech. A successful acknowledgement means requested/queued, not proof of completed audible playback or a present audio file.

The page explains disabled actions, sends explicit on/off, rechecks fresh state on the clock and waits for a matching published snapshot before confirming applied controls. Retried speech requests retain their original ID/nonce and cannot enqueue twice. These operational actions do not dirty settings or write Flash.

Approved installation on November 13 and read-only USB/HTTP/settings checks passed. The user subsequently confirmed general operation. Focused cases not individually reported remain: test sound from browser and physical button, independent levels, mute/unmute during LOCAL/BT music with no automatic restart, speech in both languages/routes, no-alarm/RTC/busy feedback and phone reconnect. Restore intended sound state and schedules. Bluetooth connection itself still uses the clock/USB until the planned browser device-management stage.

## About in the integrated page

The integrated page has a collapsible About section below the controls. It displays the device name, Pico 2 W board, full tag-derived firmware version, source/profile build ID and author FlintUA. These are read from the current device's `/status.json` `firmware` object, not hardcoded into the browser. They are available before pairing. A lost connection or older firmware without that object shows Unavailable; subsequent status recovery updates the fields. Build strings wrap on narrow screens. After separate approval on November 13, installation and read-only USB/HTTP checks passed with settings unchanged. The user subsequently confirmed About worked on the phone. Specific wrapping/reconnection scenarios were not individually reported.

## Extended overview status (substage 1; generally accepted)

The existing status response now includes an additive `overview` member, published from the main loop alongside the existing controls. It introduces no control endpoint or Flash-format change.

| Group | Meaning |
| --- | --- |
| `rtc_valid`, `date`, `weekday` | Validated local RTC date in YYYY-MM-DD and computed Monday=1..Sunday=7. Invalid RTC is explicitly unavailable. Browser time zone does not determine this date. |
| `sound_on` | Actual global sound state, separate from both stored volume levels. This substage displays it but adds no mute command. |
| `alarm` | Active slot (0=none), deferred count, nearest scheduled/snoozed slot and local civil timestamp, snooze count, nearest repeat/remaining seconds and a due-but-waiting indicator. Snoozes use device monotonic time; a forecast is unavailable without a valid date, while repeat remaining time remains available. |
| `bluetooth` | Unsupported/OFF/scanning/waiting/connected state and a display name. Waiting does not claim an active connection attempt. MAC addresses are not included. Names are JSON-escaped, valid UTF-8 is preserved and invalid bytes are replaced. Connection state is distinct from effective audio output. |
| `network` | Home/direct mode, home join/address/retry/ready status and its elapsed/remaining seconds. Direct mode is shown as a local network. |
| `ntp` | Availability/configuration, reference received, successful RTC application, pending application/request/retry, reference age, last application age and applied UTC offset. Ages use uptime, not the phone clock. |

The browser distinguishes a received NTP sample from a successful clock update. A failed RTC application reports retry pending. Direct mode does not request NTP; a previous successful update can still be shown with its age. The existing USB `ntp-status` synced flag describes the reference anchor, whereas the new browser `synced` flag specifically describes successful application.

The Activity row explains a physical editor, active alarm, running timer, open player or other busy audio. Existing command guards remain authoritative. Connection loss clears the additional rows to Unavailable, and older firmware without `overview` remains usable. These fields, like existing status, are read-only and available before browser authorization.

After approved installation, startup/Flash, read-only HTTP and settings-preservation checks passed. The device supplied the expected date/weekday, next alarm and home/NTP state. User acceptance: compare time/date/sound and nearest alarm with the clock; turn the physical sound button; inspect active/snoozed state during a normal alarm test; compare Bluetooth name with its actual connection; confirm home/direct and NTP messages and recovery after a phone reconnect. This substage adds no alarm stop/snooze buttons. Four-client/audio continuity and the existing persistence checks remain separate acceptance items.

## Standalone prototype scope

The two standalone Pico 2 W targets serve the same static page with the Pico SDK's lwIP HTTP server. CMake embeds one HTML source in both images. The AP target leases an address to the phone. The STA target obtains its address from the home router.

## Build on Windows

Use Pico SDK 2.3.1 and Raspberry Pi pico-examples revision `0d62f75bafc2c8120d3276c3343d1a9195e909e9`. The example checkout provides the DHCP server and common lwIP options. Adjust the paths for the installed compiler and tools.

```powershell
$sdkRoot = Join-Path $env:USERPROFILE '.pico-sdk'
$env:Path = "$sdkRoot\toolchain\15_2_Rel1\bin;$sdkRoot\ninja\v1.13.2;$env:Path"
$cmake = "$sdkRoot\cmake\v4.3.4\bin\cmake.exe"
$build = 'C:\pico_clock_compile\phone-ap'
& $cmake -S experiments/phone_control_ap -B $build -G Ninja -DPICO_BOARD=pico2_w "-DPICO_SDK_PATH=$sdkRoot/sdk/2.3.1" "-DPICO_EXAMPLES_PATH=$sdkRoot/pico-examples" "-Dpicotool_DIR=$sdkRoot/picotool/2.3.1/picotool" "-Dpioasm_DIR=$sdkRoot/tools/2.3.1/pioasm"
& $cmake --build $build --target clock_phone_ap
& $cmake --build $build --target clock_phone_sta
```

The outputs are `clock_phone_ap.uf2` and `clock_phone_sta.uf2`. These standalone targets contain no clock controls, settings storage, Bluetooth, MP3 or NTP service.

## Direct Wi-Fi hardware acceptance

1. Before installing the prototype, keep a verified Speaking Clock UF2 available for restoration. The prototype temporarily takes the place of the clock application on the board. Use manual BOOTSEL to load the prototype and restore the clock firmware. The clock updater expects application diagnostics that this image does not implement.
2. After installing the prototype, open its USB serial port at 115200 baud. It prints the SSID and a new WPA2 password generated at each boot. Send `?` to show them again. Keep the password in the local terminal.
3. Turn off mobile data, join `SpeakingClock` and stay on the network if the phone reports that it has no Internet access. Open `http://192.168.4.1/` in the browser. Expect the Speaking Clock page with the direct-connection message.
4. Leave the Wi-Fi network, rejoin it without restarting the Pico and reload the page. If the Pico was restarted, forget the saved network and join again with the new password.
5. Restore the clock firmware and verify that its saved configuration is still present.

On 2026-11-10, the user joined the SpeakingClock network with a phone and confirmed that the test page opened in the browser. The USB log showed the DHCP server assigning the phone an address on the prototype network.

Rejoining after leaving the network and checking the saved clock configuration after restoring the clock firmware remain separate checks.

## Home Wi-Fi hardware acceptance

1. Install `clock_phone_sta.uf2` through manual BOOTSEL. After it starts, close any serial monitor using the prototype's port.
2. From the repository root, run the local setup script. The password entry is masked and the prototype uses the credentials for this boot only.

```powershell
powershell -ExecutionPolicy Bypass -File .\experiments\phone_control_ap\connect-home-wifi.ps1 -Port COM8
```

3. Open the printed URL on a phone connected to the same home network. Expect the Speaking Clock page. To check the address again, rerun the script or send `phone-sta-status` in a serial terminal.
4. Restart the prototype and confirm that it waits for local provisioning again. Restore the clock firmware through manual BOOTSEL when finished.

On 2026-11-10, the user confirmed that the STA prototype joined the home Wi-Fi network and that the Speaking Clock page opened on the phone through the router. Credential lifetime after reboot and operation with the router disconnected from the Internet were not separately tested. NTP and Bluetooth coexistence are covered later in the integrated clock firmware.

## Integrated read-only clock status

Build the normal clock application with the experimental web profile:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-clock-bt.ps1 -Mp3 -PhoneWeb
```

The build ID ends in `-bt-mp3-wifi-phone`. The clock uses the existing saved home Wi-Fi credentials. The integrated build serves an embedded page and a read-only `/status.json` snapshot. The snapshot contains RTC time, countdown or selected duration, player state and track, and the output currently selected by the clock's audio-volume routing.

1. Boot on the existing home network and open **Settings → Phone Control (11)**. Open the displayed IP address over HTTP from a phone on the home Wi-Fi network. The USB `phone-web-status` command reports the mode and address.
2. Check that the browser follows changes made with the clock's controls. Start and stop the timer, then check the player in LOCAL and BT modes. Verify that sound plays normally and NTP still reports a successful sync.
3. In **Phone Control**, turn the timer encoder to **Start direct?** and press it. The LCD shows the **SpeakingClock** SSID and its WPA2 password for 50 seconds. The clock generates a password on the first direct start and reuses it after switching modes or restarting once the settings are saved. Join that network on the phone and open `http://192.168.4.1/`. Leaving the menu keeps direct Wi-Fi active. To see the password again, reopen **Phone Control** and turn the timer encoder one step counterclockwise.
4. Leave the direct network on the phone. On the clock, select **Return to home?** and press the timer encoder. The clock can reconnect while Phone Control remains open. Its status page shows **Connecting Ns**, **Getting IP Ns**, or **Retry in Ns**. If playback or another activity delays the next attempt, it shows **Wait until idle**. After connection, **Home Wi-Fi ready** appears above the IP address. Rejoin the home network from the phone and open that address. Close settings to allow the idle NTP service to apply a fresh time sample.
5. Repeat the switch, check the timer and Bluetooth audio in both modes, and confirm a reboot starts in home mode. To replace the direct password, select **New Wi-Fi key?** and confirm. This also forgets browsers paired in direct mode. Rejoin **SpeakingClock** with the new key.

On 2026-11-10, integrated build `1b824915e670-bt-mp3-wifi-phone` booted on the existing home network. USB reported successful NTP synchronization, an active A2DP stream and valid v11 settings slots. A PC fetched the page and two JSON snapshots with advancing RTC time. The user opened the page on the phone and confirmed live time, the selected timer duration, timer start, player activity and the current track while using the clock's controls.

On build `6eec0ace9b26-bt-mp3-wifi-phone`, the user confirmed switching to direct mode, connecting the phone and reconnecting through the router after returning to home mode. The updater also verified that a reboot returned to home mode and that the clock served its status page after NTP and Bluetooth recovered. The exact password screen duration and playback under sustained web traffic in direct mode were not separately measured.

## Timer control from the phone

1. Open the clock's numeric IP address over HTTP on the phone. Use the trusted home network or the clock's direct WPA2 network.
2. Open **Settings → Phone Control → Pair phone?** and press the timer encoder. Enter the six digits shown on the LCD into the browser within 60 seconds and select **Pair and remember**. Up to five attempts are allowed per code. If **Home trust: ON** is enabled and the browser is on the home subnet, it can skip this step.
3. Once the LCD reports **Phone paired**, press MINUS until the normal clock display returns. In the browser, select hours, minutes and seconds from 00:00:01 through 23:59:59 using three Android-style scrolling wheels. Scroll by touch or mouse, click a value, or focus a wheel and use arrow keys; Home/End select its limits. The highlighted middle row is selected. **Set** updates the idle timer duration, **Start** starts that duration, and **Cancel timer** stops a running countdown.
4. Check that a timer started in the browser can be cancelled on the clock and that one started on the clock can be cancelled from the browser. Repeat in both Wi-Fi modes. Start a short timer and disconnect the phone before it finishes. The clock should still play its usual completion sound.
5. In direct mode or with home trust disabled, pair a second browser. Reopen the first browser and verify that it still has access. Select **Sign out** in that browser, then verify that only the other browser retains control. **Forget browsers?** on the clock revokes all paired browsers.
6. In home mode, enable **Home trust: ON**. Close settings and open the page in an unpaired browser on the same subnet. Test timer control, then disable home trust and confirm that the unpaired browser requires a code again. Direct mode always requires browser pairing.
7. After closing settings and waiting for `[FLASH] autosave=OK` when the clock is idle, restart and verify that a paired browser still works and the direct Wi-Fi password is unchanged. Also verify that a browser which signed out stays signed out after the next successful autosave and restart.

The clock remembers up to four browser and network-mode combinations. This stored-authorization capacity is separate from the target of four simultaneous browser clients. Pairing is scoped to the clock address in that browser and to home or direct mode on the clock. Remembered access has no inactivity timeout. Browser private mode or clearing website data can remove the browser's copy. Phone settings use the existing A/B autosave, so a change takes effect immediately in RAM and survives a restart after a successful save. Keep a compatible settings backup when rolling back to older firmware. This branch's regular build reads v12 phone settings, v13 optional birth years and v14 expanded birthdays. Four-client behavior still needs hardware testing.

Pairing the same browser in home and direct modes uses separate entries. Trusted-home access needs no paired entry. When all four entries are occupied, new pairing is refused rather than evicting another browser; use Sign out from an existing paired browser or Forget browsers on the clock to free entries. Clearing browser data alone does not remove the corresponding key from the clock.

Home trust is off by default, resets when the configured home Wi-Fi credentials change, and applies only to clients on the clock's home IPv4 subnet. Any client on that subnet can control the clock while home trust is enabled. Signing out from a paired browser removes its key. To stop code-free control, switch home trust off on the clock. The firmware serves local HTTP. Keep it on a trusted network and do not expose it to the Internet.

The controller requires matching Host and Origin headers for commands. The command sequence and nonce prevent a repeat or a concurrent request from silently starting the timer twice. Host tests cover code expiry, retry limits, access scope, logout, origin checking, concurrent commands and v11 to v12 settings compatibility.

On 2026-11-10, the user confirmed that the earlier temporary pairing and browser timer control passed on build `ab46f94d9c4d-bt-mp3-wifi-phone` in both direct and home Wi-Fi modes. A separate check from a PC on the home network confirmed that the page and status endpoint responded and that a timer command without a valid session returned HTTP 403.

On 2026-11-11, following installation of build `10c22e15f5e9-bt-mp3-wifi-phone`, the user confirmed access through the home network without repeated pairing and reconnection in direct mode without re-entering the Wi-Fi password. Browser Sign out, independent access from a second device, disabling home trust and persistence after a power cycle remain separate hardware acceptance checks.

The connection status page stays open while a home join or retry is pending, supports the usual MINUS back action and resumes its normal timeout after connection. USB logs include each join attempt, result and elapsed time. Host tests cover joining while the phone menu is open, deferring NTP/RTC changes until idle, address acquisition and retry countdowns. On 2026-11-11, after installation of build `b37ec35760a0-bt-mp3-wifi-phone`, the user confirmed that the transition worked and the displayed progress made the waiting process clear. The exact transition duration was not measured.

## Player control from the phone

The browser uses the same music player as the physical controls. Access follows the existing remembered-browser or trusted-home rules.

- The central **Play/Pause** button shows **Pause** while playing and **Play** when paused or stopped. It sends an explicit `pause`, `resume` or `play` action according to live state. Play opens/starts the player when closed or resumes a paused track. There is no separate Pause button in the current layout.
- The underlying **Pause** command is idempotent: repeating it cannot resume playback. Resume remains a distinct command even though its visible button label is Play.
- **Next** and **Previous** use the existing playlist and history. They start the selected track, including when the previous track was paused.
- **Stop** stops and closes the player, allowing the timer to start again. Sending Stop when the player is already closed leaves other clock audio running.
- The live status shows the selected track, track count, playback mode and audio output. Physical button actions appear on the browser's next status refresh.

Timer and player commands share one bounded queue in the main loop. The HTTP callbacks only validate and queue commands. The global sequence, per-request nonce and clock epoch prevent a retried Next or Play request from executing twice and reject conflicting concurrent commands. The existing timer POST format and `timer_*` status fields remain available. New clients use the shared `command_id`, `command_nonce` and `command_result` fields for completion feedback.

Hardware acceptance checklist:

1. Close settings and any timer or alarm. Open the page on the phone and select Play. Confirm music, the current track and the actual LOCAL or BT output.
2. Pause and resume from the phone. Check that the physical TIMER button and browser controls can both operate the same playback state.
3. Select Next and Previous. Check the track shown on the LCD and in the browser. The clock's current playlist mode governs track order.
4. Select Stop and check that music ends and the player closes. Then start and cancel a short timer from the browser.
5. Repeat in direct and home Wi-Fi modes and on both LOCAL and Bluetooth outputs. Refresh or close the browser during playback and check that music continues.
6. Check that a scheduled alarm retains its existing priority. While a timer or alarm is active, player start and navigation commands must be refused. A player Stop request must not cancel timer or alarm audio.
7. Use the page from four devices during playback and check for audio interruptions or repeated track changes. Poll status and try overlapping control requests; a busy/conflict result must permit a safe retry. Complete the outstanding Sign out and remembered-access checks from the timer section.

Host tests cover player authentication, action validation, shared timer/player sequencing and idempotent retries. A JavaScript execution check covers the browser's player buttons, pause/resume label, track status, a lost response after Next was applied and timer control after Stop.

On 2026-11-11, after installation of build `44ca0814caac-bt-mp3-wifi-phone`, the user confirmed that the planned player controls worked correctly without observed problems. This accepts the functional workflow. The report did not separately identify every network/output combination or the extended alarm, two-device and sustained-playback scenarios above.

## Basic settings from the phone

The Settings panel exposes independent LOCAL and Bluetooth volume levels, language, Manual/music, Hourly, Timer and Alarm output preferences, and display-light mode with day/night brightness. Volume and brightness sliders apply automatically and show their numeric values. Checkboxes and lists also apply automatically; valid numeric track edits apply when editing finishes. Only multi-field alarm/birthday records retain Apply confirmation. Each command changes one field, so changing brightness does not submit an older copy of the audio settings.

Volume uses the existing output-specific setter and 0..30 range. Brightness sliders support every integer percentage from 0..100, including values previously saved through USB or the API. OFF/ON/AUTO modes and the 22:00 to 07:00 night period are retained. Both can be changed during music playback. Slider commands are spaced by at least 200 ms and share the existing command queue. Movements received while a command is awaiting confirmation replace the queued value for that field. The final value is retained until confirmed, including after a lost response or a conflicting command from another browser. Close physical settings or alarm editors before changing browser settings. Language and audio-route changes additionally require the player, timer, alarm and other audio to be idle. Bluetooth is a preferred output with the usual LOCAL fallback, and selecting it does not connect to a speaker.

Accepted values take effect in the main loop and use the existing settings autosave. The page distinguishes saved settings, pending changes and a save error awaiting retry. Keep the clock powered until Settings saved appears. Playback, open menus, active countdowns and the normal write cooldown can delay saving.

The authenticated `/settings` endpoint shares the timer/player command sequence and retry handling. Only these settings enter the status snapshot. Wi-Fi passwords and browser keys are excluded.

Hardware acceptance checklist:

1. Change LOCAL volume and verify the built-in speaker and left encoder. Change Bluetooth volume separately and confirm that the LOCAL value stays unchanged. Repeat with Bluetooth playback.
2. Change language while idle and request the next spoken time. Restore the preferred language.
3. While idle, set the four audio output preferences and check the corresponding settings on the clock. Check that selecting Bluetooth while disconnected retains LOCAL fallback.
4. Change display-light mode and both brightness levels. Confirm the immediate effect in ON mode and compare with the clock's menu. Full AUTO day/night transition coverage remains deferred.
5. Leave a language edit unapplied in the browser, turn the volume encoder on the clock, and check that volume updates without overwriting the language edit. Move the four sliders rapidly and release them, including at zero. Confirm the final values on the clock and check that changing one output's volume preserves the other output's level.
6. During playback, check that volume and brightness remain available and language/output controls are disabled. Open an editor on the clock and verify that browser settings are disabled until it closes.
7. Wait for Settings saved, restart, and verify the accepted values. Repeat basic settings control in direct and home Wi-Fi modes.

Host checks cover authorization, setting ranges, rejected actions on the wrong endpoint and shared command retries. A JavaScript execution check covers independent fields, unapplied edits, updates from physical controls, save feedback, editor/idle guards and continued timer/player control.

The `phone_sliders` browser-script regression test runs with Node.js when available in the host-test environment. It checks updates during an in-flight request, final-value delivery, multiple sliders, a lost response, a temporary status failure, command-ID conflicts and updates from the physical encoder.

After installation of build `499e78c94a0c-bt-mp3-wifi-phone`, the user reported that everything worked following the requested slider checks. This accepts the immediate-slider workflow broadly. The report does not individually establish every output/network combination, both brightness schedules or persistence after a later slider change. COM8 verified the exact build, valid v12 A/B slots and NTP synchronization. Alarm and birthday row checksums matched before/after installation. Read-only HTTP checks found four sliders and settings marked saved; no automated control action was sent to the device during this verification.

On 2026-11-11, after installation of build `aa20a9cc4962-bt-mp3-wifi-phone`, the user confirmed that the implemented settings functionality worked correctly. This accepts the functional workflow. The report did not separately enumerate every network/output combination, the power-cycle persistence check or the individual guard scenarios above. Full AUTO day/night transition coverage remains deferred.

## Alarm and birthday editors

The page edits five alarm slots and fifteen annual birthday slots. Select a slot, edit its fields and use Apply alarm or Apply birthday. Alarm fields include enabled state, local time, Daily/Mon-Fri/Sat-Sun/custom weekdays, Signal/Music and a folder 08 music track in the supported 1..255 range. The selected music file must exist on the audio card used by the chosen output. Birthday fields include enabled state, a date and local announcement time. Enter `10.12` when the year is unknown, or `10.12.1985` to store a known birth year. Both repeat annually by day and month, using the existing melody. A supplied year must have four digits, 1000..9999, and form a valid calendar date. February 29 with no year is valid and remains a leap-year event.

Each Apply submits one complete record. The clock validates it before queuing and applies it from the main loop only while the player, timer, alarm, audio and physical editors are idle. Editing an alarm clears only that slot's queued/snoozed occurrence and consumes its current-minute match, following the physical editor. Birthday edits use the existing apply function. Changing only the birth year preserves the handled year and does not re-arm that year's notification. Existing physical and USB date edits retain a known birth year. Remove the year in the browser to make it unknown again.

Records with known birth years among the first five slots use v13 unless slots 6-15 differ from their disabled defaults. Such additional dates use the v14 A/B format, occupying three Flash pages in the existing sectors. v1-v13 records remain readable and initialize the new slots disabled. Loading does not migrate or write Flash. Older firmware cannot read v14 records; use a compatible build when reverting. Birth years are included in stale-record detection but are independent of the last handled event year.

A lossless revision of the editable fields accompanies each request. If a physical edit or another browser changes the selected record first, the clock reports a conflict and retains its current record. Use Reload to discard the local draft and read the latest values. Unapplied drafts survive status polling. Apply or Reload before switching slots. Duplicate requests retain their result, including conflicts, without applying the record twice.

The authenticated `/alarm` and `/birthday` endpoints share the existing command sequence. Read-only status includes five alarm records and fifteen birthday records. Calendar validation, access checks and the matching Host/Origin requirement also apply to these endpoints. Scheduling uses the clock's Europe/Berlin time; browser time zone settings do not change the entered hours.

Detailed hardware acceptance checklist (the general workflow is user-accepted; the scenarios below are not all individually closed):

1. Choose an unused alarm slot, configure a future time, weekdays, enabled state and Signal. Apply, then compare the physical alarm editor. Check its scheduled operation.
2. Test Daily, Mon-Fri, Sat-Sun and custom days. At least one day is required. Select Music with a known existing folder 08 track and check that the track is retained.
3. Configure or disable a birthday in slots 6 and 15 and compare day/month and time with the clock's menu. Test a future date/time with the existing melody. Check both `DD.MM` and `DD.MM.YYYY`, add or remove the year, and verify it after Settings saved and a restart. Confirm the original five dates remain intact and restore any deliberately changed test record afterward.
4. Leave a draft open in the browser, edit that slot on the clock or another browser, then try Apply. Check that the newer clock record is preserved and Reload retrieves it.
5. Edit different slots in turn and verify that the other slots retain their values. Try an invalid calendar date and check that it is refused. February 29 is valid.
6. Check that record editing is disabled during playback, a countdown, a ringing alarm or a physical editor. Stop/close the activity and retry. Timer and player controls should still work.
7. Wait for Settings saved, restart and verify the accepted alarm/birthday records. Repeat basic editing in direct and home Wi-Fi modes.

Native checks cover malformed or out-of-range records, slot boundaries, weekday masks, 1..255 tracks, calendar validation, access rules and retry handling. A JavaScript execution check covers preset/custom days, conflicts, Reload, draft preservation, slot isolation, a lost reply after applying, calendar messages and the timer. The status buffer is 4,096 bytes for five alarms and fifteen birthdays. The web profile uses a 48 KiB lwIP heap and four POST contexts; the regular profile retains 8 KiB. Four-client load and response-size checks remain release gates.

On 2026-11-11, the user confirmed the general editors worked on build `3d87e9900de5-bt-mp3-wifi-phone` and requested a single birthday date field with an optional stored birth year. After build `d51dd7adf5b8-bt-mp3-wifi-phone`, the user reported that the result was generally satisfactory and requested sliders for volume/brightness. This broadly accepts revised date entry and mobile alignment; adding/removing a known year and retaining it through a v13 save/restart were not individually reported. Native tests cover known/unknown-year recurrence, leap-year validation, year-only edits and v12 to v13 storage migration. The browser check covers adding and clearing the optional year. A 390-pixel mobile preview was inspected for field alignment and horizontal overflow.

## Remaining web-release acceptance

Use existing configured records carefully and restore any deliberately changed test values. A broad user acceptance report closes the functional workflow, not every combination in this checklist.

| Area | Remaining focused check |
| --- | --- |
| Access | Four simultaneous browsers, distinct from four stored authorization slots; Sign out removes only that browser's paired key; Forget browsers revokes paired keys. Home trust still grants subnet access until switched off. |
| Trust and persistence | Disable home trust and require pairing again; save/restart retains intended paired access and direct key; signed-out keys stay revoked after save/restart. |
| Physical controls | Browser reflects encoder changes after slider release; an unsent draft survives polling; editing the same alarm/birthday elsewhere produces a conflict and Reload retrieves the current record. |
| Sliders and storage | Rapid moves and final zero on each output; independent LOCAL/BT levels; wait for Settings saved and verify restart retention. A non-active day/night level need not change the current PWM duty. |
| Optional birth year | Add, clear and restore a known year; save/restart preserves it; recurrence remains annual; malformed/leap-day dates are refused as appropriate. |
| Playback and priorities | Refresh/close the page or disconnect the phone during music; timer/alarm priority and guarded commands remain correct. |
| Network/output coverage | Repeat core controls in home/direct modes on LOCAL/BT; recover after a phone disconnect and mode switch. Router outage testing remains deferred. |

The remaining product stages include expanded status and commands for existing device functions, followed by interface presentation/navigation, themes/localization and release preparation. Follow the [current roadmap](../../doc/ROADMAP.md#current-expansion-status-2026-11-13); stable main has not been replaced and no stable web release has been published as part of these checks.

## November 13 interface revision

Timer digits now scale and fade continuously as they enter or leave the selected row. The player uses a wider combined Play/Pause button between Previous and Next, with Stop centered below. Paused playback uses Resume internally when Play is pressed.

The alarm music-track field has a Test button on its right. Test sends authenticated `/player` action `test_track` with a track number from 1 through 255 and retains the unsaved alarm draft. It uses the alarm output preference and LOCAL fallback, plays one selected folder 08 track, and closes at EOF. Stop in the player ends it early. A new Test can replace a previous audition or ordinary player playback; timer, alarm, mute and physical-editor guards still apply. Test does not save settings. Matching audio files must exist on the selected output card.

Focused acceptance: check smooth scrolling on the phone; Play/Pause/Resume; audition several tracks without Apply; confirm saved alarm records remain unchanged; verify natural EOF and Stop on LOCAL and Bluetooth; confirm an active timer or alarm blocks audition. On 2026-11-13, the user reported that the recent improvements worked correctly. This closes the general functional workflow, not each individual scenario in this checklist. An exact installed build ID was not supplied with that report.

### Test/Stop refinement

The alarm track button now changes from Test to Stop while a track audition is active, including when paused. Stop sends `/player` action `test_stop 0`; this only closes an audition, so a stale browser state cannot stop ordinary music playback. `/status.json` includes `player_test` to synchronize the label across browsers and return to Test after completion or physical Stop. Stopping does not validate the draft track field or save the alarm. Native access and production browser-script checks passed, and the full web-profile firmware rebuilt successfully during development. The user's November 13 report broadly accepts the recent changes; exact LOCAL/BT, paused-audition and competing-browser sequences remain individually unconfirmed.

### Current verification and source provenance

On 2026-11-13, all seventeen host checks passed after a clean local Debug rebuild. This documentation review inspected uncommitted changes based on `697c062`; it did not create or install a new firmware image. Previous build/profile measurements in this guide describe their recorded stage and must not be presented as an exact identity or current peak-memory measurement of the installed device. See [latest hardware evidence](../../doc/HARDWARE_TEST_EVIDENCE.md#november-13-expansion-user-report).

### Deferred next-alarm wording

The user reports that Speak next alarm announces the correct upcoming alarm date/time but begins with the existing fired-alarm phrase (reported Russian wording: "The alarm number 5 has triggered"). This is misleading for an information request. After functional development is complete, refresh the speech assets so next-alarm information omits the fired-event introduction or uses a dedicated upcoming-alarm introduction. Keep the actual alarm-trigger announcement distinct. No audio assets or speech sequence were changed for this report.

## Live alarm actions (substage 3; user acceptance pending)

An authorized browser shows Alarm actions alongside the existing live overview. Stop alarm and Snooze 3 min are enabled only while an alarm is ringing. Cancel snoozed repeats is enabled when at least one sleeping or audio-waiting snoozed repeat exists. The overview already identifies the ringing slot and remaining repeat time. The buttons remain usable independently of physical editors, speech availability, timer state or RTC availability. Connection loss disables them; older firmware without occurrence/revision fields hides them.

Stop ends the current occurrence. Snooze uses the existing three-minute monotonic deadline. Cancel removes all snoozed repeats known to the reviewed state without disabling the saved weekday schedules or stopping another ringing occurrence. Each request carries the displayed occurrence ID or snooze-set revision. Changed state is refused with a request to review the latest status. Duplicate network retries use the existing command ID/nonce and cannot snooze twice. Feedback waits for the matching published snapshot. Operations are RAM-only.

Nineteen registered checks and both firmware profiles pass. Host checks cover stale same-slot occurrences, snooze-set changes, waiting transitions, authorization/endpoint/value validation, cached retries and browser controls/feedback. The real C HTTP serializer fits the 4,096-byte buffer with five alarms, fifteen birthdays, escaped Bluetooth names and boundary IDs/counters. These checks do not establish physical audio behavior or four-client timing. Approved installation and read-only USB/HTTP/settings verification passed on November 13 as `1.0.0+25.g97fd951`. User acceptance remains pending.

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
