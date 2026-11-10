#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lwip/apps/httpd.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

static bool radio_ready;

static bool printable_ascii(const char *text) {
    for (; *text; ++text)
        if ((unsigned char)*text < 32 || (unsigned char)*text > 126) return false;
    return true;
}

static bool valid_password(const char *password) {
    size_t length = strlen(password);
    if (!length) return true;
    if (length < 8 || length > 64 || !printable_ascii(password)) return false;
    if (length == 64)
        for (; *password; ++password)
            if (!isxdigit((unsigned char)*password)) return false;
    return true;
}

static void status(void) {
    if (!radio_ready) {
        puts("[PHONE-STA] ERROR radio initialization failed");
    } else if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
        char address[IP4ADDR_STRLEN_MAX];
        cyw43_arch_lwip_begin();
        ip4addr_ntoa_r(netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]),
                      address, sizeof(address));
        cyw43_arch_lwip_end();
        printf("[PHONE-STA] URL http://%s/\n", address);
    } else {
        puts("[PHONE-STA] Ready for local Wi-Fi setup");
    }
}

static void command(char *line) {
    if (strcmp(line, "phone-sta-status") == 0) {
        status();
        return;
    }
    static const char prefix[] = "wifi-config ";
    if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) {
        puts("[PHONE-STA] Use the local setup script or phone-sta-status");
        return;
    }
    char *ssid = line + sizeof(prefix) - 1;
    char *password = strchr(ssid, '|');
    if (!password || password == ssid || password - ssid > 32) {
        puts("[PHONE-STA] ERROR invalid network configuration");
        return;
    }
    *password++ = '\0';
    if (!printable_ascii(ssid) || !valid_password(password)) {
        puts("[PHONE-STA] ERROR invalid network configuration");
        return;
    }
    if (!radio_ready || cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP) {
        status();
        return;
    }

    puts("[PHONE-STA] Connecting");
    uint32_t auth = *password ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    int result = cyw43_arch_wifi_connect_timeout_ms(ssid, password, auth, 20000);
    if (result == 0) {
        status();
    } else {
        cyw43_arch_disable_sta_mode();
        cyw43_arch_enable_sta_mode();
        printf("[PHONE-STA] ERROR connect failed (%d)\n", result);
    }
}

int main(void) {
    stdio_init_all();
    radio_ready = cyw43_arch_init() == 0;
    if (radio_ready) {
        cyw43_arch_enable_sta_mode();
        cyw43_arch_lwip_begin();
        httpd_init();
        cyw43_arch_lwip_end();
    }

    char line[128];
    size_t length = 0;
    bool invalid_line = false;
    while (true) {
        int byte = getchar_timeout_us(100000);
        if (!stdio_usb_connected()) {
            memset(line, 0, sizeof(line));
            length = 0;
            invalid_line = false;
        } else if (byte == '\n') {
            if (invalid_line) {
                puts("[PHONE-STA] ERROR invalid command");
            } else if (length) {
                line[length] = '\0';
                command(line);
            }
            memset(line, 0, sizeof(line));
            length = 0;
            invalid_line = false;
        } else if (byte >= 0 && byte != '\r') {
            if (length == sizeof(line) - 1 || byte < 32 || byte > 126)
                invalid_line = true;
            else if (!invalid_line)
                line[length++] = (char)byte;
        }
    }
}
