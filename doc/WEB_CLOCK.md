# Browser date/time and NTP requests

Status: implemented and software-tested; installation and user acceptance pending.

## Controls

The Date and time panel edits a complete local date and time, including seconds, for 2000-01-01 through 2099-12-31. Use clock time reloads the applied RTC value. Use phone time (Berlin) fills an unsaved draft from the browser's system clock converted explicitly to Europe/Berlin; it does not depend on the phone's own time-zone setting and does not write the RTC. Edited fields survive status polling. Set date and time deliberately applies both fields together. No automatic Apply is used for a clock adjustment.

The main loop requires idle timer/player/alarm/audio and closed physical editors. A complete manual entry can repair an invalid RTC read, subject to successful write/readback. Calendar validation rejects invalid dates, leap seconds, unsupported years and the nonexistent 02:00-02:59 hour at the spring Europe/Berlin transition. Repeated autumn-hour civil times are allowed: DS1302 stores local civil fields, without a UTC-offset/fold flag. Weekday is derived by firmware, not accepted from the browser.

Synchronize now (NTP) is a separate explicit request, available with a configured, ready HOME Wi-Fi connection. It is unavailable in direct mode and while a reference/application is already pending. Acceptance means the request was queued, not that synchronization succeeded. Live status distinguishes requesting, validated reference waiting for idle RTC application, successful synchronization, RTC-write retry and latest request failure. A failed DNS/send/timeout does not erase the last successful synchronization age or stop RTC operation. Automatic retry remains active. NTP uses network time, not the phone clock.

Manual setting does not disable NTP or alter Wi-Fi credentials. After successful manual write, outstanding/queued old reference data is invalidated and the previous synchronized indication is cleared. The next periodic request is scheduled one hour later; explicit synchronization or reconnection can request network time sooner and overwrite the manual value. A late DNS/UDP result from the cancelled request is not applied.

## Scheduling and persistence

Manual writes from physical date/time editors and the web panel share the RTC application function with the NTP path. NTP retains its existing one-second tolerance and existing DST adjustment planning. Manual edits always use write/readback and scheduler adjustment planning: backward changes suppress replay through the previous observed minute; forward changes consume the new first observed minute rather than immediately replaying its hour/alarm. The birthday observer also consumes the first observed manual-adjustment minute without new playback. Existing birthday handled-year persistence remains in effect. Future valid boundaries continue normally.

This does not cancel or move existing monotonic snoozes/deferred-event expiry, nor rewrite saved alarm or birthday definitions. Merely landing on a scheduled minute is not a request to audition an event. RTC corrections refresh the next poll/snapshot. No Flash schema, stored volume, audio asset or timer pre-warning change is introduced.

## Protocol

Authenticated `/clock` uses the same token/epoch/ID/nonce envelope as other commands. `set` carries a 32-bit packed civil-calendar second count, obtained by treating the entered civil fields as UTC solely for encoding (946684800 through 4102444799). It is not a UTC instant or a browser-local timestamp; firmware decodes its calendar fields and applies Europe/Berlin local-time validation. `sync 0` queues an NTP request. Commands are isolated from `/control`, `/timer`, `/settings` and schedule endpoints. Existing duplicate-command handling prevents repeated RTC writes or NTP requests after a lost response.

`clock_control:true` advertises support, and the existing settings snapshot acknowledgement also identifies the clock state after application. Results include `network_unavailable`, `rtc_write_failed` and `invalid_time`, in addition to existing admission/conflict results. `overview.ntp.failed` identifies latest NTP request failure separately from RTC write retry. Older firmware without the capability leaves controls unavailable. The status buffer remains 4 KiB; maximum-record/identifier sizing is checked before candidate review.

## Validation and acceptance

Native checks cover calendar bounds/leap days, Berlin spring gap/autumn repeat, request scopes/ranges/endpoint isolation and duplicate writes/requests. Network tests cover stale-anchor invalidation, delayed DNS suppression, no immediate reapplication, explicit request, timeout/DNS failure and later validated recovery. Production-script tests cover draft preservation, Berlin phone-time fill, explicit apply/refusal, invalid dates, idle/direct/legacy guards and NTP failure feedback. An extracted actual-handler fixture verifies RTC write failure preserves scheduler/reference state, backward/forward replay guards, recovery from invalid RTC and NTP HOME/direct admission. Both web/ordinary profiles compile.

Real Chrome fixtures at 360/412/960 CSS pixels with enlarged text and an America/New_York browser zone verify civil-time encoding, Berlin phone-time fill, preserved drafts, visible failed NTP requests and no horizontal overflow. These fixtures do not alter the user's RTC or establish hardware acceptance.

After separately approved installation, refresh the page. Start with Use clock time and compare the fields with the device; editing/reloading alone must not change RTC. Test Synchronize now on HOME and watch the actual applied status/age. Manual testing should use a small deliberate correction at a safe time without due alarms/birthdays, then restore using NTP. Check device/web agreement and physical-edit reflection, plus direct mode manual setting/NTP unavailability when convenient. A real backward/forward event-boundary test, RTC hardware failure, network outage and multiple-client contention remain focused acceptance scenarios. No time-changing or sound command is sent during installation verification.
