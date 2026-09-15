# Speaking Timer-Clock v3 - self-learning DS1302 calibration
# MicroPython

import json
import os

STATE_PATH = "/rtc_calibration.json"
STATE_TMP_PATH = "/rtc_calibration.tmp"

MIN_SAMPLE_DAYS = 5.0
MAX_LEARN_ERROR_SEC = 600
DST_MIN_ERROR_SEC = 3300
DST_MAX_ERROR_SEC = 3900
MAX_RATE_SEC_PER_DAY = 30.0
HISTORY_LIMIT = 5


def _default_state():
    return {
        "enabled": False,
        "rate_sec_per_day": 0.0,
        "fraction": 0.0,
        "sample_count": 0,
        "baseline_ref_sec": 0,
        "baseline_rate": 0.0,
        "last_apply_ordinal": 0,
        "last_error_sec": 0,
        "last_event": "none",
        "history": [],
    }


def _clamp(value, minimum, maximum):
    return max(minimum, min(maximum, value))


def _validated_state(data):
    state = _default_state()
    if isinstance(data, dict):
        state.update(data)

    state["enabled"] = bool(state.get("enabled", False))
    try:
        state["rate_sec_per_day"] = _clamp(
            float(state.get("rate_sec_per_day", 0.0)),
            -MAX_RATE_SEC_PER_DAY,
            MAX_RATE_SEC_PER_DAY,
        )
    except (TypeError, ValueError):
        state["rate_sec_per_day"] = 0.0
    try:
        state["fraction"] = float(state.get("fraction", 0.0))
    except (TypeError, ValueError):
        state["fraction"] = 0.0
    if state["fraction"] <= -1.0 or state["fraction"] >= 1.0:
        state["fraction"] = 0.0

    for key in (
        "sample_count", "baseline_ref_sec", "last_apply_ordinal",
        "last_error_sec",
    ):
        try:
            state[key] = int(state.get(key, 0))
        except (TypeError, ValueError):
            state[key] = 0

    try:
        state["baseline_rate"] = _clamp(
            float(state.get("baseline_rate", 0.0)),
            -MAX_RATE_SEC_PER_DAY,
            MAX_RATE_SEC_PER_DAY,
        )
    except (TypeError, ValueError):
        state["baseline_rate"] = 0.0

    history = state.get("history", [])
    if not isinstance(history, list):
        history = []
    cleaned = []
    for item in history[-HISTORY_LIMIT:]:
        if not isinstance(item, dict):
            continue
        try:
            days = float(item.get("days", 0.0))
            error = int(item.get("error", 0))
            rate = float(item.get("rate", 0.0))
        except (TypeError, ValueError):
            continue
        if days <= 0:
            continue
        cleaned.append({
            "days": round(days, 4),
            "error": error,
            "rate": round(_clamp(rate, -MAX_RATE_SEC_PER_DAY, MAX_RATE_SEC_PER_DAY), 4),
        })
    state["history"] = cleaned
    state["last_event"] = str(state.get("last_event", "none"))[:24]
    return state


def load_state():
    try:
        with open(STATE_PATH, "r") as f:
            return _validated_state(json.load(f))
    except (OSError, ValueError):
        return _default_state()


def save_state(state):
    state = _validated_state(state)
    with open(STATE_TMP_PATH, "w") as f:
        json.dump(state, f)
    try:
        os.remove(STATE_PATH)
    except OSError:
        pass
    os.rename(STATE_TMP_PATH, STATE_PATH)
    return state


def reset_state():
    try:
        os.remove(STATE_PATH)
    except OSError:
        pass
    try:
        os.remove(STATE_TMP_PATH)
    except OSError:
        pass
    return _default_state()


def is_leap(year):
    return (year % 4 == 0 and year % 100 != 0) or (year % 400 == 0)


def days_in_month(year, month):
    if month == 2:
        return 29 if is_leap(year) else 28
    if month in (4, 6, 9, 11):
        return 30
    return 31


def date_ordinal(year, month, day):
    """Return a timezone-free local calendar day number."""
    total = 0
    for y in range(2000, int(year)):
        total += 366 if is_leap(y) else 365
    for m in range(1, int(month)):
        total += days_in_month(int(year), m)
    return total + int(day) - 1


def datetime_seconds(value):
    """Convert a local calendar dictionary to seconds since 2000-01-01."""
    return (
        date_ordinal(value["year"], value["month"], value["day"]) * 86400
        + int(value["hour"]) * 3600
        + int(value["minute"]) * 60
        + int(value["second"])
    )


def seconds_to_datetime(total_seconds):
    total_seconds = int(total_seconds)
    if total_seconds < 0:
        total_seconds = 0
    ordinal, sod = divmod(total_seconds, 86400)

    year = 2000
    while True:
        size = 366 if is_leap(year) else 365
        if ordinal < size:
            break
        ordinal -= size
        year += 1

    month = 1
    while True:
        size = days_in_month(year, month)
        if ordinal < size:
            break
        ordinal -= size
        month += 1

    day = ordinal + 1
    hour, sod = divmod(sod, 3600)
    minute, second = divmod(sod, 60)
    return {
        "year": year,
        "month": month,
        "day": day,
        "hour": hour,
        "minute": minute,
        "second": second,
    }


def shift_datetime(value, seconds):
    return seconds_to_datetime(datetime_seconds(value) + int(seconds))


def machine_rtc_datetime(rtc_tuple):
    """Convert machine.RTC().datetime() into the common dictionary form."""
    if not rtc_tuple or len(rtc_tuple) < 7:
        return None
    try:
        result = {
            "year": int(rtc_tuple[0]),
            "month": int(rtc_tuple[1]),
            "day": int(rtc_tuple[2]),
            "hour": int(rtc_tuple[4]),
            "minute": int(rtc_tuple[5]),
            "second": int(rtc_tuple[6]),
        }
    except (TypeError, ValueError, IndexError):
        return None
    if not valid_datetime(result):
        return None
    return result


def valid_datetime(value):
    if not isinstance(value, dict):
        return False
    try:
        year = int(value["year"])
        month = int(value["month"])
        day = int(value["day"])
        hour = int(value["hour"])
        minute = int(value["minute"])
        second = int(value["second"])
    except (KeyError, TypeError, ValueError):
        return False
    if year < 2023 or year > 2099 or month < 1 or month > 12:
        return False
    if day < 1 or day > days_in_month(year, month):
        return False
    return 0 <= hour <= 23 and 0 <= minute <= 59 and 0 <= second <= 59


def _dst_window(value):
    """Return True around the European spring/autumn clock-change windows."""
    month = int(value["month"])
    day = int(value["day"])
    return (
        (month == 3 and day >= 20)
        or (month == 4 and day <= 7)
        or (month == 10 and day >= 20)
        or (month == 11 and day <= 7)
    )


def _weighted_rate(history):
    if not history:
        return 0.0
    total = 0.0
    weight_total = 0.0
    for item in history:
        weight = min(30.0, max(1.0, float(item["days"])))
        total += float(item["rate"]) * weight
        weight_total += weight
    if weight_total <= 0:
        return 0.0
    return _clamp(total / weight_total, -MAX_RATE_SEC_PER_DAY, MAX_RATE_SEC_PER_DAY)


def _start_baseline(state, reference):
    state["enabled"] = True
    state["baseline_ref_sec"] = datetime_seconds(reference)
    state["baseline_rate"] = float(state["rate_sec_per_day"])
    state["last_apply_ordinal"] = date_ordinal(
        reference["year"], reference["month"], reference["day"]
    )
    state["fraction"] = 0.0


def process_reference(state, ds_now, reference):
    """Learn from a trusted PC/USB time reference.

    Positive error means the DS1302 is ahead. The returned event is one of:
    baseline, learned, dst, large_change, too_soon, invalid.
    The caller should set DS1302 to the returned reference for every event
    except invalid.
    """
    state = _validated_state(state)
    if not valid_datetime(ds_now) or not valid_datetime(reference):
        return state, "invalid", None

    ds_sec = datetime_seconds(ds_now)
    ref_sec = datetime_seconds(reference)
    error = ds_sec - ref_sec
    state["last_error_sec"] = int(error)

    if not state["baseline_ref_sec"]:
        _start_baseline(state, reference)
        state["last_event"] = "baseline"
        return save_state(state), "baseline", reference

    elapsed_sec = ref_sec - int(state["baseline_ref_sec"])
    days = elapsed_sec / 86400.0

    abs_error = abs(error)
    if (
        DST_MIN_ERROR_SEC <= abs_error <= DST_MAX_ERROR_SEC
        and _dst_window(reference)
    ):
        _start_baseline(state, reference)
        state["last_event"] = "dst"
        return save_state(state), "dst", reference

    if abs_error > MAX_LEARN_ERROR_SEC:
        _start_baseline(state, reference)
        state["last_event"] = "large_change"
        return save_state(state), "large_change", reference

    if days < MIN_SAMPLE_DAYS:
        _start_baseline(state, reference)
        state["last_event"] = "too_soon"
        return save_state(state), "too_soon", reference

    # During the interval, baseline_rate was already compensating the DS1302.
    # Residual error/day therefore refines that previous correction:
    # new correction = old correction - residual error/day.
    candidate = float(state["baseline_rate"]) - (float(error) / days)
    candidate = _clamp(candidate, -MAX_RATE_SEC_PER_DAY, MAX_RATE_SEC_PER_DAY)

    history = list(state.get("history", []))
    history.append({
        "days": round(days, 4),
        "error": int(error),
        "rate": round(candidate, 4),
    })
    history = history[-HISTORY_LIMIT:]
    state["history"] = history
    state["rate_sec_per_day"] = round(_weighted_rate(history), 4)
    state["sample_count"] = int(state.get("sample_count", 0)) + 1
    _start_baseline(state, reference)
    state["last_event"] = "learned"
    return save_state(state), "learned", reference


def correction_due(state, ds_now):
    """Return (state, whole_seconds, shifted_datetime, changed_state)."""
    state = _validated_state(state)
    if not state["enabled"] or not valid_datetime(ds_now):
        return state, 0, None, False

    current_ordinal = date_ordinal(ds_now["year"], ds_now["month"], ds_now["day"])
    last_ordinal = int(state.get("last_apply_ordinal", 0))

    if last_ordinal <= 0:
        state["last_apply_ordinal"] = current_ordinal
        return save_state(state), 0, None, True

    days = current_ordinal - last_ordinal
    if days <= 0:
        return state, 0, None, False

    total = float(state["rate_sec_per_day"]) * days + float(state["fraction"])
    whole = int(total)
    state["fraction"] = total - whole
    state["last_apply_ordinal"] = current_ordinal
    state = save_state(state)

    if whole == 0:
        return state, 0, None, True
    return state, whole, shift_datetime(ds_now, whole), True
