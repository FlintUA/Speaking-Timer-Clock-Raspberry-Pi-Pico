# Standalone Wi-Fi smoke test for Raspberry Pi Pico W / Pico 2 W.
# Copy to Pico root together with wifi_manager.py and wifi_secrets.py,
# then run this file manually from Thonny.

from wifi_manager import WiFiManager
from wifi_secrets import WIFI_SSID, WIFI_PASSWORD

print("Wi-Fi test starting")

wifi = WiFiManager(WIFI_SSID, WIFI_PASSWORD, timeout_ms=10000)

if wifi.connect():
    print("Wi-Fi: CONNECTED")
    print("IP:", wifi.ifconfig()[0])
    print("Netmask:", wifi.ifconfig()[1])
    print("Gateway:", wifi.ifconfig()[2])
    print("DNS:", wifi.ifconfig()[3])
else:
    print("Wi-Fi: FAILED")
    print("Status:", wifi.status())
