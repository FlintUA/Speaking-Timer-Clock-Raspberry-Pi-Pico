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
   Completed as source only; not added to Pico CMake executable and
   not yet hardware tested. The clock runtime is unchanged.
2. Integrate read-only `alarm-status` / `alarm-test HH MM`
   diagnostics using scheduler state without real audio or flash.
   Start with all five alarms disabled. No unintended alarm on boot.
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

Do NOT add `alarm_engine.c` to `speaking_clock_c` until the integration
stage. The previous UF2 is unchanged by this source-only addition.
