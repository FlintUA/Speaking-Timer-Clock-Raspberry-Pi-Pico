# Browser automatic announcements and quiet hours

Status: installed with separate approval as `1.0.0+32.g08b93c5` on November 14. Startup/settings/HTTP checks passed; the user subsequently reported the options present and working. This is general acceptance, not individual scheduled-boundary or restart evidence.

## Controls

Settings now includes Automatic announcements and Quiet hours groups. Choices apply immediately through the existing scalar queue, without per-field Apply buttons. Stop music/timers/audio and close physical editors before changing these options. The main loop rechecks idle state; refused edits are restored and are not silently replayed later.

- Automatic announcements (until restart): the existing runtime `auto on/off` switch. It gates hourly/half-hour announcements, leaves manual speech/timers/alarms available, does not write Flash and returns to ON at boot. This is separate from global Sound off.
- On the hour: Speak the time (VOICE) or Clock chime (CHIME). The physical MODE button and browser reflect the same saved value. Changing mode does not replay a missed time boundary or start a preview.
- Double beep on the half-hour: enables the existing half-hour cue, subject to automatic-enable, sound, quiet hours and normal priority rules.
- Include the exact-hour word in spoken time: exposes the existing saved exact-hour preference. It also affects manually requested spoken time where the current speech plan uses this preference. No speech recordings are changed.
- Enable quiet hours, Quiet from and Quiet until: the existing saved whole-hour schedule, with 00:00 through 23:00 choices. Start is inclusive and end exclusive. A start later than the end crosses midnight. Equal start/end means quiet all day and is explicitly labelled. Off disables the schedule without discarding either hour. Values can be prepared while quiet mode is off; each choice is applied independently.

The page displays the applied quiet range, including Off, overnight and all-day explanations. Hours use local RTC time with the existing Europe/Berlin NTP/DST behavior. Quiet hours suppress automatic announcements and postpone birthday melodies for their existing maximum 30-minute pending period. They do not suppress manual speech, music, timer completion or scheduled alarms. Display-light Auto remains independent, with its fixed 22:00-07:00 night interval.

Hourly speech retains its Hourly audio output preference and Bluetooth-to-LOCAL fallback. Chimes and half-hour beeps remain LOCAL as in the existing firmware. This stage does not alter audio files, gains, the timer pre-warning owner or the deferred short-beep investigation.

## Implementation and verification

The authenticated `/settings` scalar commands add `auto_enabled`, `clock_mode`, `half_hour`, `exact_hour`, `quiet_enabled` (0/1), and `quiet_start`, `quiet_end` (0-23). Status publishes those seven values in `settings` with the existing snapshot acknowledgement. The UI uses the serialized coalescing/reconciliation queue and waits for acknowledged state. Fields are unavailable when older firmware omits these values. Pending announcement edits also hold Timer Start/Set until reconciliation, as other idle-only settings do.

All persistent fields already exist in the settings record and use deferred autosave only when changed. No Flash schema/sector migration is introduced; runtime automatic-enable is intentionally excluded from persistence. Existing scheduler boundary consumption, priority and missed-event rules are unchanged. Changing a quiet hour takes effect immediately for any pending birthday; there is no multi-field transaction.

Host tests cover request bounds/scopes/endpoint isolation/retries, production-script application/refusal/idle guards/missing metadata, applied quiet summaries and unchanged values. A fixture extracted from the actual main-loop announcement branch verifies saved-change counts versus RAM-only auto-enable, invalid-value preservation and blocked idle state. The actual quiet predicate is checked at overnight/daytime endpoints and for equal-hour all-day behavior. Both firmware profiles compile; the full status response is checked against the unchanged 4 KiB HTTP buffer. Real Chrome fixtures at 360/412/960 CSS pixels with enlarged text verify immediate choices, the all-day explanation, 24 hour choices and no horizontal overflow. These checks do not prove acoustic or radio timing on hardware.

## Acceptance after separately approved installation

Refresh the page; compare displayed options with physical menus and USB `auto-status`. Change VOICE/CHIME, half-hour and exact-hour choices from the browser, then change them physically and observe the page. Verify quiet Off, an overnight range and equal-hour all-day text; restore the intended hours. Test runtime automatic OFF and distinguish it from global mute. A later restart should restore automatic ON and preserve the other six saved values after autosave.

Observe an actual hourly/half-hour boundary when convenient, without changing RTC for the test. Confirm quiet suppression and the unaffected manual/timer/alarm paths; birthday deferral remains a separate focused case. Validate both network modes and output combinations when available. The pre-existing short-beep audibility issue remains unresolved and is not accepted by installing this stage.
