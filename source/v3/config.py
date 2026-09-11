# Speaking Timer-Clock v3 - persistent configuration
# MicroPython

import json
import os

CONFIG_PATH = "/config.json"
CONFIG_TMP_PATH = "/config.tmp"

DEFAULT_ALARMS = (
    (False, 7, 0),
    (False, 8, 0),
    (False, 9, 0),
    (False, 10, 0),
    (False, 11, 0),
)

DEFAULTS = {
    "language": "ru",
    "volume": 10,
    "quiet_enabled": True,
    "quiet_start": 22,
    "quiet_end": 7,
    # Clock automatic mode is exclusive: "voice" OR "chime".
    "clock_mode": "voice",
    # User-controlled automatic announcement options. Both default OFF.
    "half_hour_enabled": False,
    "exact_hour_phrase_enabled": False,
    # Migration marker for v3.6.0. Older configs had half-hour enabled by
    # default but no UI switch, so the first v3.6.0 load starts both options
    # OFF as explicit user-facing defaults.
    "auto_announce_options_initialized": True,
    "rtc_correction_sec_per_day": 0,
    "alarms": [],
}


def _default_alarms():
    result = []
    for enabled, hour, minute in DEFAULT_ALARMS:
        result.append({
            "enabled": enabled,
            "hour": hour,
            "minute": minute,
            "sound": "signal",
            "track": 1,
        })
    return result


def _validated_alarms(value):
    result = []
    source = value if isinstance(value, list) else []
    defaults = _default_alarms()

    for index in range(5):
        default = defaults[index]
        item = source[index] if index < len(source) else default
        if not isinstance(item, dict):
            item = default
        try:
            hour = int(item.get("hour", default["hour"])) % 24
        except (TypeError, ValueError):
            hour = default["hour"]
        try:
            minute = int(item.get("minute", default["minute"])) % 60
        except (TypeError, ValueError):
            minute = default["minute"]
        sound = item.get("sound", "signal")
        if sound not in ("signal", "music"):
            sound = "signal"
        try:
            track = int(item.get("track", 1))
        except (TypeError, ValueError):
            track = 1
        track = max(1, min(45, track))
        result.append({
            "enabled": bool(item.get("enabled", default["enabled"])),
            "hour": hour,
            "minute": minute,
            "sound": sound,
            "track": track,
        })
    return result


def _validated(data):
    cfg = dict(DEFAULTS)
    cfg["alarms"] = _default_alarms()
    source_is_dict = isinstance(data, dict)
    legacy_auto_options = (
        source_is_dict and "auto_announce_options_initialized" not in data
    )
    if source_is_dict:
        cfg.update(data)

    # Backward compatibility with early v3 config files.
    if "clock_mode" not in data if source_is_dict else True:
        if source_is_dict and data.get("chimes_enabled"):
            cfg["clock_mode"] = "chime"
        else:
            cfg["clock_mode"] = "voice"

    # v3.6.0 exposes both options in Settings. Existing installations may
    # contain half_hour_enabled=True only because that was the historical
    # default, not an explicit user choice. Start both switches OFF once.
    if legacy_auto_options:
        cfg["half_hour_enabled"] = False
        cfg["exact_hour_phrase_enabled"] = False
    cfg["auto_announce_options_initialized"] = True

    if cfg["language"] not in ("ru", "de"):
        cfg["language"] = "ru"
    if cfg["clock_mode"] not in ("voice", "chime"):
        cfg["clock_mode"] = "voice"

    cfg["volume"] = max(0, min(30, int(cfg["volume"])))
    cfg["quiet_start"] = int(cfg["quiet_start"]) % 24
    cfg["quiet_end"] = int(cfg["quiet_end"]) % 24
    cfg["quiet_enabled"] = bool(cfg["quiet_enabled"])
    cfg["half_hour_enabled"] = bool(cfg["half_hour_enabled"])
    cfg["exact_hour_phrase_enabled"] = bool(cfg["exact_hour_phrase_enabled"])
    cfg["rtc_correction_sec_per_day"] = max(
        -30, min(30, int(cfg["rtc_correction_sec_per_day"]))
    )
    cfg["alarms"] = _validated_alarms(cfg.get("alarms"))
    return cfg


def load_config():
    try:
        with open(CONFIG_PATH, "r") as f:
            return _validated(json.load(f))
    except (OSError, ValueError):
        return _validated({})


def save_config(config):
    cfg = _validated(config)
    with open(CONFIG_TMP_PATH, "w") as f:
        json.dump(cfg, f)
    try:
        os.remove(CONFIG_PATH)
    except OSError:
        pass
    os.rename(CONFIG_TMP_PATH, CONFIG_PATH)
    return cfg
