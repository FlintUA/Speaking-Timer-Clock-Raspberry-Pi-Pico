# Optional five-second timer warning

Status: installed with separate approval on November 13 as `1.0.0+29.g90b047a`. Startup, read-only USB/HTTP/settings checks passed and the warning defaulted OFF; physical acceptance is pending.

## Behavior and controls

A saved, independent **Beep during the last 5 seconds** option defaults OFF. When enabled, the running countdown requests one short single beep at 5, 4, 3, 2 and 1 seconds remaining. At zero, ordinary completion message/signal/music behavior starts. The warning does not alter duration, the monotonic deadline, alarm schedules, existing completion choices or separate output levels. Sound OFF suppresses warnings. Quiet hours continue to govern automatic time announcements, not this manually started timer.

The browser checkbox lives in Settings and applies automatically; see [scalar settings and mobile fixes](WEB_SCALAR_SETTINGS.md). Changes require a stopped timer, closed physical editors and idle audio, like the other timer preferences. The physical Timer sound editor now starts with TIMER WARN 5 SEC ON/OFF, followed by its existing message/sound/music fields; only final confirmation saves the whole draft. MINUS backs up/cancels and timeout discards unfinished edits. Menu item numbers are unchanged. USB `timer-warning` reports the setting; `timer-warning on|off` applies it when idle. HTTP uses the authenticated `/settings` action `timer_warning 0|1` and publishes the value under `settings`.

The option is independent of completion message and extra sound. For example, warning beeps can be enabled while the final completion is silent. It does not add five-second warning sounds to scheduled alarms or three-minute snoozes. It respects the timer's preferred LOCAL/BT output and existing output volume; there is no extra warning-volume control.

## Asset and timing

The repository's numeric-card file is `19/001.mp3`, the mapped single `beep_01`. It is 11,612 bytes; local ffprobe reports approximately 0.197333 seconds, 48 kHz mono. The user confirmed that the original beep is short and both cards are identical. The existing double beep/half-hour audio is unchanged. No audio assets were edited or copied by this change.

`timer_warning_poll` uses absolute remaining time, emits each edge once and admits it only within 150 ms of that edge. Late, muted, disabled, UART-busy or audio-busy edges are consumed rather than queued/replayed. Start speech and higher-priority owners are not interrupted. A 1-5 second timer may therefore omit warning ticks while its start phrase plays. Test with 15-20 seconds initially so start speech can finish before the warning window.

LOCAL warning playback uses the existing paced DFPlayer folder command directly, rather than the normal queue's 2.5-second no-BUSY fallback. Its owner releases naturally after an observed BUSY completion, or is paused 150 ms before the next tick/zero edge (up to 850 ms after an on-time request), plus existing main-loop/UART servicing latency. This allows DFPlayer startup and the full short file while preserving the UART gap. BUSY onset/end timing is logged. Bluetooth uses a one-segment timer plan with the same bounded warning owner and existing SBC/MP3 path. Expired cues and BT loss/file failure are cancelled without a late LOCAL replay. A stream that is already unavailable can use the existing immediate LOCAL fallback. Cancellation, mute, replacement by other audio and zero clear the owner; stale warning cleanup cannot pause new music. No sleeps, card copies or delayed cue queue are introduced.

This describes software scheduling. Actual speaker latency, audible cadence, decoder/startup behavior and radio/load effects require device testing. The countdown deadline never waits for cue completion; normal output transport latency still exists.

## Saved format

Only an enabled warning selects conditional Flash v17: 640 bytes, three pages in the existing A/B sectors. It extends the immutable v16 prefix, preserves all prior fields and validates the new boolean and CRC. Reads of v1-v16 are write-free and default warning OFF. Warning OFF retains the previous conditional v11-v16 encodings. Runtime tick state does not dirty settings or write Flash. A rollback after saving warning-enabled preferences requires a v17-aware reader to retain access to those records. BTstack storage and Flash sector assignments are unchanged.

## Verification and hardware acceptance

All 22 registered tests pass after clean rebuild. The warning scheduler test covers once-only 5..1 edges, late/muted/disabled skips, no catch-up, cancellation/restart, zero, 150/151 ms boundary and 64-bit uptime. Expanded storage tests cover legacy OFF, v17 round-trip, preservation of random alarm/credential preferences, warning-only extension, write failure, invalid fields with recomputed CRC, A/B fallback and disabling back to an older encoding. Parser and production JS tests cover ranges, endpoint/scope checks, automatic checkbox updates, busy guards and older-status compatibility. Both firmware profiles build.

A one-off host harness compiles actual warning ownership/service/cancellation, timer finish and BT fallback functions with mocked transports. It verifies five one-second LOCAL requests even when BUSY never asserts, UART pacing, skipped busy edges, no stale pause after another owner appends audio, mute/cancel/zero, a 500 ms BT bound and dropping lost BT cues without LOCAL replay. It uses production scheduler, speech planner and queue code; physical transports remain untested.

After separately approved installation, refresh the phone page, enable the checkbox while idle and wait for confirmation. Start a 15-20 second timer, check five single beeps followed by the normal completion and confirm its deadline is unchanged. Repeat through LOCAL and Bluetooth, cancel during the final seconds, and verify Sound OFF prevents beeps. Check an optional silent final completion, short-timer busy skips, and physical menu back/confirmation. Wait for Settings saved, power cycle and verify the checkbox persists. Do not claim hardware cadence/persistence from host tests.
