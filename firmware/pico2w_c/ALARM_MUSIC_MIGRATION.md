# C migration follow-up - alarm and music modules

Baseline (hardware verified 2026-11-05):
`checkpoint/pico2w-c-core-tested-2026-11-05`
at `85989addb9accdba0f7b1e04f3cf7ed304107789`.
Work continues on `develop/v3` only. The checkpoint is not changed.

## Reference MicroPython behavior

From `source/v3/config.py`:
- Five daily alarms, initially OFF at 07:00, 08:00, 09:00, 10:00, 11:00.
- Each alarm stores enabled, hour (0..23), minute (0..59),
  sound=signal/music, music track 1..45.

From `source/v3/main.py`:
- Alarm button short press opens/closes the alarm list; long press
  (>=900 ms) enters/exits music player.
- Timer encoder navigates alarm list and edits enabled/hour/minute/
  sound/track, track preview, and saves on completion.
- At matching RTC minute, alarm starts once; if several alarms
  coincide, first due index is chosen and later ones can run after
  the active alarm finishes.
- Alarm preempts current music, suppresses ordinary clock announcement,
  and is stopped by user input; safety timeout 15 minutes.
- Sound sequence: on RU, numbered phrase 07/001..005; on DE
  numbered phrase is absent and skipped; then time speech;
  finally either folder 08 music track or long signal 07/013.
- Original alarm engine does not check quiet hours before alarm:
  alarms are independent of regular automatic announcements.
  Timer interaction must be preserved deliberately.

From `source/v3/music_player.py`:
- Folder 08; track count detected through DFPlayer when possible,
  otherwise fallback in main.py; normal/shuffle/repeat modes.
- Shuffle bag avoids duplicates until exhausted, then places
  the previous cycle's last seven tracks late in the next cycle.
- Pause replays the current track on resume; next/previous available.
- Playing automatically advances after the queue becomes idle.

## Implementation stages (safe order)

1. **Isolated portable alarm scheduler**: app/alarm_engine.h/.c,
   five defaults, validity and one-shot per minute, host test.
   Completed; module compiled into the Pico executable but does not
   run automatically and has no audio or flash access. Not hardware tested. The clock runtime is unchanged.
2. **Read-only diagnostics integrated**: `alarm-status` prints all five
   RAM-only disabled defaults. `alarm-test HH MM` simulates the existing
   defaults. `alarm-test HH MM N` (N=1..5) virtually enables alarm N
   at the supplied time, runs a disposable copy of the scheduler,
   verifies a running alarm blocks a second, and prints a fired mask.
   No actual alarm audio, RTC write, persistent change, or automatic
   alarm is possible from these diagnostics.
3. Add safe alarm audio state machine, manual cancel, and
   15-minute timeout, with automatic clock suppression.
   Prove one alarm end-to-end before enabling multiple slots.
4. Extend flash storage intentionally: current v2 record is full
   except for no spare alarm capacity. Design a new v3 record
   with backwards read compatibility for v1/v2, CRC, A/B,
   and no writes to occupied unknown sectors. Validate layout
   on Pico before any migration write.
5. Implement 5-alarm LCD/encoder/button editor with previews,
   and save only on explicit edit commit.
6. Implement separate music engine (normal/shuffle/repeat),
   ensure DFPlayer BUSY/queue behavior and audio priority are
   stable. Integrate after alarms or on an isolated test path.

## Host test command

Run from `firmware/pico2w_c` on an OS with a C11 compiler:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iapp app/alarm_engine.c tests/alarm_engine_host_test.c -o alarm_engine_test
./alarm_engine_test
```

`alarm_engine.c` is now linked into `speaking_clock_c` but is only
called by serial diagnostics. Hardware behavior is otherwise unchanged.
The updated UF2 needs to be built and verified on the Pico.

## Diagnostic hardware smoke test

After `git pull --ff-only` and rebuilding/flashing:

```text
alarm-status
alarm-test 07 00
alarm-test 07 00 1
alarm-test 19 45 5
flash-info
auto-status
```

Expected: five disabled defaults at 07:00..11:00; first dry-run
`first=0` (no alarm enabled); second `first=1` and
`while_ringing=BLOCKED after_stop=0 fired_mask=0x01`;
third `first=5`, mask `0x10`. No DFPlayer audio and no flash
writes. The current RTC date is read for simulation but is never set.

Compilation and hardware verification have not been performed by this
commit; results must be checked on the physical device.


## Stage 3A - first real alarm (slot 1, opt-in RAM only)

This stage is deliberately restricted to **alarm 1**. Other four alarm
entries remain disabled and are available only to diagnostic dry runs.
Nothing is saved to flash; resetting Pico disables all five alarms.
No RTC register is written by the alarm commands.

### Commands

- \`alarm-status\` - show RAM configuration of all five and active alarm.
- \`alarm-set HH MM signal\` - arm slot 1 at local DS1302 HH:MM,
  with the final audio signal \`07/013\`.
- \`alarm-set HH MM music TRACK\` - arm slot 1 with final track
  \`08/TRACK\`, where TRACK is 1..45.
- \`alarm-off\` - disarm slot 1; if already ringing, use \`alarm-stop\`.
- \`alarm-stop\` - immediately stop current alarm audio.
- Press **ALARM** button while ringing to stop the alarm.
- Existing \`alarm-test\` remains a read-only simulation.

An alarm schedules once per observed calendar minute and only within
seconds 00..05, with suppression of stale starts. A matching alarm is
consumed even if the timer is active or settings menu is open; no delayed
alarm is allowed to start if those conditions clear later that minute.
A successful alarm owns the clock minute, suppressing hourly chimes
and half-hour beeps.

Audio:
- RU: \`07/001\` (alarm 1 number), speech of the current HH:MM,
  then signal \`07/013\` or configured music in folder \`08\`.
- DE: no unverified German alarm-number track is played;
  German spoken time, then signal/music.
- The audio queue preempts previous playback; completion waits for
  both empty queue and inactive DFPlayer BUSY. ALARM button or
  \`alarm-stop\` cancels the sequence. 15-minute failsafe stops it.
- Running or just-finished timers take precedence over alarms.
- While ringing, ordinary button actions other than ALARM/volume
  are blocked; serial \`play\`, \`pause\`, \`say\` and timer start are
  guarded. No claim yet about full alarm/music interaction.

### Suggested physical test

1. Flash the new UF2 and confirm no spontaneous alarm on boot:
   \`alarm-status\` reports all OFF; \`flash-info\` remains valid v2.
2. With real DS1302 time, run \`alarm-set HH MM signal\` choosing the
   **next** minute, not the current minute. Example: at 20:42 use
   \`alarm-set 20 43 signal\`. Do not set an hour/minute in the past.
3. At HH:MM:00 look for:
   \`[ALARM] 1 RINGING HH:MM sound=SIGNAL track=1 (RAM only)\`.
   In RU expect \`[AUDIO-TRACK] ... folder=07 track=001\`, hour/minute
   voice tracks, then \`folder=07 track=013\`. In DE expect only German
   time voice and \`07/013\`.
4. Verify it does not repeat in the same minute. Let the sequence
   complete and check \`[ALARM] finished=AUDIO_COMPLETED\`.
5. For manual stop, set alarm again at a future minute, and press
   the physical ALARM button during audio. Look for
   \`[ALARM] 1 stopped=BUTTON\`; verify sound stops.
6. Test \`alarm-set HH MM music 1\` separately for final folder=08
   track=001. Use \`alarm-stop\` if the track is long.
7. \`alarm-off\`, then \`alarm-status\`. Power-cycle to confirm
   alarms default OFF and flash sectors remain unchanged.
8. Later regression: a timer running at the alarm minute should log
   \`[ALARM] 1 skipped=TIMER_PRIORITY\` and no delayed playback.

Notes: this is a **source change requiring local Pico SDK build and
hardware validation**. Five-alarm editor, persistent flash format,
music player and multi-alarm concurrency remain future stages.
