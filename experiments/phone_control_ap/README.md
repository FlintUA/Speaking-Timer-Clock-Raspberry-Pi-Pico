# Phone control Wi-Fi prototypes

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
3. Once the LCD reports **Phone paired**, press MINUS until the normal clock display returns. In the browser, enter minutes and seconds. **Set** updates the idle timer duration, **Start** starts that duration, and **Cancel timer** stops a running countdown.
4. Check that a timer started in the browser can be cancelled on the clock and that one started on the clock can be cancelled from the browser. Repeat in both Wi-Fi modes. Start a short timer and disconnect the phone before it finishes. The clock should still play its usual completion sound.
5. In direct mode or with home trust disabled, pair a second browser. Reopen the first browser and verify that it still has access. Select **Sign out** in that browser, then verify that only the other browser retains control. **Forget browsers?** on the clock revokes all paired browsers.
6. In home mode, enable **Home trust: ON**. Close settings and open the page in an unpaired browser on the same subnet. Test timer control, then disable home trust and confirm that the unpaired browser requires a code again. Direct mode always requires browser pairing.
7. After closing settings and waiting for `[FLASH] autosave=OK` when the clock is idle, restart and verify that a paired browser still works and the direct Wi-Fi password is unchanged. Also verify that a browser which signed out stays signed out after the next successful autosave and restart.

The clock remembers up to four browser and network-mode combinations. Pairing is scoped to the clock address in that browser and to home or direct mode on the clock. Remembered access has no inactivity timeout. Browser private mode or clearing website data can remove the browser's copy. Phone settings use the existing A/B autosave, so a change takes effect immediately in RAM and survives a restart after a successful save. Keep a compatible settings backup when rolling back to older firmware. This branch's regular build reads both v12 phone settings and v13 optional birth years.

Home trust is off by default, resets when the configured home Wi-Fi credentials change, and applies only to clients on the clock's home IPv4 subnet. Any client on that subnet can control the clock while home trust is enabled. Signing out from a paired browser removes its key. To stop code-free control, switch home trust off on the clock. The firmware serves local HTTP. Keep it on a trusted network and do not expose it to the Internet.

The controller requires matching Host and Origin headers for commands. The command sequence and nonce prevent a repeat or a concurrent request from silently starting the timer twice. Host tests cover code expiry, retry limits, access scope, logout, origin checking, concurrent commands and v11 to v12 settings compatibility.

On 2026-11-10, the user confirmed that the earlier temporary pairing and browser timer control passed on build `ab46f94d9c4d-bt-mp3-wifi-phone` in both direct and home Wi-Fi modes. A separate check from a PC on the home network confirmed that the page and status endpoint responded and that a timer command without a valid session returned HTTP 403.

On 2026-11-11, following installation of build `10c22e15f5e9-bt-mp3-wifi-phone`, the user confirmed access through the home network without repeated pairing and reconnection in direct mode without re-entering the Wi-Fi password. Browser Sign out, independent access from a second device, disabling home trust and persistence after a power cycle remain separate hardware acceptance checks.

The connection status page stays open while a home join or retry is pending, supports the usual MINUS back action and resumes its normal timeout after connection. USB logs include each join attempt, result and elapsed time. Host tests cover joining while the phone menu is open, deferring NTP/RTC changes until idle, address acquisition and retry countdowns. On 2026-11-11, after installation of build `b37ec35760a0-bt-mp3-wifi-phone`, the user confirmed that the transition worked and the displayed progress made the waiting process clear. The exact transition duration was not measured.

## Player control from the phone

The browser uses the same music player as the physical controls. Access follows the existing remembered-browser or trusted-home rules.

- **Play** opens and starts the player, or starts the selected track when stopped. If paused, the button becomes **Resume** and continues through the existing pause/resume path.
- **Pause** pauses playback. Repeating a pause request cannot resume it.
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
7. Use the page from two devices during playback and check for audio interruptions or repeated track changes. Complete the outstanding Sign out and remembered-access checks from the timer section.

Host tests cover player authentication, action validation, shared timer/player sequencing and idempotent retries. A JavaScript execution check covers the browser's player buttons, pause/resume label, track status, a lost response after Next was applied and timer control after Stop.

On 2026-11-11, after installation of build `44ca0814caac-bt-mp3-wifi-phone`, the user confirmed that the planned player controls worked correctly without observed problems. This accepts the functional workflow. The report did not separately identify every network/output combination or the extended alarm, two-device and sustained-playback scenarios above.

## Basic settings from the phone

The Settings panel exposes independent LOCAL and Bluetooth volume levels, language, Manual/music, Hourly, Timer and Alarm output preferences, and display-light mode with day/night brightness. Each Apply changes one field. Live snapshots update fields which have no unapplied local edit, so changing brightness does not submit an older copy of the audio settings.

Volume uses the existing output-specific setter and 0..30 range. Brightness uses the existing 0..100 percent validation, OFF/ON/AUTO modes and 22:00 to 07:00 night period. Both can be changed during music playback. Close physical settings or alarm editors before applying browser changes. Language and audio-route changes additionally require the player, timer, alarm and other audio to be idle. Bluetooth is a preferred output with the usual LOCAL fallback, and selecting it does not connect to a speaker.

Accepted values take effect in the main loop and use the existing settings autosave. The page distinguishes saved settings, pending changes and a save error awaiting retry. Keep the clock powered until Settings saved appears. Playback, open menus, active countdowns and the normal write cooldown can delay saving.

The authenticated `/settings` endpoint shares the timer/player command sequence and retry handling. Only these settings enter the status snapshot. Wi-Fi passwords and browser keys are excluded.

Hardware acceptance checklist:

1. Change LOCAL volume and verify the built-in speaker and left encoder. Change Bluetooth volume separately and confirm that the LOCAL value stays unchanged. Repeat with Bluetooth playback.
2. Change language while idle and request the next spoken time. Restore the preferred language.
3. While idle, set the four audio output preferences and check the corresponding settings on the clock. Check that selecting Bluetooth while disconnected retains LOCAL fallback.
4. Change display-light mode and both brightness levels. Confirm the immediate effect in ON mode and compare with the clock's menu. Full AUTO day/night transition coverage remains deferred.
5. Leave a brightness edit unapplied in the browser, turn the volume encoder on the clock, and check that volume updates without overwriting the brightness edit. Apply brightness and confirm that the other settings retain their current values.
6. During playback, check that volume and brightness remain available and language/output controls are disabled. Open an editor on the clock and verify that browser settings are disabled until it closes.
7. Wait for Settings saved, restart, and verify the accepted values. Repeat basic settings control in direct and home Wi-Fi modes.

Host checks cover authorization, setting ranges, rejected actions on the wrong endpoint and shared command retries. A JavaScript execution check covers independent fields, unapplied edits, updates from physical controls, save feedback, editor/idle guards and continued timer/player control.

On 2026-11-11, after installation of build `aa20a9cc4962-bt-mp3-wifi-phone`, the user confirmed that the implemented settings functionality worked correctly. This accepts the functional workflow. The report did not separately enumerate every network/output combination, the power-cycle persistence check or the individual guard scenarios above. Full AUTO day/night transition coverage remains deferred.

## Alarm and birthday editors

The page edits the five existing alarm slots and five annual birthday slots. Select a slot, edit its fields and use Apply alarm or Apply birthday. Alarm fields include enabled state, local time, Daily/Mon-Fri/Sat-Sun/custom weekdays, Signal/Music and a folder 08 music track in the supported 1..255 range. The selected music file must exist on the audio card used by the chosen output. Birthday fields include enabled state, a date and local announcement time. Enter `10.12` when the year is unknown, or `10.12.1985` to store a known birth year. Both repeat annually by day and month, using the existing melody. A supplied year must have four digits, 1000..9999, and form a valid calendar date. February 29 with no year is valid and remains a leap-year event.

Each Apply submits one complete record. The clock validates it before queuing and applies it from the main loop only while the player, timer, alarm, audio and physical editors are idle. Editing an alarm clears only that slot's queued/snoozed occurrence and consumes its current-minute match, following the physical editor. Birthday edits use the existing apply function. Changing only the birth year preserves the handled year and does not re-arm that year's notification. Existing physical and USB date edits retain a known birth year. Remove the year in the browser to make it unknown again.

Records with known birth years use the v13 A/B format, which still occupies two Flash pages in the existing sectors. v1-v12 records remain readable and load unknown birth years. Loading does not migrate or write Flash. Settings without known birth years retain the v11/v12 encoding. Older firmware cannot read v13 records; use a compatible build when reverting. Birth years are included in stale-record detection but are independent of the last handled event year.

A lossless revision of the editable fields accompanies each request. If a physical edit or another browser changes the selected record first, the clock reports a conflict and retains its current record. Use Reload to discard the local draft and read the latest values. Unapplied drafts survive status polling. Apply or Reload before switching slots. Duplicate requests retain their result, including conflicts, without applying the record twice.

The authenticated `/alarm` and `/birthday` endpoints share the existing command sequence. Read-only status includes the five records of each kind. Calendar validation, access checks and the matching Host/Origin requirement also apply to these endpoints. Scheduling uses the clock's Europe/Berlin time; browser time zone settings do not change the entered hours.

Hardware acceptance is pending:

1. Choose an unused alarm slot, configure a future time, weekdays, enabled state and Signal. Apply, then compare the physical alarm editor. Check its scheduled operation.
2. Test Daily, Mon-Fri, Sat-Sun and custom days. At least one day is required. Select Music with a known existing folder 08 track and check that the track is retained.
3. Configure or disable a birthday in an unused slot and compare its day/month and time with the clock's menu. Test a future date/time with the existing melody. Check both `DD.MM` and `DD.MM.YYYY`, add or remove the year, and verify it after Settings saved and a restart. Restore the intended record afterward.
4. Leave a draft open in the browser, edit that slot on the clock or another browser, then try Apply. Check that the newer clock record is preserved and Reload retrieves it.
5. Edit different slots in turn and verify that the other slots retain their values. Try an invalid calendar date and check that it is refused. February 29 is valid.
6. Check that record editing is disabled during playback, a countdown, a ringing alarm or a physical editor. Stop/close the activity and retry. Timer and player controls should still work.
7. Wait for Settings saved, restart and verify the accepted alarm/birthday records. Repeat basic editing in direct and home Wi-Fi modes.

Native checks cover malformed or out-of-range records, slot boundaries, weekday masks, 1..255 tracks, calendar validation, access rules and retry handling. A JavaScript execution check covers preset/custom days, conflicts, Reload, draft preservation, slot isolation, a lost reply after applying, calendar messages and the timer. A format check parses the maximum-length status response with five alarms and five birthdays and verifies its buffer limit. The web profile uses a 16 KiB lwIP heap to accommodate the larger responses and TCP copies; the regular profile retains 8 KiB.

On 2026-11-11, the user confirmed the general editors worked on build `3d87e9900de5-bt-mp3-wifi-phone` and requested a single birthday date field with an optional stored birth year. The revised date entry and mobile field alignment await hardware acceptance. Native tests cover known/unknown-year recurrence, leap-year validation, year-only edits and v12 to v13 storage migration. The browser check covers adding and clearing the optional year. A 390-pixel mobile preview was inspected for field alignment and horizontal overflow.
