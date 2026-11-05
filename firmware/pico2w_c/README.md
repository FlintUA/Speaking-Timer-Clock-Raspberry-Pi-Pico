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

## Countdown bring-up (serial-only, no extra wiring)

Native `app/countdown.c` ports the monotonic countdown semantics from
`source/v3/timer_engine.py`. The serial line parser is polled without blocking
the main loop. This firmware **does not yet play an alarm sound**.

Build in the already-configured `build-ninja` folder:

```powershell
git pull --ff-only
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-ninja --target speaking_clock_c
```

Flash `build-ninja/speaking_clock_c.uf2` only to the spare Pico 2 W.
Open Serial Monitor (USB CDC, previously COM7), and **send each command
with a newline / Enter**. The monitor's send-mode must append LF or CRLF.

| Command | Purpose |
| --- | --- |
| `help` or `h` | Show available commands |
| `t 10` | Set the duration to 10 seconds |
| `start` | Start or restart the countdown |
| `status` | Show active state and remaining seconds |
| `cancel` | Cancel without producing a finished event |

Expected after `t 5`, `start`: remaining seconds decrease, then exactly
one `[TIMER] FINISHED` message. The deadline uses a monotonic millisecond
counter, independent of DS1302. RTC-unavailable diagnostics can appear
in parallel if no RTC is attached.

**Status:** code committed but build and physical tests of the countdown
version are pending.

## GPIO buttons and rotary encoders - diagnostic build

The driver `drivers/controls.c` is wired to the **original v3.10.0
pin mapping**, with internal pull-ups. The firmware only logs input
events to USB for this milestone. It does **not yet** bind GPIO events to
clock menus, alarm editing or audio; the serial countdown commands are
still available.

| Device | Pico GPIO |
| --- | --- |
| Volume encoder DT / CLK | GP14 / GP15 |
| Timer encoder DT / CLK | GP11 / GP10 |
| Volume encoder push | GP20 |
| Timer encoder push | GP19 |
| Timer-mode button | GP28 |
| Alarm button | GP21 |
| Mode button | GP22 |
| Minus button | GP26 |
| Settings / plus button | GP27 |

When hardware is connected, expect logs such as:

```text
[INPUT] GPIO controls initialized (2 encoders, 7 buttons)
[INPUT] encoder TIMER CW
[INPUT] encoder TIMER CCW
[INPUT] button TIMER PRESS
[INPUT] button TIMER RELEASE
```

Button debounce is 25 ms; encoders are polled at approximately 1 ms
intervals, recognizing the same two quadrature transitions as the
original MicroPython driver. This is a preliminary strategy and
**must be checked for missed steps/bounce** on the actual encoders.
Rotating or pressing a physical control does not start/stop the clock
yet.

### Test without the original clock hardware

Compile:

```powershell
git pull --ff-only
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-ninja --target speaking_clock_c
```

Flash `build-ninja/speaking_clock_c.uf2` to the **spare Pico 2 W only**,
open COM7 or the newly enumerated COM port, 115200/USB CDC.

Existing serial commands provide a software-only validation:

- `t 30` + Enter, `start` + Enter
- `cancel` + Enter before 30 seconds elapse
- `status` + Enter should show `STOPPED`; no `FINISHED` should follow.

For GPIO tests, connect one ordinary normally-open pushbutton between
**GP19 and GND** on the spare board; do not apply an external voltage.
Press/release should produce `button TIMER PRESS` then
`button TIMER RELEASE`. A free-standing encoder can then be connected
to the documented pins and GND, with 3.3 V only if required for a
compatible passive encoder module. **Never attach 5 V logic directly
to Pico GPIO.** No need to dismantle the production clock.

Status: **committed, pending local build and hardware validation**.

## Timer encoder/button and LCD - integration milestone

**Status: committed, pending compile and live hardware validation.**
This is a partial UI implementation, not feature parity with MicroPython.

- Timer encoder GP11/GP10: clockwise increases duration by 60 seconds per reported event; counterclockwise decreases it, bounded to 1..14400 seconds.
- Timer pushbutton GP19: start the timer, or cancel when it is running.
- LCD while selecting: `TIMER SET` / `HH:MM:SS`; while running: `TIMER RUNNING` / remaining `HH:MM:SS`.
- On expiration: `TIMER FINISHED` for about 3 seconds, then return to clock view.
- On cancellation: return to RTC time/date screen immediately.
- UART/USB serial commands `t <seconds>`, `start`, `cancel` also update LCD state.
- RTC still polled once a second; other buttons remain diagnostic only.
- No sound, changes to RTC, flash settings, or alarms in this build.

Build from `firmware/pico2w_c` using the previously configured `build-ninja`:

```powershell
git pull --ff-only
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-ninja --target speaking_clock_c
```

Check that `build-ninja/speaking_clock_c.uf2` was regenerated before flashing.
Test right/left turns and button press on assembled clock; compare displayed
minute increments against actual encoder detents. If one detent causes more
than one minute change, the encoder step scaling still needs calibration.
