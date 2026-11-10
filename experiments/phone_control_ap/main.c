#include <inttypes.h>
#include <stdio.h>

#include "dhcpserver.h"
#include "lwip/apps/httpd.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

static dhcp_server_t dhcp;
static const char ssid[] = "SpeakingClock";

int main(void) {
    stdio_init_all();
    char password[17];
    snprintf(password, sizeof(password), "%016" PRIx64, get_rand_64());

    const char *error = NULL;
    if (cyw43_arch_init()) {
        error = "Radio initialization failed";
    } else {
        cyw43_arch_enable_ap_mode(ssid, password, CYW43_AUTH_WPA2_AES_PSK);
        cyw43_arch_lwip_begin();
        struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
        ip_addr_t address = *netif_ip4_addr(ap);
        ip_addr_t netmask = *netif_ip4_netmask(ap);
        dhcp_server_init(&dhcp, ap, &address, &netmask);
        if (dhcp.udp) {
            httpd_init();
        } else {
            error = "DHCP initialization failed";
        }
        cyw43_arch_lwip_end();
        if (error) cyw43_arch_disable_ap_mode();
    }

    bool was_connected = false;
    while (true) {
        bool connected = stdio_usb_connected();
        if (connected && (!was_connected || getchar_timeout_us(0) == '?')) {
            if (error) {
                printf("[PHONE-AP] ERROR %s\n", error);
            } else {
                printf("[PHONE-AP] SSID: %s\n"
                       "[PHONE-AP] WPA2 password: %s\n"
                       "[PHONE-AP] Open http://192.168.4.1/\n", ssid, password);
            }
        }
        was_connected = connected;
        sleep_ms(50);
    }
}
