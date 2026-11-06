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


## Automatic settings persistence (hardware test pending)

After the A/B flash test passed on the assembled Pico 2 W, autosave was
added for these three parameters only:

- Language RU/DE, after pressing the TIMER encoder button to confirm in the
  SETTINGS language menu; cancelling the menu does not change the value.
- DFPlayer volume 0..30 when adjusted by the volume encoder or \`vol N\`.
- Exact-hour phrase option when changed using \`exact on\` / \`exact off\`.

A change only marks RAM dirty. The main loop writes the latest complete
settings snapshot when all conditions are met:

1. At least **5 seconds** after the *last* actual settings change.
2. At least **60 seconds** after the previous successful flash save in
   this boot session, whether manual or automatic.
3. No actively running countdown, open language-selection menu, or queued
   DFPlayer playback.
4. The new settings differ from the latest successfully persisted snapshot.

If a write fails, it retries no more frequently than every 60 seconds
and logs the error. The existing flash protections, A/B slots and CRC32
checks remain unchanged. No write occurs just because the Pico boots.
The timer duration and sound mute state are NOT persisted in this stage.

Serial monitor diagnostics:

| Command | Action |
| --- | --- |
| \`autosave-status\` | Current values, pending flag, remaining debounce, cooldown, error retry time |
| \`flash-info\` | Sector ownership and record sequence |
| \`save\` | Force manual save (still available for diagnosis) |

**Hardware validation (do not use \`save\` in the first test):**

1. After a successful build, flash the UF2 and read \`flash-info\`.
   Verify existing valid slots (sequence 1 and 2 from previous test).
2. Change volume by **one** encoder detent; run \`autosave-status\`
   immediately and expect \`pending=yes\`.
3. Let the clock run without changes for more than 5 s. If a countdown
   or sound is playing, let it finish first. Expect a single
   \`[FLASH] autosave=OK ...\` line. Read \`flash-info\` again; one slot
   should now have the next sequence number (3).
4. Change the language with SETTINGS -> TIMER turn -> TIMER button.
   Within the 60 s cooldown the new choice remains active in RAM;
   \`autosave-status\` shows the remaining cooldown. After it expires,
   expect the next autosave to succeed.
5. Power-cycle without reflashing. Check the boot
   \`[FLASH] loaded lang=... volume=... exact=...\` line.

Do **not** change the volume dozens of times merely to test wear limits.
The timing guards reduce write frequency, but flash endurance remains finite.
If autosave fails with OCCUPIED or IMAGE_OVERLAP, stop and report the log.
Earlier sections referring to manual-only saving document previous milestones.


## DS1302 intermittent read diagnostics (hardware check pending)

The user observed sporadic \`[RTC] Invalid/unavailable\` messages, sometimes
after SETTINGS/TIMER button activity, but also without a button press.
The driver now captures raw register values **from the existing seven
read transactions** in the same order and with the same timings:
seconds, minutes, hours, day, month, weekday, year. It does **not**
perform extra reads, retry failures, write RTC registers, or modify
the clock's control pins.

Two new USB Serial commands (115200 baud, LF):

| Command | Description |
| --- | --- |
| \`rtc-debug\` | Show successful/failed counts, current and maximum consecutive failures, last raw read and last failed raw read with decoded error flags |
| \`rtc-reset-stats\` | Reset counters and stored sample ONLY; does not alter DS1302 chip contents |

Failure flags cover the oscillator CH bit, 12-hour mode bit,
seconds/minutes/hours/day/month/weekday range checks and malformed BCD
digits. The previous driver already rejected invalid numeric ranges;
the new code additionally flags invalid BCD digit encodings. No recovery
retry has yet been enabled so that the original failing packet can be
captured for diagnosis.

Test with the assembled clock:
1. Build and flash as usual. Run \`rtc-debug\` during stable operation.
2. Open/close SETTINGS, turn TIMER encoder, run a countdown.
3. If an \`Invalid/unavailable\` message occurs, run \`rtc-debug\`
   promptly, then send the full diagnostics output. The most useful
   lines are \`last_error\` and \`error_flags\`. The last-error snapshot
   is retained until reset or power cycle, even when good reads resume.
4. Optionally \`rtc-reset-stats\` to start a clean observation window.
5. Do not modify DS1302 wiring or timings yet; compare corrupted bytes
   with good ones before deciding whether to change the driver.

Do not confuse a rejected read with proven RTC power failure: invalid
BCD/range can be caused by one sampled bit or electrical interference.


## Three-item LCD settings menu (hardware verification pending)

This milestone expands the previous one-item language shortcut to a two-level
menu following the proven MicroPython settings navigation concept. It exposes
**Language**, **Volume** and **Exact-hour word**, reusing the existing settings
snapshot and protected autosave. No flash record format changes and no
changes to RTC/DFPlayer drivers.

With the countdown stopped:

1. Press **SETTINGS** (GP27) to open a list, initially \`SETTINGS 1/3\`
   and \`Language\`.
2. Turn the **TIMER encoder** (GP11/10) to choose among Language, Volume,
   Exact-hour word (list wraps).
3. Press **TIMER encoder button** (GP19) or **SETTINGS** to enter edit.
4. Turn the same TIMER encoder to change the candidate: RU/DE, 0..30
   (clamped), or OFF/ON.
5. Press TIMER button or SETTINGS to **commit** and return to the list.
   A changed value uses the existing automatic flash save after 5s idle
   and the 60s write cooldown.
6. Press **MINUS** (GP26) while editing to **discard** unconfirmed changes
   and return to the list. Press MINUS again from the list to exit.
   Unconfirmed changes also discard on the 20-second timeout.

Volume encoder (left) continues adjusting the real volume directly.
In the Volume editor, the timer encoder changes only the candidate
until confirmation, so cancelling does not change the DFPlayer volume.
Timer duration is not modified while the menu is open. Long MODE still
announces RTC time as before.

Suggested regression test: verify all three list entries, change language
RU/DE and hear with long MODE, change volume by one step and confirm with
\`audio\`, toggle Exact-hour word and verify \`autosave-status\`. Open
the editor a second time, change a candidate and press MINUS to confirm
that its original value remains unchanged. Run \`rtc-debug\` after the
test if any RTC validation error occurs.

**Compatibility note:** SETTINGS no longer immediately opens the language
editor. It opens the list first. In this stage only the three items are
available; time/date/quiet/chime/alarms are not yet implemented in C.


## Time and Date editors (C firmware, **not yet hardware-tested**)

The five-item menu now has: Language, **Time**, **Date**, Volume,
Exact-hour word. This follows the staged editor in \`source/v3/main.py\`.
The two new editors use the TIMER encoder to change one field at a time,
TIMER button or SETTINGS to advance to the next field, and MINUS to
cancel without writing to the DS1302:

- Time: **hour -> minute -> second -> final confirmation**.
- Date: **day -> month -> year -> final confirmation**. Day-of-week
  is recalculated automatically (Monday=1..Sunday=7); leap years and
  month lengths are validated.
- After the last field is confirmed, C reads the RTC again, merges only
  the edited Time/Date subset, validates the full resulting calendar,
  writes an 8-byte DS1302 clock burst, preserves the original RTC
  control-register write-protection bit, reads the clock back to verify,
  and returns to the settings list only if verification passed.
- If an RTC read is invalid, an edit cannot start and a save is
  refused. The menu also rejects invalid calendar dates. The 20-second
  timeout cancels an unconfirmed edit. The flash settings structure and
  A/B slots are completely unchanged.
- **Warning:** confirming seconds in Time or year in Date is a *real*
  RTC write even if the displayed values were not edited. Unlike the
  other three settings, Time/Date changes are not flash settings.

### Safe-first test sequence

1. Flash the build and open SETTINGS; verify five entries in both
   directions. Check that the old Language/Volume/Exact editors still
   work.
2. Open Time, rotate hours one click, then press MINUS. Read the clock:
   it must be unchanged. Repeat with Date, without advancing through
   the final confirmation.
3. Only when the navigation/cancel test passes and a deliberate clock
   adjustment is desired, enter Time. Change the desired field(s),
   advance through Hour -> Minute -> Second, then confirm once more to
   write. Observe \`[RTC] TIME saved ...\`, continued RTC ticking, and
   check \`rtc-debug\`.
4. Likewise, change Date only if necessary; test month end/leap-year
   boundaries, avoiding a real RTC date change until ready.
5. If \`[RTC] RTC write/readback failed\` occurs, do not repeatedly
   confirm: capture \`rtc-debug\` and inspect hardware before retrying.

No NTP, periodic RTC adjustment, automatic time setting, or Bluetooth
are enabled by this milestone.


## Quiet mode + Half-hour beep settings (hardware test pending)

The settings list has seven entries in MicroPython order, with Volume
appended for convenience:

1. Language
2. Time
3. Date
4. Quiet mode
5. Half-hour beep
6. Exact-hour word
7. Volume

Quiet mode is a three-step editor: Enabled -> Start hour -> End hour.
Its defaults match \`source/v3/config.py\`: **ON, 22:00 until 07:00**.
The timer encoder chooses ON/OFF or an hour in 0..23; TIMER button
advances to the next stage, and the final press commits the whole
group. MINUS cancels pending edits. Half-hour beep is a separate
single ON/OFF setting, **OFF** by default.

These settings are **configuration only**. No automatic voice or
half-hour playback has been enabled. The serial command
\`quiet-status\` displays the current settings and explicitly reports
that automatic playback is disabled.

### A/B flash migration safety

Flash record v2 is 24 bytes and includes quiet_enabled, quiet_start,
quiet_end and half_hour_enabled. The existing **v1 20-byte records**
remain valid for load/ownership/CRC. On loading v1 the new values
use the documented defaults (ON,22,07,OFF). The sequence number and
same A/B sector addresses are preserved. Only after the user changes
a setting (or requests \`save\`) is a v2 record written to the opposite
slot. No flash nuke or manual migration is required.

### Hardware tests

1. Update firmware. Immediately check \`flash-info\`. Expect the
   previous slots to remain \`VALID version=1\`, then run
   \`quiet-status\`: \`enabled=ON start=22 end=07 half_hour=OFF\`.
2. Open SETTINGS, rotate to Quiet mode, press TIMER to edit, rotate
   ON/OFF, advance through Start/End, then press MINUS to cancel
   **without writing**. Check \`quiet-status\`: should remain unchanged.
3. Reopen Quiet mode and change just Start from 22 to 23, advance,
   confirm. Close the menu with MINUS and allow 5 seconds idle.
   Expect \`[FLASH] autosave=OK\`. \`flash-info\` should show one
   \`version=2\` record with a newer sequence, the other still v1.
4. Half-hour beep: set ON, commit and close the list. Expect normal
   delayed save (or 60s cooldown). No sound should play automatically
   because scheduling has not yet been added.
5. Power cycle, check \`quiet-status\` and \`flash-info\` to confirm
   persisted new values. Existing Language/Volume/Exact must remain.

Do not change or erase flash sectors manually to test migration.
If any slot becomes OCCUPIED or no settings load, stop and send
the log before trying another write.


## Automatic hourly speech and half-hour beep - opt-in milestone

The C scheduler now follows \`source/v3/main.py::service_clock_auto\`
for the supported **voice** clock mode. At minute \`00\` it queues
the same RU/DE hour tracks as \`speech_enqueue_time\` (and the
Exact-hour minute \`000\` when selected). At minute \`30\`, when
the existing Half-hour beep setting is ON, it queues the confirmed
language-neutral **folder 07, track 015** double-beep. It never
changes or sets the DS1302 time.

**Safety gate:** automatic playback defaults **OFF at every boot**,
even when Half-hour beep is ON in persisted settings. It is enabled
for the current power session only with serial \`auto on\`, and
disabled with \`auto off\`. Neither command writes flash. This
first milestone intentionally does *not* implement the alternative
clock chime mode from MicroPython.

Serial diagnostics (send each command on a separate line):

- \`auto-status\` reports gate, quiet period, Half-hour beep, sound,
  and session counters \`played\` and \`skipped\`.
- \`auto-test 21 00\` simulates a 21:00 event without queuing sound,
  writing flash or touching RTC. The runtime auto gate is ignored
  for these dry runs, but sound/quiet/timer/menu/busy guards still
  apply.
- \`auto-test 21 30\` tests the half-hour event. Compare
  \`auto-test 22 00\` against Quiet ON, 22-07.
- \`auto on\` turns on real playback until power cycle or \`auto off\`.

A boundary is processed at most once per date/hour/minute. If
disabled, quiet, muted, the timer is running/recently finished,
the settings menu is open, or DFPlayer is busy, the event is skipped
rather than queued for later. Playback starts only in seconds
\`00..05\` of the target minute so an RTC recovery or late startup
does not play an old announcement. No audio is interrupted.
Time/date write and manual \`say\` continue to work independently.

### Hardware test plan

1. Flash, open serial and run \`quiet-status\` and \`auto-status\`.
   Expect \`auto_runtime=OFF\`. Previous flash v2 settings should load.
2. With Quiet OFF and Half-hour beep ON, run \`auto-test 18 00\`,
   \`auto-test 18 30\`, \`auto-test 18 15\`. Expected
   \`HOURLY_VOICE\`, \`HALF_HOUR_BEEP\`, \`NO_EVENT\`.
3. Set Quiet ON, 22-07, then test \`auto-test 23 00\` and
   \`auto-test 08 00\`: expected \`QUIET_HOURS\` and
   \`HOURLY_VOICE\`.
4. If needed, enable real scheduling with \`auto on\` and observe
   the next actual hour/half-hour. Unlike \`auto-test\`, this can
   play DFPlayer audio. Run \`auto off\` to stop future events.
   \`auto off\` does not stop audio already underway.
5. Verify \`rtc-debug\` and timer start/finish still work.

This implementation is not yet compiled or tested on the user's
physical Pico 2 W. Do not assume audio playback is proven until
serial and speaker observations are received.


## Timer finish / flash autosave ordering fix

The main loop now calls \`countdown_consume_finished\` and queues the
timer finish sound **before** \`autosave_service\`. Previously,
\`countdown_service\` cleared \`timer.running\` at zero and autosave
could write flash before the finish sound was queued.

Autosave now explicitly waits until all of the following are true:
no active timer, no open menu, the 3-second timer-finished display
interval has expired, the audio queue is idle, and the DFPlayer
BUSY pin is inactive. The existing 5-second debounce and 60-second
flash-write cooldown are unchanged. This does not change the
\`save\` command, flash record format, RTC registers or any audio tracks.

### Hardware regression test

1. Flash the new UF2. Confirm old settings with \`flash-info\`,
   \`quiet-status\`.
2. Set the timer to 60 seconds. While it runs, change volume by one
   encoder step and issue \`autosave-status\`. Expect \`pending=yes\`
   and no \`autosave=OK\` yet.
3. Let the timer finish naturally, keep serial logs open. Expect
   \`[TIMER] FINISHED\` **before** \`[FLASH] autosave=OK\`.
   The finish sound should not be interrupted by flash activity.
4. After finish audio and 3-second overlay, expect an eventual
   \`[FLASH] autosave=OK\` (possibly delayed by a write cooldown).
   Verify \`flash-info\` and retained volume after a power cycle.
5. \`quiet-status\` now includes \`auto_runtime=ON/OFF\`.


## German timer-finish MP3 fallback

Hardware checks on 2026-11-05 showed explicit \`play 17 11\` spoke
"Timer deaktiviert" and \`play 17 13\` spoke "Timer eingestellt"
instead of the expected finish phrase and long signal. The checked-in
\`sound/_sdcard/17/\` contains only \`007.mp3\`, \`008.mp3\`,
\`009.mp3\`, \`014.mp3\`, and \`015.mp3\`; there are no files
\`011.mp3\` or \`013.mp3\` in that German service folder.
The documented track labels are in
\`sound/_sdcard/описание папок.txt\`. The user's physical SD-card
contents have not yet been independently inventoried.

As a temporary nonverbal German fallback,
\`speech_enqueue_timer_finish\` now queues only \`07/013\`
(the long timer signal in the Russian service folder). Russian
timer-finish playback continues to queue \`07/011\` + \`07/013\`.
This avoids requesting missing German tracks without guessing their
new SD-card positions. Later, if the German finish voice is added,
it should be restored deliberately and tested.

Hardware regression: first run \`play 7 13\` and confirm a long
signal; then run a short German countdown to completion. Expect
\`[AUDIO-TRACK] play folder=07 track=013\` and **no**
\`folder=17 track=011/013\`. Check audibility and logs before
changing any MP3 files.


## Voice / Chime clock mode (ST/MO button)

The MicroPython v3.10.0 behavior is now available in C:

- **ST/MO short press** (less than 900 ms) toggles
  \`VOICE\` ↔ \`CHIME\`. An LCD overlay confirms the selection.
  The action is disabled during a running timer or an open settings menu.
- **ST/MO long press** (at least 900 ms) still speaks the
  current RTC time; no change to the selected mode.
- In \`VOICE\` mode, each hour queues the normal RU/DE time announcement.
- In \`CHIME\` mode, the hour queues one file in folder **18**,
  track \`hour % 12\` (00/12 → **012**), matching original
  \`source/v3/main.py\`. File \`18/000\` is not used for hourly
  strikes. Repository \`sound/_sdcard/18\` includes 000..012.
- The half-hour double-beep remains \`07/015\`, independent of
  mode, and keeps the existing Half-hour beep setting.
- Quiet mode, timer priority, audio-busy guard and once-per-minute
  suppression remain in effect. Automatic scheduling starts ON after boot;
  \`auto off\` and \`auto on\` remain runtime diagnostic overrides.

### Persistent settings compatibility

No flash sector relocation or version change: existing **v2
24-byte** record's reserved byte is used for mode (\`0=VOICE\`,
\`1=CHIME\`). Existing valid v2 records have \`reserved=FF\`,
which is read as \`VOICE\` and accepted by CRC validation; v1
records also load as \`VOICE\`. New saves continue as
\`version=2\`, with the same CRC and A/B safety. A short press
changing mode marks settings dirty and follows the existing
5-second autosave and 60-second cooldown. Toggling back to the
saved mode cancels the pending write. Automatic scheduling is ON after every boot, independently of
flash mode persistence. Use \`auto off\` for temporary diagnostics.

### Serial commands and dry-run tests

Each command must be sent on a separate line.

1. \`flash-info\`, \`mode-status\`, \`auto-status\` - after flashing,
   expect the old sectors to remain VALID and mode VOICE.
2. \`mode chime\` - select chime without playing a sound. After
   \`auto-test 19 00\`, expect \`HOURLY_CHIME\` and
   \`folder=18 track=007\`. \`auto-test 00 00\` and
   \`auto-test 12 00\` expect \`track=012\`.
3. \`auto-test 19 30\` still reports the half-hour beep if enabled.
   No \`auto-test\` command changes DS1302 or starts audio.
4. Short press ST/MO twice and observe CHIME→VOICE→CHIME,
   \`mode-status\`, and the LCD overlay. Hold for 900ms+ to
   speak the time, without changing the mode.
5. With chime selected and timer idle, verify sound manually by
   sending \`play 18 7\`. For real scheduling use \`auto on\`
   and observe a future hour; use \`auto off\` afterward.
6. Allow the autosave delay, power cycle and confirm \`mode-status\`
   retains CHIME and \`flash-info\` still reports v2 records.
   Restore \`mode voice\` when desired.

**Note:** the source modifications have not been compiled or tested
on physical Pico 2 W as of this commit. Do not treat audible chime,
persistence, or timing as verified until hardware logs are received.


## Automatic clock schedule enabled on startup

C behavior now matches the MicroPython v3 default: the automatic schedule
is **ON after each power-up**, even without serial commands. Voice or chime
at the top of the hour is determined by the flash-persisted clock mode;
the half-hour sound is controlled by the persisted half-hour option.

The boot-only safety rule consumes the first observed RTC minute if that
minute is :00 or :30, regardless of second, preventing a duplicate or
stale announcement after a restart exactly at the boundary. An RTC read
failure delays this initialization until the first valid read. If the
initial RTC minute is not a boundary, the scheduler arms for the next
one. No RTC writes or flash format changes are involved.

Existing protections remain: quiet hours, sound OFF, running/just-finished
timer, settings menu, DFPlayer BUSY, queue occupancy, 5-second boundary
window and once-per-calendar-minute suppression. A suppressed scheduled
event is not replayed late. The serial commands remain available:

- \`auto-status\`: shows \`enabled=ON\` after boot.
- \`auto off\`: suppress automatic events for the current session.
- \`auto on\`: re-enable automatic events without replaying the
  current :00/:30 minute.
- \`auto-test 19 00\`: dry-run only; never changes RTC or plays audio.

### Pico 2 W hardware checks

1. After flashing and power cycling, send \`auto-status\` and
   \`mode-status\`. Expect \`enabled=ON\` without entering \`auto on\`,
   and the previously saved VOICE/CHIME selection.
2. To avoid an unscheduled chime while testing, use \`auto off\`.
   Check \`auto-test 19 00\` and \`auto-test 19 30\`. These
   simulations deliberately ignore the runtime ON/OFF gate.
3. With no timer, menu or other audio active, send \`auto on\`
   well before the next real :00 or :30. Observe the exact track
   in \`[AUDIO-TRACK]\` and only one \`[AUTO]\` event.
4. Restart during an existing :00/:30 minute if convenient and
   verify \`[AUTO] boot: current ... boundary consumed (no replay)\`.
   Do not expect automatic playback at that already-active boundary.
5. Check a real event with a timer running through :00/:30:
   expect \`[AUTO] ... skipped=TIMER_PRIORITY\` and no late replay.

Note: These source changes need a fresh Pico SDK compile and hardware
verification. Previous tests of CHIME playback do not establish the
new startup behavior.


## Read-only AUTO scheduler diagnostics after manual RTC changes

For a hardware case where a 180s timer crossed RTC 20:30 and only the
timer completion sound played, but **no** \`[AUTO] skipped=TIMER_PRIORITY\`
line was emitted, the scheduler now provides:

- \`auto-debug\`: print enabled/boot seed/last processed calendar-minute
  key, current RTC calendar-minute key and whether they match, timer,
  menu, half-hour option and audio state. It does not change the RTC,
  settings, audio or scheduler. A live RTC read is performed.
- On re-entry into an *already processed* boundary minute after the RTC
  first showed a different minute, a one-time diagnostic:
  \`[AUTO-DEBUG] HH:MM suppressed=ALREADY_PROCESSED key=...\`.
  This is **not** printed for routine one-second polling after an
  automatically handled event.

The original deduplication logic, timer priority, quiet hours,
once-per-minute behavior and flash record format are unchanged.

### Hardware regression

1. With a running timer, send \`auto-debug\`; verify timer=RUNNING.
2. Use an RTC boundary that has **not** been reached earlier since
   boot (or restart Pico before test), and let timer cross :30;
   expect \`[AUTO] HH:30 skipped=TIMER_PRIORITY\`.
3. Let the timer complete and verify only \`07/013\` sounded.
4. If the automatic event is silent again with no skip line, send
   \`auto-debug\` and check \`same_as_last\`. If it is YES, the
   boundary was already consumed; use another boundary.
5. Remember: \`auto-test HH MM\` is a dry run and cannot test the
   one-event-per-calendar-minute deduplication path.

This diagnostics change has not yet been compiled or hardware-tested.

## Reserved expansion pin map - SPI1 microSD + LCD backlight PWM (planned, 2026-11-06)

This is a **documented future wiring change**, not the active GPIO mapping in `drivers/controls.c`. The user has Dupont connectors and approved moving two timer-encoder wires.

| Peripheral | Planned GPIO (physical Pico 2 W pin) |
| --- | --- |
| Timer encoder DT | **GP12 (pin 16)**; currently GP11 |
| Timer encoder CLK | **GP13 (pin 17)**; currently GP10 |
| SPI1 SD MISO / DO | **GP8 (pin 11)** |
| SPI1 SD CS | **GP9 (pin 12)** |
| SPI1 SD SCK / CLK | **GP10 (pin 14)** |
| SPI1 SD MOSI / DI | **GP11 (pin 15)** |
| LCD1602 backlight control | **GP6 PWM (pin 9)** via transistor driver |

Do not connect SPI1 signals to GP10/GP11 until the timer encoder has been relocated and `drivers/controls.c` plus any active firmware have matching pin assignments. Avoid simultaneously changing pins on the production clock before the new firmware has been built and checked. The new Pico-side SD card is independent of the DFPlayer Mini's SD card. MP3 decoder, SPI+FAT32 driver, PWM control, and backlight switching hardware are **not yet implemented or tested**. Power the SD breakout according to its particular design (3.3V-safe logic mandatory); inspect LCD backpack's A/K backlight wiring before selecting a keying transistor.

The detailed wiring checklist and hardware/firmware rollout sequence live in `doc/C_MIGRATION_PLAN.md`.


### Hardware clarification: SD 3.3 V and separate display illumination (2026-11-06)

The user confirmed that the **external SPI microSD module is a 3.3 V module**. Plan its VCC from Pico **3V3(OUT), physical pin 36**, and GND from common ground. All SPI signals GP8/GP9/GP10/GP11 must remain at **3.3 V logic**. The module's stated supply rating does not by itself confirm its exact breakout circuitry or current draw; verify markings/pinout and check power headroom before final connection.

The installed **LCD1602 has no built-in backlight**. Consequently, GP6 (physical pin 9) is reserved for PWM drive of a **separate external LED used to illuminate the display**, rather than the LCD A/K backlight terminals or PCF8574 backpack's backlight control. Use GP6 -> approximately 1 kOhm base resistor -> NPN transistor (e.g. BC547/2N2222), emitter -> GND, collector -> LED cathode, LED anode -> LED current-limiting resistor -> appropriate supply. The previously proposed example uses VBUS 5 V through 220 Ohm for an LED with roughly 3 V forward drop, approximately 9 mA; confirm actual LED ratings and resistor/transistor pinout. Keep common ground; do not drive the LED directly from GPIO. VBUS 5 V is present only with suitable USB-derived power, so check the final clock power wiring before relying on it.

Three planned operating modes remain **OFF, ON, AUTO** (day/night dimming), but PWM driver, scheduling, electrical connections and firmware implementation are **not yet completed**. This clarification does not change the agreed timer encoder relocation GP11->GP12 (DT) and GP10->GP13 (CLK), or planned microSD SPI1 pinout.


## MP3 decode-only experiment (optional)

The `mp3-test 07/001.mp3` command uses an external Helix decoder with custom
read-only SPI1/FAT32 seek/read callbacks. It decodes up to 16 blocks of
256 stereo PCM frames to RAM and prints sample rate, bitrate, frame count and
elapsed microseconds. **No audio goes to JBL from the file yet**. DFPlayer
and clock logic remain unchanged.

License checkpoint: the wrapper [Helix-MP3-Decoder](https://github.com/Lefucjusz/Helix-MP3-Decoder)
has a GPLv3 LICENSE; RealNetworks Helix core is under RPSL/RCSL notices.
Review licenses before sharing combined firmware. No vendor code is committed.

```powershell
git clone https://github.com/Lefucjusz/Helix-MP3-Decoder.git third_party/Helix-MP3-Decoder
git -C third_party/Helix-MP3-Decoder checkout e4f5adadb5126708144e55a862f59e318cbffb51
git pull --ff-only
powershell -ExecutionPolicy Bypass -File .\update-pico.ps1 -Build -Bluetooth -Mp3
```

Close COM8 serial monitor for update. Afterwards run
`flash-info`, `alarm-status`, `sd-test`, `mp3-test 07/001.mp3`, `bt-status`.
The normal `-Build -Bluetooth` remains unchanged (MP3 OFF).
The MP3 build and performance on actual RP2350 are **not verified yet**.
Keep the working UF2 for rollback.
