# Browser player modes and sleep

Status: implemented and software-tested on the development branch; installation and user acceptance pending.

## Controls and behavior

The Music player panel has two immediate-choice lists below the transport buttons. No Apply button is needed. Playback mode selects Normal, Shuffle or Repeat one track; the currently playing or paused track is retained. Explicit Next still advances in Repeat one track. Mode changes retain Previous history using the existing physical playlist logic. Selecting the same mode does not rebuild shuffle history.

Sleep timer selects Off, 15, 30, 60 or 90 minutes and shows remaining minutes/seconds. A duration selection starts a new monotonic deadline, including when the same duration is selected again after another choice. The selected preset and the live remaining time are separate: a 15-minute preset continues to show 15 while the countdown falls to 14. Off cancels the deadline without stopping music. Pause and global mute do not suspend sleep. Expiry silently stops/closes the player through its existing service handler. Stop, player close and reboot clear sleep; a new player session uses its existing Shuffle default. These are session controls, not saved preferences, and do not write Flash.

Controls are available only with an open ordinary player, including paused/muted playback. They are unavailable during an alarm-track audition, a countdown, ringing alarm or physical editor. The main loop rechecks these conditions after a queued request; a stale/refused choice is restored from status. Selecting an option never opens the player or starts an audition. Physical mode/sleep changes appear on polling, and physical sleep cycling follows the browser-selected preset.

## Protocol and compatibility

The existing authenticated `/player` endpoint adds `mode 0|1|2` (Normal/Shuffle/Repeat one) and `sleep 0|15|30|60|90` to its token/epoch/ID/nonce envelope. Invalid, unsupported, wrong-scope or wrong-endpoint values are rejected. Existing command acknowledgement/retry rules ensure a duplicate accepted sleep request does not restart the deadline. A deliberately new duration command starts a new deadline. No cyclic web action is used.

Status retains `player_mode` (`NORM`, `SHUF`, `REP1`) and adds `player_sleep_preset` (minutes; zero when closed/off) and `player_sleep_seconds` (rounded up, zero when closed/off/expired). HTTP callbacks only queue commands; application/playlist/sleep state remains owned by the main loop. The ordinary profile continues to compile without serving a web page. Flash v17 and audio assets are unchanged.

## Verification and hardware acceptance

Host checks cover direct mode selection/current-track retention, unchanged repeated selection, invalid modes, explicit sleep presets/deadlines, physical cycling after a web selection, expiry and invalid-value preservation. Access tests cover ranges, scopes, endpoint isolation and duplicate sleep requests. Production JavaScript tests cover immediate choices, acknowledgement/lost-response behavior, physical state reflection, remaining-time display, paused/muted availability, audition/editor/closed guards and stale request refusal. Both firmware profiles compile. Real Chrome fixtures at 360/412/960 CSS pixels with enlarged text verify mode/sleep changes, remaining-time display and no horizontal overflow. An actual-main-handler fixture verifies closed/audition/editor/timer/alarm guards, current-track retention and supported/invalid sleep values. The full status serializer fixture (maximum identifiers, 5 alarms and 15 birthdays) fits the unchanged 4 KiB HTTP buffer; final clean-version sizing is checked again before candidate review.

After separately approved installation, refresh the browser. Start ordinary music, switch modes, verify that the current track continues and check Next/Previous. Change the mode physically and observe the browser. Select a sleep preset, check the remaining time, pause/resume and select Off without stopping playback. Repeat over LOCAL and BT when available. Allow a 15-minute sleep to expire, including a paused case, and check silent close, no restart and cleared status. Verify Stop/reopen defaults and confirm that alarm audition does not expose these controls. Actual audio/timing, full sleep expiry and multiple-browser contention remain hardware checks.
