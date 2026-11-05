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

## DFPlayer Mini - first C UART/queue bring-up

**Status: source committed, local build and audible test pending.**
The first DFPlayer stage ports the frame format, 9600-baud UART,
active-low BUSY indication, command-spacing rule (120 ms), and simple
nonblocking fixed-size playback queue from `source/v3/audio.py`.
The same hardware pins are used: **UART0 TX GP16, RX GP17, BUSY GP18**.

There is **no playback on boot**, no automatic timer-complete sound,
and no changes to audio volume. The existing speaker wiring and SD card
remain in place. In Serial Monitor (select LF), send one command per line:

| Command | Result |
| --- | --- |
| `audio` | Read BUSY pin and queue state |
| `play 7 16` | Queue RU service-folder short UI click (07/016) |
| `play 7 12` | Queue short timer signal (07/012) |
| `pause` | Clear queue and request DFPlayer pause |

Folder/track references are from `source/v3/audio.py`; the actual files
must be present on the SD card and DFPlayer must be powered and connected.
Each playback command uses DFPlayer's `0x0F` folder/track format.
The driver does **not** yet parse DFPlayer UART acknowledgements or
query folder contents, and `audio` reflects the BUSY pin rather than
verified playback success.

**Hardware check:** after confirming a successful new build, flash
`build-ninja/speaking_clock_c.uf2`; start monitoring the correct COM
port (previously COM8 on the assembled clock), use line ending LF,
send `audio` then `play 7 16`. Listen for the short click. If silent,
send `audio` again and report the full serial output. Avoid repeated
playback commands until we confirm the first result.

Build from `firmware/pico2w_c`:

```powershell
git pull --ff-only
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-ninja --target speaking_clock_c
```

The previously functioning MicroPython firmware is still the recovery
baseline; this test does not erase or alter DFPlayer SD card contents.


## Volume encoder, mute and Russian timer speech

**Status: code committed, build and physical test pending.** The previous
DFPlayer test confirmed audible playback of SD tracks.

This milestone mirrors the default Russian MicroPython audio behavior:

- Volume encoder GP14/GP15 changes volume by one per registered detent
  within 0..30; startup default is 10 (not persisted yet).
- Volume button GP20 toggles sound. Sound OFF cancels playback through the
  audio queue and requests DFPlayer pause. Sound ON permits new playback.
- USB command `audio` prints BUSY/queue plus current volume and sound mode.
- USB command `vol 12` sets the DFPlayer volume to 12, and
  `sound` toggles sound for testing. Commands use LF, one per line.
- Starting a timer announces RU service phrase folder 07 track 007,
  followed by the hour/minute recording where applicable (only for an
  exact number of minutes; seconds are deliberately not spoken).
- Cancelling the timer announces folder 07 track 014.
- Finishing the timer queues folder 07 track 011 followed by 07/013.
- USB `start`/`cancel` and physical timer button use the same audio logic.
- The audio queue coalesces rapid volume updates and enforces a 120 ms
  gap between DFPlayer commands without blocking LCD/RTC polling.

This build still uses **fixed RU mappings**. Persisted volume, language
switching, the full UI, chimes, alarms, and Bluetooth remain separate work.
The SD files must match the existing `source/v3/audio.py` mapping. The
existing MicroPython `source/v3` remains untouched.

Suggested physical tests: rotate volume slowly and monitor
`[AUDIO] volume=N/30`; set volume at 10-15, send `play 7 16`, toggle
sound OFF and verify silence, then ON; use `t 5` and `start` to test
the finishing voice/signals, and rotate the timer encoder then press it
to test the spoken number of minutes. If the short announcement overlaps
with the finish of a five-second timer, test again with a longer countdown
before reporting a queuing fault.


### LCD volume overlay

Confirmed hardware test (2026-11-05): volume encoder events change the value, sound output works, and timer was cancelled deliberately. The previous firmware did not display volume on LCD.

New behavior (pending local compilation and physical verification): turning the volume encoder, pressing its button, or using `vol <0..30>` / `sound` shows `VOLUME ON` or `VOLUME OFF` and the current `N / 30` level for about 1.8 seconds on LCD1602. The display then returns to the previously active timer or clock view; countdown and RTC polling continue during the overlay. No DFPlayer protocol change.

## RU/DE voice time announcements - first integration

**Code committed; local build and real DFPlayer playback test still required.**

The bilingual mappings are in `audio/speech.[ch]`, using the original
`source/v3/audio.py` folders: RU 01 hours, 02 minutes, 07 phrases;
DE 11 hours, 12 minutes, 17 phrases. Neither the stable MicroPython
files nor the DFPlayer SD card are changed.

Serial Monitor (115200, LF, **one line per command**):

| Command | Action |
| --- | --- |
| `lang` | Show selected language |
| `lang ru` | Select Russian (RAM only, default at startup) |
| `lang de` | Select German (RAM only) |
| `say` | Announce current DS1302 hour and minute |
| `say 14 23` | Test a specific time without changing RTC |
| `say 18 0` | Test exact-hour handling |
| `exact on` / `exact off` | Enable/disable minute track 000 at :00; off by default |

**Physical control:** holding the MODE/ST-MO button (GP22) for
at least **900 ms** and releasing it announces the RTC time.
Short MODE presses remain diagnostics only in this milestone.
A temporary LCD overlay shows `SPEAK TIME ru` or `SPEAK TIME de`
and the announced HH:MM for about 2 seconds.

The selected language also controls the existing timer-service phrase
folder (07 or 17). Timer duration speech follows the established
MicroPython behavior: RU hours/minutes for whole-minute durations;
DE number of minutes + 17/017 when duration is under 1 hour.
Incomplete fractional-minute durations do not speak a number.
Other features (settings screen, saved language, automatic hourly
announcements, quiet hours, alarms, date speech) are **not** implemented.

Recommended test sequence after updating and flashing the new UF2:
`lang ru` then `say 14 23`, wait until playback finishes,
`lang de` then `say 14 23`, wait, then hold MODE for >900 ms.
Check that hour and minute files play in order and LCD returns to
the clock after the overlay. Test `exact on` and `say 18 0`
separately. No RTC writes are performed.



## Language settings menu - first LCD UI slice

**Status: committed, pending local compilation and physical testing.**
This stage intentionally does not read, erase or write RP2350 flash.
The language is stored in RAM and resets to Russian on reboot.

With timer NOT running:

1. Press **SETTINGS** (GP27) to open the language selection screen.
2. LCD1602 displays `LANGUAGE SETTING` and `RU PRESS=SAVE`
   or `DE PRESS=SAVE`.
3. Turn the **TIMER encoder** one detent to switch RU/DE. Subsequent
   detents toggle between the two languages without changing timer duration.
4. Press **TIMER encoder button** (GP19) to commit the choice **to RAM**.
   The menu closes and LCD returns to the clock.
5. Hold **MODE** (GP22) for 900+ ms to hear the current time in the
   chosen language; `say` over USB performs the same playback.
6. Reopen the menu and press **SETTINGS** again to discard any unconfirmed
   choice. After 20 seconds without input the menu also times out and
   discards the unconfirmed choice.

`menu` in USB Serial Monitor can open/cancel the menu for diagnostics,
while `lang` shows the current active language. Existing `lang ru` /
`lang de` commands remain available for testing. Starting a timer from
USB while a menu is open closes the menu first.

The running timer, volume encoder, mute button, audio queue and RTC are
not changed. **Settings cannot be opened during an active countdown.**
Other settings and nonvolatile storage will be implemented only after
the physical UI passes this test. Do not expect settings to survive
a power cycle yet.

Build from `firmware/pico2w_c`:

```powershell
git pull --ff-only
& "C:\Users\Flint123\.pico-sdk\cmake\v4.3.4\bin\cmake.exe" --build build-ninja --target speaking_clock_c
```

Flash the newly built `build-ninja/speaking_clock_c.uf2`, then test
SETTINGS -> one detent -> TIMER button -> long MODE press. Confirm
the language actually changes without changing the selected timer
duration. Test cancelling menu and the 20-second timeout separately.


## Persistent settings - cautious A/B flash pilot

**Status: committed, build and hardware read/write tests pending.**
This is the first nonvolatile-storage implementation. It uses Pico SDK
`pico_flash/flash_safe_execute` and `hardware_flash` under a one-core
assumption, with two sector-sized A/B slots at the end of the Pico 2 W
board-defined flash, a record magic/version, monotonic sequence and
IEEE CRC32. The program-image-end guard refuses writes if the firmware
overlaps the reserved area, leaving an additional 4 KiB margin.

**Critical legacy protection:** because the clock previously ran
MicroPython, the last flash sectors may contain old MicroPython
filesystem data. The C implementation **does not erase unfamiliar
nonblank sectors**. A valid owned sector must contain our valid CRC32
header and only FF bytes for the remainder of the entire sector.
If there is legacy content the command replies `OCCUPIED`, and no
erase or write is attempted. Do not erase the Pico's MicroPython
filesystem merely to make this test pass before independently verifying
its backup. There is no force-save command.

### Read-only stage (do this FIRST)

Build/flash the updated UF2, open Serial Monitor with LF and send:

```text
flash-info
```

Expected output includes the board flash size, computed sector offsets,
the linked firmware-end address, `guard=OK` or `guard=FAIL`, and
`slot0`/`slot1` states (`BLANK`, `VALID` or `OCCUPIED`).
**Copy the full output before proceeding to save.**

At boot the firmware reads a valid latest record if present; otherwise
it uses defaults: RU, volume 10, exact-hour OFF. A read never erases
or changes flash. The selected language and volume can still be
changed using controls as before.

### Explicit save test - only after confirming slot safety

The firmware does **not** autosave on encoder detents or on language
selection. The Serial Monitor command

```text
save
```

asks the firmware to persist exactly three parameters: language,
volume, and exact-hour phrase. It returns `[FLASH] save=OK` on
successful write and readback verification. It refuses `OCCUPIED`
or `IMAGE_OVERLAP` and reports flash/verification errors.

The settings record alternates sectors: a new record is written
to the inactive slot while the previous valid record remains,
to better tolerate power interruption. This is not a filesystem
and does not preserve MicroPython files in the same sectors.
Flash endurance and arbitrary power loss DURING first save remain
hardware-dependent. Firmware updates using a full-chip erase
will remove saved settings, and settings retained through a normal
UF2 update are not yet proven.

After a confirmed `save=OK`, **power-cycle** the clock and read
the boot `[FLASH] loaded ...` message; verify language, volume
and `exact` against the saved values. Use `flash-info` after
reboot to confirm the active slot. No other settings are yet saved.

Implementation: `services/settings_store.[ch]`. The stable
MicroPython implementation and SD card files are unchanged.


## Read-only diagnostic for occupied settings sectors (2026-11-05)

On the assembled 4 MiB Pico 2 W, the user observed:

```text
[FLASH] size=4096 KiB slots=0x003fe000,0x003ff000 image_end=0x100185fc guard=OK
[FLASH] slot0=OCCUPIED sequence=0
[FLASH] slot1=OCCUPIED sequence=0
```

This does **not** establish whether the sectors hold MicroPython
LittleFS data or another older flash layout. Do not invoke `save`
until the original flash contents are independently backed up and
we have chosen a safe storage location.

A **new read-only** command, `flash-inspect`, prints for each of the two
sectors the flash offset, occupied/blank/owned status, byte counts
(0xFF, zero, other), locations of first and last non-0xFF bytes and
just the first 16 bytes in hexadecimal. It does not erase, program,
write a file, mount a filesystem, or modify settings. These byte
samples are diagnostic hints, not definitive filesystem identification.

Build from `firmware/pico2w_c`, flash the new UF2 and in Serial
Monitor (115200 baud, LF) send:

```text
flash-info
flash-inspect
```

Send each as a separate line. Capture both results.
If the slots remain occupied, simply leave persistent saving
disabled; all clock, timer and audio features still run using RAM
settings. A later migration can place settings in an independently
verified spare region or preserve a backed-up legacy filesystem.


## Flash tail diagnostic (read-only)

After an attempted erase, slot0 was BLANK but the last 256 bytes
of slot1 at flash offset `0x003FFF00` remained occupied.
The `flash-tail` serial command prints all 256 bytes in 16 rows
of hexadecimal and the IEEE CRC32 of those bytes:

```text
flash-tail
```

This command only reads from XIP flash. It performs no erase or write,
does not change the bootloader or settings, and does not send secret
data to external services. **However, the hex dump could contain
previously stored configuration or credentials**; inspect/redact such
data before sharing the output in public.

For comparison, repeat `flash-tail` after a simple power cycle
(without installing another firmware in between). An identical CRC32
suggests stable leftover data; a differing CRC32 indicates a change
that should be investigated. Neither result alone proves its origin.
Keep `save` unused until two storage slots are confirmed safe.


## RP2350-E10 discovery - why the last 256 bytes are EF (2026-11-05)

Physical test after erasing flash and reinstalling the C UF2:

```text
[FLASH] slot0=BLANK
[FLASH] slot1=OCCUPIED
[FLASH] 003fff00..003fffff = EF repeated 256 times
[FLASH] tail_crc32=825d9e84
```

This exactly matches the standard RP2350-E10 UF2 boot-ROM workaround:
a special RP2350 firmware image block can cause the last flash
page to contain 256 copies of `0xEF`. It is not evidence of a
MicroPython filesystem. See Raspberry Pi forum discussion
https://forums.raspberrypi.com/viewtopic.php?t=387986 and picotool
UF2 E10 block notes.

**Layout corrected:** Do NOT use the final sector for settings;
it is potentially modified by each UF2 firmware update.
The two provisional settings slots are now:

- slot0 = `0x003FC000` (4 KiB, from -16 KiB)
- slot1 = `0x003FD000` (4 KiB, from -12 KiB)

The final two sectors at `0x003FE000` and `0x003FF000`
are intentionally excluded from storage. These offsets are based
on the board's reported 4 MiB flash size, and remain protected
by image-overlap checking and refusal to erase any nonblank
unowned sector. This change only relocates the planned slots;
it does not execute any flash writes.

**Next hardware test:** after pulling and flashing the new build,
send `flash-info`, then `flash-inspect`. Confirm both relocated
slots are **BLANK** before doing anything with `save`.
If either is `OCCUPIED`, stop - do not erase or force-write.
If both are blank, storage can be tested in a later deliberate step.

Earlier sections of this README discussing use of the last two sectors
are historical and superseded by this corrected layout.
