# Automatic scalar settings and reliable mobile checkboxes

Status: installed with approval as `1.0.0+30.g31ba195` on November 13; startup/settings checks passed. User acceptance of the mobile fix remains unconfirmed, and LOCAL beep audibility remains unresolved.

## Where confirmation is needed

The Settings panel applies independent scalar preferences automatically: language, preferred audio outputs, display-light mode, timer message/sound/random/pre-warning options and specific timer music number. Checkbox/select changes commit immediately; a valid numeric track commits on change/finishing input. Volume and brightness keep their existing coalesced slider behavior. The UI serializes updates at least 200 ms apart, keeps the newest unsent value per field and confirms the matching device snapshot before clearing an edit. Changes use the existing authenticated queue and normal deferred Flash save, without a new API or schema.

There are no per-field Apply buttons in Settings. Apply alarm and Apply birthday remain because they validate and save an entire multi-field draft together, with slot revision/conflict checking. Reload remains the explicit way to discard/reload such drafts. Timer Start/Set/Cancel and player actions remain deliberate operations. Timer Start/Set waits for outstanding timer/language/output preference edits to be confirmed; cancelling/stopping an event is not held behind an unsent edit.

Idle/editor/output-support guards are unchanged. An edit refused because the clock became busy is not replayed silently when it later becomes idle; feedback explains refusal and the current value is restored, including a focused checkbox/select. Invalid numeric text remains editable with validation feedback and is not sent. Lost responses trigger fresh-status reconciliation and retain the bounded ID/nonce retry behavior. Changes arriving during confirmation queue the newest value. Wait for Settings saved before power cycling.

## Mobile layout

Checkboxes have a fixed nonshrinking 20 x 20 CSS-pixel box with a wrapping label at least 44 pixels tall. Settings use the full available row width. The old layout combined a nowrap label with an individual Apply button and allowed checkbox shrinking to zero under constrained width/larger fonts. A real Chrome fixture reproduced 0-pixel width at 360 pixels with Arial 20px and about 9px at 412 pixels. The corrected fixture retains 20px at 360/412/960, including larger text, and native clicks toggle and save both states. Desktop-default font metrics did not reproduce the zero-width case; the enlarged-text case demonstrates the layout defect rather than identifying the phone's exact font settings.

Real Chrome tests also verify no horizontal overflow, zero Settings Apply buttons, two record Apply buttons and restoration of a refused focused checkbox. These fixture tests do not replace the user's actual phone/firmware test.

## Quiet timer warning report

The user could enable the warning with the physical controls but reported a very quiet beep compared with timer-completion speech. Read-only device status during investigation showed Bluetooth OFF, timer output Bluetooth-preferred (therefore LOCAL fallback), warning enabled and idle audio. No audio/settings command was sent during investigation.

Local FFmpeg analysis of existing files measured beep 19/001.mp3 at mean -8.5 dBFS / peak -0.1 dBFS, versus completion phrase 07/011.mp3 at mean -14.9 / peak -3.0 dBFS. These are source-file levels, not acoustic loudness measurements. The beep asset is not digitally low-level, so its bytes and both saved volume levels were left unchanged.

The former fixed 500 ms LOCAL owner cutoff could truncate a DFPlayer that starts the 197 ms file late. The revised owner observes BUSY onset/end, releases naturally without a redundant pause when BUSY completes, and allows playback until 150 ms before the next absolute tick/zero edge. This permits up to 850 ms from an on-time edge, or 700 ms from a 150-ms-late start, while retaining the 120 ms UART guard before the next edge. A stuck/no-BUSY clone is still bounded. Bluetooth retains its existing 500 ms guard. Cancel/mute/replacement/zero priority and no late replay remain unchanged.

The actual-handler host harness verifies a simulated 600 ms LOCAL startup / 800 ms completion without truncation, five requests at one-second intervals with no BUSY, UART pacing, no stale pause, and the existing BT/zero behavior. New `[TIMER-WARN] LOCAL BUSY after=...ms` / `LOCAL complete after=...ms` logs expose real device timing. Actual audibility and the cause of the quiet report remain pending hardware testing; the change removes a plausible truncation cause, without claiming an acoustic gain improvement in advance.

## Acceptance

After separately approved installation, refresh the page and toggle the long warning checkbox on/off without Apply. Verify language/output/light/timer choices save automatically, and that alarm/birthday records still require their own confirmation. Test a 15-20 second timer on LOCAL and compare full beep audibility with the previous firmware; collect the new timing logs if it remains quiet. Verify cancellation, Sound OFF and normal zero completion, then Bluetooth when available. Wait for Settings saved before restart and check persistence. Flash format is unchanged from v17.

## Subsequent device evidence

On November 13 the user reported that pre-warning sounded like very quiet partial clicks after installation. A read-only COM8 capture during a user-started countdown measured five LOCAL BUSY onsets at 489/372/377/373/378 ms and completions at 674/546/552/548/553 ms after the play requests. All ended naturally before the cutoff; this run does not support software truncation as the cause. Separate ordinary playback of 19/001 and 19/002 also failed to produce normal-volume beeps according to the user. No volume or audio-card changes were made. A separate diagnostic 19/001 variant with 300 ms leading silence was prepared outside the repository; it has not been installed or tested. The cause remains unknown; the user deferred sound testing until the next day.
