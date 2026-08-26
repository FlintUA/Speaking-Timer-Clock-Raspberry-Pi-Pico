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
    "hourly_voice": True,
    "half_hour_voice": True,
    "chimes_enabled": False,
    "rtc_correction_sec_per_day": 0,
}


def _validated(data):
    cfg = dict(DEFAULTS)
    if isinstance(data, dict):
        cfg.update(data)

    if cfg["language"] not in ("ru", "de"):
        cfg["language"] = "ru"
    cfg["volume"] = max(0, min(30, int(cfg["volume"])))
    cfg["quiet_start"] = int(cfg["quiet_start"]) % 24
    cfg["quiet_end"] = int(cfg["quiet_end"]) % 24
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
