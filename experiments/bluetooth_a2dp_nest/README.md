# Pico 2 W -> Google Nest Audio Bluetooth A2DP experiment

This experiment deliberately does not modify the MicroPython clock firmware.

Goal:

1. Build the official Raspberry Pi / BTstack A2DP Source demo for Raspberry Pi Pico 2 W.
2. Put Google Nest Audio (Nest) into Bluetooth pairing mode.
3. Let Pico 2 W scan for a Bluetooth speaker and connect.
4. Start the demo sine-wave stream.
5. Capture the USB serial output so pairing / A2DP behavior can be reviewed before integration.

## Why the official demo first

Raspberry Pi pico-examples includes an a2dp_source_demo for Bluetooth-capable Pico boards.
It uses BTstack Classic + SBC encoder, scans for Bluetooth audio speakers, and handles SSP numeric confirmation.
No Wi-Fi connection is required for the A2DP audio path.

## Requirements on Windows

You need CMake, Ninja, an Arm GNU toolchain supported by Pico SDK, Git, pico-sdk, pico-examples and pico-extras.
The easiest supported setup is the Raspberry Pi Pico VS Code extension / Pico SDK toolchain.

If the SDK is already installed, set these environment variables in PowerShell:

~~~powershell
$env:PICO_SDK_PATH = "C:\\path\\to\\pico-sdk"
$env:PICO_EXAMPLES_PATH = "C:\\path\\to\\pico-examples"
$env:PICO_EXTRAS_PATH = "C:\\path\\to\\pico-extras"
~~~

Initialize submodules:

~~~powershell
git -C $env:PICO_SDK_PATH submodule update --init
git -C $env:PICO_EXAMPLES_PATH submodule update --init
git -C $env:PICO_EXTRAS_PATH submodule update --init
~~~

## Build

From the clock repository root:

~~~powershell
powershell -ExecutionPolicy Bypass -File .\experiments\bluetooth_a2dp_nest\build_official_a2dp_source.ps1
~~~

The script configures for PICO_BOARD=pico2_w, PICO_PLATFORM=rp2350 and BTSTACK_EXAMPLES_ALL=1.
It prints the exact a2dp_source_demo.uf2 path when successful.

## Test procedure

Important: this UF2 temporarily replaces MicroPython in flash. It does not alter the project files on the PC.

1. Put Nest Audio into Bluetooth pairing mode in Google Home.
2. Keep other nearby Bluetooth speakers out of pairing mode for the cleanest first test.
3. Hold BOOTSEL while connecting Pico 2 W by USB.
4. Copy a2dp_source_demo.uf2 to the RP2350 boot drive.
5. Open the Pico USB serial console.
6. Watch for discovery, SSP and A2DP messages.

Typical useful output includes:

~~~text
Start scanning...
Device found: xx:xx:xx:xx:xx:xx ... name 'Nest'
Bluetooth speaker detected, trying to connect...
SSP User Confirmation Request ...
~~~

The upstream demo automatically accepts SSP numeric confirmation.
The interactive upstream demo also provides command x to start its generated sine stream.
If stdin is not available in the selected build, the first run still confirms discovery/pairing/A2DP and we will then add a tiny auto-start wrapper.

## What to send back

Send the complete USB serial log starting from Start scanning....
Especially useful are Device found, name, COD, SSP/PIN, A2DP connection, codec configuration and stream-state lines.

## Rollback

The production clock remains source/v3/main.py and is not modified by this experiment.
To restore the clock: flash the official Pico 2 W MicroPython UF2, copy the current runtime files back to the Pico and keep the local wifi_secrets.py.
