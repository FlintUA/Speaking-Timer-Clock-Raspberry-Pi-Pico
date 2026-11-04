# Speaking Timer-Clock v3 - NTP reference for Pico W / Pico 2 W
# Returns Europe/Berlin local civil time without changing machine.RTC().

import socket
import struct

from rtc_calibration import (
    datetime_seconds,
    seconds_to_datetime,
    days_in_month,
)

NTP_PORT = 123
NTP_PACKET_SIZE = 48
NTP_DELTA_2000 = 3155673600
DEFAULT_SERVER = "pool.ntp.org"
DEFAULT_TIMEOUT_SEC = 3


def _weekday_for_date(year, month, day):
    offsets = (0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4)
    y = year - (1 if month < 3 else 0)
    sunday_zero = (
        y + y // 4 - y // 100 + y // 400 + offsets[month - 1] + day
    ) % 7
    return 7 if sunday_zero == 0 else sunday_zero


def _last_sunday(year, month):
    last_day = days_in_month(year, month)
    weekday = _weekday_for_date(year, month, last_day)
    return last_day - (weekday % 7)


def _berlin_utc_offset_seconds(utc_value):
    year = int(utc_value["year"])

    march_day = _last_sunday(year, 3)
    october_day = _last_sunday(year, 10)

    dst_start = {
        "year": year, "month": 3, "day": march_day,
        "hour": 1, "minute": 0, "second": 0,
    }
    dst_end = {
        "year": year, "month": 10, "day": october_day,
        "hour": 1, "minute": 0, "second": 0,
    }

    current = datetime_seconds(utc_value)
    if datetime_seconds(dst_start) <= current < datetime_seconds(dst_end):
        return 7200
    return 3600


def query_utc(server=DEFAULT_SERVER, timeout_sec=DEFAULT_TIMEOUT_SEC):
    packet = bytearray(NTP_PACKET_SIZE)
    packet[0] = 0x1B

    addr = socket.getaddrinfo(server, NTP_PORT)[0][-1]
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.settimeout(timeout_sec)
        sock.sendto(packet, addr)
        data = sock.recv(NTP_PACKET_SIZE)
    finally:
        sock.close()

    if len(data) < NTP_PACKET_SIZE:
        raise OSError("short NTP reply")

    ntp_seconds = struct.unpack("!I", data[40:44])[0]
    seconds_since_2000 = int(ntp_seconds) - NTP_DELTA_2000
    if seconds_since_2000 <= 0:
        raise OSError("invalid NTP timestamp")

    return seconds_to_datetime(seconds_since_2000)


def query_berlin_local(server=DEFAULT_SERVER, timeout_sec=DEFAULT_TIMEOUT_SEC):
    utc_value = query_utc(server, timeout_sec)
    offset_sec = _berlin_utc_offset_seconds(utc_value)
    local_value = seconds_to_datetime(
        datetime_seconds(utc_value) + offset_sec
    )
    return local_value, offset_sec
