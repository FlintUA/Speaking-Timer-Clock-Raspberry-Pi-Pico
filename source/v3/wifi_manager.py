# Speaking Timer-Clock v3 - Pico W / Pico 2 W Wi-Fi helper
# MicroPython

import time
import network


class WiFiManager:
    def __init__(self, ssid, password, timeout_ms=8000):
        self.ssid = ssid
        self.password = password
        self.timeout_ms = int(timeout_ms)
        self.wlan = network.WLAN(network.STA_IF)

    def connect(self):
        self.wlan.active(True)

        if self.wlan.isconnected():
            return True

        self.wlan.connect(self.ssid, self.password)
        deadline = time.ticks_add(time.ticks_ms(), self.timeout_ms)

        while not self.wlan.isconnected():
            status = self.wlan.status()

            # Negative statuses are terminal failures on current RP2 builds.
            if status < 0:
                return False

            if time.ticks_diff(deadline, time.ticks_ms()) <= 0:
                return False

            time.sleep_ms(100)

        return True

    def disconnect(self):
        try:
            self.wlan.disconnect()
        except Exception:
            pass

    def connected(self):
        return bool(self.wlan.isconnected())

    def status(self):
        return self.wlan.status()

    def ifconfig(self):
        return self.wlan.ifconfig()
