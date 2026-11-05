# Pico 2 W native C firmware (migration workspace)

Status: **first read-only hardware bring-up implementation**. Source and CMake target added; compilation and physical LCD/RTC operation still require local verification.

This directory is the future integrated Pico SDK firmware. It is deliberately
separate from the known-good Bluetooth proof of concept in
`experiments/bluetooth_a2dp_nest/nest_autotest/` and from the stable
MicroPython clock `source/v3/main.py` (v3.10.0).

## Decisions

- Single repository, one production baseline and one native C migration track.
- Pico SDK 2.3.1 and RP2350 / Pico 2 W are the **tested A2DP baseline**.
- Target: cooperative, nonblocking application state machine on one core first.
- Keep DFPlayer Mini as a local audio output while adding Bluetooth A2DP.
- Preserve the current Russian/German phrases, timer semantics, alarm defaults,
  persistent options, RTC calibration and user controls.
- No change to the production Pico until equivalent modules are validated.
- No copying the upstream BTstack demo into this public repository. Respect
  the upstream BTstack example's applicable licensing terms; the existing
  experimental CMake patch creates a modified local copy at build time.
- Wi-Fi and A2DP coexistence is **not yet verified** and needs a dedicated
  hardware test before claiming that NTP and Bluetooth can operate together.

## Planned modules (not all implemented)

```text
firmware/pico2w_c/
  app/         application state machine, scheduler, alarm/timer policy
  drivers/     LCD1602 I2C, DS1302, encoders/buttons, DFPlayer UART
  audio/       routing, DFPlayer queue, BTstack A2DP and audio buffering
  services/    NTP, Wi-Fi, configuration, RTC correction
  tests/       host-side behavioral tests where feasible
```

The first CMake target is `speaking_clock_c`, built from `main.c` and two
independent drivers. It does not yet implement the clock's timers, menus,
settings, DFPlayer or Bluetooth.

## First vertical slice

1. Reproduce the exact production pin map and verify device startup.
2. Make an LCD1602 + DS1302 clock display using C SDK drivers.
3. Port button/encoder events and the countdown engine, with tests.
4. Add DFPlayer transport and audio queue (fallback local output).
5. Integrate the already-tested A2DP tone command, then file-backed speech.
6. Restore persistent settings, alarms, RU/DE speech, Wi-Fi/NTP and automatic
   RTC correction. Verify functionality against MicroPython.
7. Long-running stability tests, reboot/disconnect tests and final cutover.

Current migration checklist: [doc/C_MIGRATION_PLAN.md](../../doc/C_MIGRATION_PLAN.md).

## Safe development workflow

Work on `develop/v3`, commit one small change at a time, `git pull --ff-only`
on the PC and build/test only with the spare Pico 2 W initially.
`source/v3` is the reference implementation, not a directory to clean up.
Do not move, delete or rename assets until the replacement firmware is proven.

## Build in VS Code (Windows, Pico SDK extension)

Open a terminal at the **repository root**, run `git pull --ff-only`, and then
open the `firmware/pico2w_c` folder using VS Code **File > Open Folder**.
Select **Raspberry Pi Pico 2 W** as the board (RP2350 ARM) when configuring.

The project uses `PICO_SDK_PATH`. If the extension has not set it, configure
CMake explicitly from a PowerShell terminal:

```powershell
cd C:\Users\Flint123\projects\Speaking-Timer-Clock-Raspberry-Pi-Pico\firmware\pico2w_c
$env:PICO_SDK_PATH = "C:\Users\Flint123\.pico-sdk\sdk\2.3.1"
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" -S . -B build -G Ninja -DPICO_BOARD=pico2_w -DPICO_PLATFORM=rp2350-arm-s
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build --target speaking_clock_c
```

If an existing CMake preset from the Pico extension is available, prefer
**CMake: Configure** then **CMake: Build Target > speaking_clock_c**.

Expected output: `firmware/pico2w_c/build/speaking_clock_c.uf2`.
**Do not flash it to the production clock.**

## Serial output and wiring

- USB CDC serial, 115200 baud in VS Code Serial Monitor (USB CDC baud rate
  is informational).
- LCD1602 via PCF8574 backpack: I2C0 SDA=GP0, SCL=GP1, scan common addresses
  `0x20..0x27` and `0x38..0x3F`.
- DS1302 (match existing Python constructor): CLK=GP2, DIO=GP5, CE=GP4.
- The code only **reads** DS1302 time. It does not adjust, start, stop or
  calibrate the oscillator. Keep RTC wiring at Pico-compatible logic levels.
- If a standalone Pico has no external clock/display attached, the serial
  output should report missing LCD and invalid RTC; that is expected.
- 3-second startup delay permits Serial Monitor to connect.
- When RTC responds, expected display is HH:MM:SS on line one and
  DD.MM.YYYY on line two. LCD lines are refreshed only when content changes.

## Physical test acceptance (pending)

- [ ] CMake target compiles and UF2 is generated.
- [ ] Bare Pico prints boot and missing-peripheral diagnostics.
- [ ] LCD is detected and two rows show stable readable text.
- [ ] DS1302 shows advancing seconds without inadvertent register writes.
- [ ] Cold reboot produces the same result.
- [ ] The production MicroPython firmware remains untouched.
