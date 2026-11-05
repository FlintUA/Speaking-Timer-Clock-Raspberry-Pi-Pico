# Pico 2 W -> Google Nest Audio Bluetooth A2DP experiment

This experiment belongs to the main Speaking Timer-Clock project but is intentionally isolated from the production MicroPython firmware in `source/v3`.

## Verified baseline

Hardware test completed successfully on Raspberry Pi Pico 2 W:

- Pico SDK 2.3.1
- RP2350 ARM build
- BTstack Classic enabled
- A2DP Source connected to Google Nest Audio named `Nest`
- SSP pairing completed
- SBC stereo stream at 44.1 kHz negotiated
- AVRCP connected
- audio was heard from the Nest speaker
- USB CDC serial logging worked after enabling USB stdio

This is the known-good baseline for all further Bluetooth work.

## Current architecture

```text
Pico 2 W
  -> Bluetooth Classic
  -> A2DP Source
  -> SBC
  -> Google Nest Audio
```

Wi-Fi is not required for the A2DP audio path.

## Project rule

Do not modify `source/v3` while experimenting with A2DP.

The production clock remains the stable MicroPython implementation.
All Bluetooth experiments stay under:

```text
experiments/bluetooth_a2dp_nest/
```

until the architecture is mature enough for integration.

## Development stages

1. Reproduce the known-good official A2DP Source demo from this repository.
2. Automatically discover the speaker named `Nest`.
3. Automatically reconnect to the bonded Nest after restart.
4. Remove the MOD demo music.
5. Stream a generated short PCM test tone.
6. Stream WAV data.
7. Research practical MP3 decoding on RP2350.
8. Decide how Bluetooth audio should integrate with the MicroPython clock.

## Known-good upstream demo

The working baseline uses the official Raspberry Pi / BTstack source:

```text
<PICO_SDK_PATH>/lib/btstack/example/a2dp_source_demo.c
```

and the Raspberry Pi `pico-examples` CMake target:

```text
bluetooth/btstack_examples/a2dp_source_demo
```

USB serial must be enabled for this target:

```cmake
pico_enable_stdio_usb(a2dp_source_demo 1)
pico_enable_stdio_uart(a2dp_source_demo 0)
```

## Windows environment used in the successful test

The Raspberry Pi Pico VS Code extension installed the toolchain under:

```text
C:\Users\Flint123\.pico-sdk
```

The successful test used:

```text
SDK:      .pico-sdk\sdk\2.3.1
pioasm:   .pico-sdk\tools\2.3.1\pioasm\pioasm.exe
picotool: .pico-sdk\picotool\2.3.1\picotool\picotool.exe
ninja:    .pico-sdk\ninja\v1.13.2\ninja.exe
cmake:    .pico-sdk\cmake\v4.3.4\bin\cmake.exe
```

These paths are examples from the verified machine. The build script tries to locate the installed tools automatically.

## Build the official baseline

From the main repository root:

```powershell
powershell -ExecutionPolicy Bypass -File .\experiments\bluetooth_a2dp_nest\build_official_a2dp_source.ps1
```

The script locates the Pico SDK installation, locates or clones pico-examples, enables USB stdio for the A2DP target, configures Pico 2 W / RP2350 ARM, builds `a2dp_source_demo`, and prints the UF2 path.

## Test procedure

1. Put Nest Audio into Bluetooth pairing mode.
2. Flash the generated `a2dp_source_demo.uf2` to a spare Pico 2 W.
3. Open its USB COM port at 115200.
4. Send `a` in the BTstack console to scan and connect.
5. Confirm that `Nest` is discovered and A2DP/AVRCP connect.
6. Start a test stream if needed from the BTstack console.

The spare Pico 2 W is preferred for Bluetooth development so the working clock remains untouched.

## Next development target

The next custom build should remove dependence on interactive console commands and perform:

```text
boot
 -> initialize BTstack
 -> find bonded Nest / scan for "Nest"
 -> connect A2DP
 -> connect AVRCP
 -> stream a short generated PCM tone
```

No MOD music should be started in the custom test.

## 300 ms tone + persistent SBC silence (experimental)

The standalone `nest_autotest` build now patches the SDK's upstream
`a2dp_source_demo.c` **at CMake configure time** into a generated file inside
`nest_autotest/build/`. This avoids committing the upstream source, whose
license restricts commercial use. Review the upstream license before any
commercial distribution.

The generated A2DP source plays a generated 441 Hz tone for exactly 300 ms
of PCM samples after stream start, then encodes and sends zero-valued PCM
samples without suspending the A2DP stream. This keeps the SBC transport
running (at a continuous bandwidth and power cost).

The CMake patch is deliberately strict. If a Pico SDK update changes the
upstream code, configuration fails with a useful error rather than silently
falling back to an endless tone.

### Build in VS Code

1. `git pull` on `develop/v3`.
2. In VS Code, `CMake: Build Target` -> `nest_a2dp_autotest`.
3. Flash `nest_autotest/build/nest_a2dp_autotest.uf2` to the **spare** Pico 2 W.
4. Open Serial Monitor and inspect `[TONE]` diagnostics.

Expected log after A2DP stream starts:

```text
[TONE] Scheduled 300 ms at 44100 Hz (13230 samples per channel)
A2DP Source: Stream started ...
[TONE] 300 ms complete; sending SBC silence, A2DP stays active
```

The precise log order includes upstream A2DP events. The actual acoustic
duration can be delayed by Bluetooth buffering. Audio stream persistence
during prolonged silence is still unverified on Nest. Automatic connection
continues to use upstream discovery of the first matching audio-class device;
explicit filtering by the name Nest is a future task.

`usb_stdio_test` remains an independent target. The production MicroPython
clock in `source/v3` remains unchanged.
