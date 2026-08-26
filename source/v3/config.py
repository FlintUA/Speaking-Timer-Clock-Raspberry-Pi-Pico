# Speaking Timer-Clock v3 - persistent configuration
# MicroPython

import json
import os

CONFIG_PATH = "/config.json"
CONFIG_TMP_PATH = "/config.tmp"

DEFAULTS = {
    "language": "ru",
    "volume": 10,
    "quiet_enabled": True,
    "quiet_start": 22,
    "quiet_end": 7,
    # Clock automatic mode is exclusive: "voice" OR "chime".
    "clock_mode": "voice",
    "half_hour_enabled": True,
    "rtc_correction_sec_per_day": 0,
}


def _validated(data):
    cfg = dict(DEFAULTS)
    if isinstance(data, dict):
        cfg.update(data)

    # Backward compatibility with early v3 config files.
    if "clock_mode" not in data if isinstance(data, dict) else True:
        if isinstance(data, dict) and data.get("chimes_enabled"):
            cfg["clock_mode"] = "chime"
        else:
            cfg["clock_mode"] = "voice"

    if cfg["language"] not in ("ru", "de"):
        cfg["language"] = "ru"
    if cfg["clock_mode"] not in ("voice", "chime"):
        cfg["clock_mode"] = "voice"

    cfg["volume"] = max(0, min(30, int(cfg["volume"])))
    cfg["quiet_start"] = int(cfg["quiet_start"]) % 24
    cfg["quiet_end"] = int(cfg["quiet_end"]) % 24
    cfg["quiet_enabled"] = bool(cfg["quiet_enabled"])
    cfg["half_hour_enabled"] = bool(cfg["half_hour_enabled"])
    cfg["rtc_correction_sec_per_day"] = max(
        -30, min(30, int(cfg["rtc_correction_sec_per_day"]))
    )
    return cfg


def load_config():
    try:
        with open(CONFIG_PATH, "r") as f:
            return _validated(json.load(f))
    except (OSError, ValueError):
        return dict(DEFAULTS)


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
