# RTC Auto Calibration - v3.7.0

The clock uses the external DS1302 as its normal time source. Version 3.7.0 adds a self-learning correction model which can use the Pico internal RTC as a trusted reference after Thonny synchronizes it from the computer.

## Files

Copy these files to the Pico root filesystem:

- `main_v3.py` as `/main.py`
- `rtc_calibration.py` as `/rtc_calibration.py`
- `rtc_usb_sync.py` as `/rtc_usb_sync.py`

Existing `config.py`, `ui.py`, `audio.py`, `timer_engine.py` and other v3 files remain in place.

## First calibration

1. Connect the Pico to the computer and open Thonny.
2. Make sure the computer time is correct.
3. Thonny synchronizes the Pico internal `machine.RTC()` from the computer.
4. Run `/rtc_usb_sync.py` from Thonny.
5. The script copies the trusted time to DS1302 and creates `/rtc_calibration.json`.
6. The first event is `baseline`. No drift rate is learned yet.

Wait at least 5 days before the next learning sample. 10-14 days is preferable.

## Next calibration

Run `/rtc_usb_sync.py` again after several days.

The script compares DS1302 with the trusted Pico/PC time and calculates the residual drift. It stores the new correction rate in seconds/day and synchronizes DS1302 to the reference time.

Example:

- 14 days elapsed
- DS1302 is 120 seconds fast
- initial learned correction is approximately `-8.57 sec/day`

The main program then applies the learned correction automatically. Fractional seconds are accumulated between days, so rates such as `-8.57 sec/day` are preserved rather than rounded to an integer.

Up to five recent learning samples are stored and combined using a weighted average.

## Seasonal time changes

A difference close to `+/-3600 seconds` during the European DST transition windows is treated as a seasonal clock change, not oscillator drift.

For a DST event:

- DS1302 is synchronized to the computer time;
- the learned drift rate is preserved;
- the DST sample is not added to drift history;
- a new baseline starts from the new local time.

## Other protections

- Samples shorter than 5 days are accepted as a new exact-time baseline but are not used for learning.
- Time differences greater than 10 minutes are treated as a large/manual change and are not used for learning.
- Learned correction is limited to `+/-30 sec/day`.
- Manual date/time edits from the clock menu invalidate the learning baseline, preventing a manual change from contaminating drift calculations.
- The previous manual `rtc_correction_sec_per_day` setting remains available as a fallback until USB auto calibration is activated.

## Diagnostic output

`rtc_usb_sync.py` prints:

- PC/Pico reference time;
- DS1302 time before synchronization;
- detected event;
- error in seconds;
- current auto-correction rate;
- number of learned samples.

Typical events:

- `baseline` - first trusted synchronization;
- `learned` - a valid drift sample updated the rate;
- `dst` - seasonal +/-1 hour change detected;
- `large_change` - a large manual time change was detected;
- `too_soon` - interval was too short for drift learning.
