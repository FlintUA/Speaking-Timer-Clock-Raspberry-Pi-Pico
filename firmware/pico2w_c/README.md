# Pico 2 W native C firmware (migration workspace)

Status: **planning / not yet a bootable clock firmware**.

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

We will add compilable source and CMake targets incrementally; do not use
this directory as a firmware build target until there is an implemented
`CMakeLists.txt`.

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
