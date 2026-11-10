#include "phone_web.h"
#include "network_time.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "dhcpserver.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"

static bool ready, direct_active;
static dhcp_server_t dhcp;
static char password[17];
static char address[IP4ADDR_STRLEN_MAX];
static phone_web_snapshot_t published = {.player = "Closed"};

void phone_web_init(bool radio_ready) {
    ready = radio_ready;
    if (!ready) return;
    cyw43_arch_lwip_begin();
    httpd_init();
    cyw43_arch_lwip_end();
}

bool phone_web_direct(void) { return direct_active; }
const char *phone_web_address(void) { return address; }
const char *phone_web_password(void) { return password; }

bool phone_web_set_direct(bool direct) {
    if (!ready) return false;
    if (direct == direct_active) return true;
    address[0] = '\0';
    if (direct) {
        snprintf(password, sizeof(password), "%016" PRIx64, get_rand_64());
        network_time_suspend_home(true);
        cyw43_arch_lwip_begin();
        cyw43_arch_disable_sta_mode();
        cyw43_arch_enable_ap_mode(PHONE_WEB_SSID, password, CYW43_AUTH_WPA2_AES_PSK);
        struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
        if (netif_is_up(ap) && !ip4_addr_isany(netif_ip4_addr(ap))) {
            ip_addr_t ip = *netif_ip4_addr(ap);
            ip_addr_t mask = *netif_ip4_netmask(ap);
            dhcp_server_init(&dhcp, ap, &ip, &mask);
            direct_active = dhcp.udp != NULL;
        }
        cyw43_arch_lwip_end();
        if (direct_active) {
            puts("[PHONE-WEB] Direct Wi-Fi started");
            return true;
        }
    }

    cyw43_arch_lwip_begin();
    if (direct_active) dhcp_server_deinit(&dhcp);
    memset(&dhcp, 0, sizeof(dhcp));
    cyw43_arch_disable_ap_mode();
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_end();
    direct_active = false;
    memset(password, 0, sizeof(password));
    network_time_suspend_home(false);
    puts(direct ? "[PHONE-WEB] Direct Wi-Fi failed; home mode restored" :
                  "[PHONE-WEB] Home Wi-Fi resumed");
    return !direct;
}

void phone_web_status(void) {
    printf("[PHONE-WEB] mode=%s address=%s\n",
           !ready ? "OFFLINE" : direct_active ? "DIRECT" : "HOME",
           address[0] ? address : "NONE");
}

void phone_web_publish(const phone_web_snapshot_t *snapshot) {
    if (!ready) return;
    cyw43_arch_lwip_begin();
    published = *snapshot;
    struct netif *net = &cyw43_state.netif[direct_active ? CYW43_ITF_AP : CYW43_ITF_STA];
    if (netif_is_up(net) && (direct_active || netif_is_link_up(net)) &&
        !ip4_addr_isany(netif_ip4_addr(net))) {
        ip4addr_ntoa_r(netif_ip4_addr(net), address, sizeof(address));
    } else {
        address[0] = '\0';
    }
    cyw43_arch_lwip_end();
}

int fs_open_custom(struct fs_file *file, const char *name) {
    if (strcmp(name, "/status.json") != 0) return 0;
    const size_t capacity = 256;
    char *response = mem_malloc(capacity);
    if (!response) return 0;
    int len = snprintf(response, capacity,
        "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
        "{\"time\":\"%s\",\"timer_running\":%s,\"timer_seconds\":%" PRIu32
        ",\"player\":\"%s\",\"track\":%u,\"output\":\"%s\"}\n",
        published.time, published.timer_running ? "true" : "false",
        published.timer_seconds, published.player, (unsigned)published.track,
        published.bluetooth ? "BT" : "LOCAL");
    if (len < 0 || (size_t)len >= capacity) {
        mem_free(response);
        return 0;
    }
    *file = (struct fs_file){.data = response, .len = len, .index = len,
                             .flags = FS_FILE_FLAGS_HEADER_INCLUDED};
    return 1;
}

void fs_close_custom(struct fs_file *file) {
    mem_free((void *)file->data);
}
