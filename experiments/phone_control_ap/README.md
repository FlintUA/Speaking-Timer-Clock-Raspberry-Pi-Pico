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

The clock remembers up to four browser and network-mode combinations. Pairing is scoped to the clock address in that browser and to home or direct mode on the clock. Remembered access has no inactivity timeout. Browser private mode or clearing website data can remove the browser's copy. Phone settings use the existing A/B autosave, so a change takes effect immediately in RAM and survives a restart after a successful save. If rolling back to firmware older than the v12 settings format, plan to retain a compatible backup of the settings. This branch's regular build also reads v12.

Home trust is off by default, resets when the configured home Wi-Fi credentials change, and applies only to clients on the clock's home IPv4 subnet. Any client on that subnet can control the clock while home trust is enabled. Signing out from a paired browser removes its key. To stop code-free control, switch home trust off on the clock. The firmware serves local HTTP. Keep it on a trusted network and do not expose it to the Internet.

The controller requires matching Host and Origin headers for commands. The command sequence and nonce prevent a repeat or a concurrent request from silently starting the timer twice. Host tests cover code expiry, retry limits, access scope, logout, origin checking, concurrent commands and v11 to v12 settings compatibility.

On 2026-11-10, the user confirmed that the earlier temporary pairing and browser timer control passed on build `ab46f94d9c4d-bt-mp3-wifi-phone` in both direct and home Wi-Fi modes. A separate check from a PC on the home network confirmed that the page and status endpoint responded and that a timer command without a valid session returned HTTP 403.

On 2026-11-11, following installation of build `10c22e15f5e9-bt-mp3-wifi-phone`, the user confirmed access through the home network without repeated pairing and reconnection in direct mode without re-entering the Wi-Fi password. Browser Sign out, independent access from a second device, disabling home trust and persistence after a power cycle remain separate hardware acceptance checks.

The connection status page stays open while a home join or retry is pending, supports the usual MINUS back action and resumes its normal timeout after connection. USB logs include each join attempt, result and elapsed time. Host tests cover joining while the phone menu is open, deferring NTP/RTC changes until idle, address acquisition and retry countdowns. Hardware acceptance of the status screen and transition duration is pending.
