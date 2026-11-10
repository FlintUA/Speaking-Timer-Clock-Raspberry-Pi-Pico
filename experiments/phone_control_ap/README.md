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
