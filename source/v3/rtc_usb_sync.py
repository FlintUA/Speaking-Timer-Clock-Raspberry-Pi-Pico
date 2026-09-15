# Speaking Timer-Clock v3 - trusted USB/Thonny RTC synchronization helper
# Run this file from Thonny after the Pico RTC has been synchronized from PC.

from machine import Pin, RTC
from ds1302 import DS1302
from rtc_calibration import load_state, process_reference, machine_rtc_datetime


def weekday_for_date(year, month, day):
    offsets = (0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4)
    y = year - (1 if month < 3 else 0)
    sunday_zero = (
        y + y // 4 - y // 100 + y // 400 + offsets[month - 1] + day
    ) % 7
    return 7 if sunday_zero == 0 else sunday_zero


def ds_now(ds):
    value = ds.date_time()
    return {
        "year": value[0],
        "month": value[1],
        "day": value[2],
        "hour": value[4],
        "minute": value[5],
        "second": value[6],
    }


def write_ds(ds, value):
    weekday = weekday_for_date(value["year"], value["month"], value["day"])
    ds.date_time([
        value["year"], value["month"], value["day"], weekday,
        value["hour"], value["minute"], value["second"],
    ])
    ds.start()


def main():
    reference = machine_rtc_datetime(RTC().datetime())
    if reference is None:
        print("RTC USB SYNC: invalid Pico RTC reference")
        print("Connect with Thonny and enable/sync Pico time first.")
        return

    ds = DS1302(Pin(2), Pin(5), Pin(4))
    before = ds_now(ds)
    state = load_state()
    state, event, sync_value = process_reference(state, before, reference)

    if sync_value is not None:
        write_ds(ds, sync_value)

    print("RTC USB SYNC")
    print(
        "Reference: %04d-%02d-%02d %02d:%02d:%02d" % (
            reference["year"], reference["month"], reference["day"],
            reference["hour"], reference["minute"], reference["second"],
        )
    )
    print(
        "DS1302 before: %04d-%02d-%02d %02d:%02d:%02d" % (
            before["year"], before["month"], before["day"],
            before["hour"], before["minute"], before["second"],
        )
    )
    print("Event:", event)
    print("Error sec:", state["last_error_sec"])
    print("Auto correction sec/day:", state["rate_sec_per_day"])
    print("Samples:", state["sample_count"])

    if event == "baseline":
        print("Calibration baseline saved. Repeat USB sync after at least 5 days.")
    elif event == "learned":
        print("Calibration updated from the new trusted PC time sample.")
    elif event == "dst":
        print("Seasonal +/-1 hour change detected. Drift rate was NOT relearned.")
    elif event == "large_change":
        print("Large manual time change detected. Drift rate was NOT relearned.")
    elif event == "too_soon":
        print("Reference accepted, but interval is too short for drift learning.")


main()
