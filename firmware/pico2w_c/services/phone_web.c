#include "phone_web.h"
#include "network_time.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "dhcpserver.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/ip.h"
#include "lwip/mem.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/time.h"

static bool ready, direct_active;
static dhcp_server_t dhcp;
static char address[IP4ADDR_STRLEN_MAX];
static phone_web_snapshot_t published = {.player = "Closed", .player_mode = ""};
static uint32_t published_command_id;
static phone_access_t access;

typedef enum { POST_PAIR, POST_TIMER, POST_PLAYER, POST_SETTINGS, POST_ACCESS, POST_LOGOUT } phone_post_action_t;

typedef struct {
    void *connection;
    phone_post_action_t action;
    bool home_peer;
    uint16_t length, received;
    char body[96];
} phone_post_t;

static phone_post_t posts[2];

static uint64_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void reset_requests_locked(void) {
    phone_access_reset(&access, get_rand_32());
    published_command_id = 0;
    memset(posts, 0, sizeof(posts));
}

bool phone_web_begin_pair(void) {
    if (!ready || !address[0]) return false;
    uint32_t code = get_rand_32() % 1000000u;
    cyw43_arch_lwip_begin();
    phone_access_begin(&access, code, now_ms());
    cyw43_arch_lwip_end();
    return true;
}

bool phone_web_pair_code(uint32_t *code) {
    if (!ready) return false;
    cyw43_arch_lwip_begin();
    bool active = access.attempts && now_ms() < access.code_until;
    if (active) *code = access.code;
    cyw43_arch_lwip_end();
    return active;
}

bool phone_web_pair_succeeded(void) {
    if (!ready) return false;
    cyw43_arch_lwip_begin();
    bool paired = access.paired;
    cyw43_arch_lwip_end();
    return paired;
}

void phone_web_revoke(void) {
    if (!ready) return;
    cyw43_arch_lwip_begin();
    memset(access.settings.browsers, 0, sizeof(access.settings.browsers));
    reset_requests_locked();
    access.changed = true;
    cyw43_arch_lwip_end();
}

void phone_web_load_settings(const phone_settings_t *settings) {
    access.settings = *settings; // Before starting the web server.
}

void phone_web_get_settings(phone_settings_t *settings) {
    if (ready) cyw43_arch_lwip_begin();
    *settings = access.settings;
    if (ready) cyw43_arch_lwip_end();
}

bool phone_web_take_changes(void) {
    if (!ready) return false;
    cyw43_arch_lwip_begin();
    bool changed = access.changed;
    access.changed = false;
    cyw43_arch_lwip_end();
    return changed;
}

bool phone_web_home_trusted(void) {
    return access.settings.home_trusted != 0;
}

void phone_web_set_home_trusted(bool enabled) {
    if (ready) cyw43_arch_lwip_begin();
    if (access.settings.home_trusted != (uint8_t)enabled) {
        access.settings.home_trusted = (uint8_t)enabled;
        if (ready) reset_requests_locked();
        access.changed = true;
    }
    if (ready) cyw43_arch_lwip_end();
}

bool phone_web_command_pending(phone_command_t *request) {
    if (!ready) return false;
    cyw43_arch_lwip_begin();
    bool pending = access.last.id && access.result == PHONE_COMMAND_PENDING;
    if (pending) *request = access.last;
    cyw43_arch_lwip_end();
    return pending;
}

void phone_web_command_complete(uint32_t id, bool applied) {
    cyw43_arch_lwip_begin();
    if (access.last.id == id)
        access.result = applied ? PHONE_COMMAND_APPLIED : PHONE_COMMAND_BLOCKED;
    cyw43_arch_lwip_end();
}

void phone_web_init(bool radio_ready) {
    ready = radio_ready;
    if (!ready) return;
    cyw43_arch_lwip_begin();
    reset_requests_locked();
    httpd_init();
    cyw43_arch_lwip_end();
}

bool phone_web_direct(void) { return direct_active; }
const char *phone_web_address(void) { return address; }
const char *phone_web_password(void) { return access.settings.password; }

static void new_password_locked(void) {
    uint64_t random = get_rand_64();
    snprintf(access.settings.password, sizeof(access.settings.password), "%016" PRIx64, random);
    access.changed = true;
}

bool phone_web_rotate_password(void) {
    if (!ready) return false;
    bool restart_direct = direct_active;
    if (restart_direct && !phone_web_set_direct(false)) return false;
    cyw43_arch_lwip_begin();
    new_password_locked();
    for (unsigned i = 0; i < PHONE_BROWSER_MAX; ++i)
        if (access.settings.browsers[i].scope == PHONE_SCOPE_DIRECT)
            access.settings.browsers[i] = (phone_browser_t){0};
    cyw43_arch_lwip_end();
    return !restart_direct || phone_web_set_direct(true);
}

bool phone_web_set_direct(bool direct) {
    if (!ready) return false;
    if (direct == direct_active) return true;
    cyw43_arch_lwip_begin();
    address[0] = '\0';
    reset_requests_locked();
    cyw43_arch_lwip_end();
    if (direct) {
        network_time_suspend_home(true);
        cyw43_arch_lwip_begin();
        cyw43_arch_disable_sta_mode();
        if (!access.settings.password[0]) new_password_locked();
        cyw43_arch_enable_ap_mode(PHONE_WEB_SSID, access.settings.password, CYW43_AUTH_WPA2_AES_PSK);
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
    published_command_id = access.last.id;
    if (published_command_id && access.result == PHONE_COMMAND_PENDING) --published_command_id;
    char new_address[sizeof(address)] = {0};
    struct netif *net = &cyw43_state.netif[direct_active ? CYW43_ITF_AP : CYW43_ITF_STA];
    if (netif_is_up(net) && (direct_active || netif_is_link_up(net)) &&
        !ip4_addr_isany(netif_ip4_addr(net))) {
        ip4addr_ntoa_r(netif_ip4_addr(net), new_address, sizeof(new_address));
    }
    if (strcmp(address, new_address) != 0) {
        memcpy(address, new_address, sizeof(address));
        reset_requests_locked();
    }
    cyw43_arch_lwip_end();
}

static bool trusted_home_peer(void) {
    const struct netif *home = &cyw43_state.netif[CYW43_ITF_STA];
    const ip_addr_t *peer = ip_current_src_addr();
    return !direct_active && access.settings.home_trusted &&
           ip_current_input_netif() == home && IP_IS_V4(peer) &&
           !ip4_addr_isany(netif_ip4_netmask(home)) &&
           ip4_addr_netcmp(ip_2_ip4(peer), netif_ip4_addr(home), netif_ip4_netmask(home));
}

static const char *reply_path(phone_reply_t reply) {
    static const char *const paths[] = {
        "/phone-ok.json", "/phone-invalid.json", "/phone-denied.json", "/phone-busy.json"
    };
    return paths[reply];
}

err_t httpd_post_begin(void *connection, const char *uri, const char *header,
                      u16_t header_length, int length, char *response_uri,
                      u16_t response_length, u8_t *auto_window) {
    (void)auto_window;
    phone_post_action_t action;
    if (strcmp(uri, "/pair") == 0) action = POST_PAIR;
    else if (strcmp(uri, "/timer") == 0) action = POST_TIMER;
    else if (strcmp(uri, "/player") == 0) action = POST_PLAYER;
    else if (strcmp(uri, "/settings") == 0) action = POST_SETTINGS;
    else if (strcmp(uri, "/access") == 0) action = POST_ACCESS;
    else if (strcmp(uri, "/logout") == 0) action = POST_LOGOUT;
    else return ERR_ARG;
    // lwIP passes the header after the URI and terminates it at the blank line.
    if (!phone_access_same_origin(header, header_length, address)) {
        snprintf(response_uri, response_length, "%s", reply_path(PHONE_DENIED));
        return ERR_ARG;
    }
    if (length < 1 || (size_t)length >= sizeof(posts[0].body)) {
        snprintf(response_uri, response_length, "%s", reply_path(PHONE_INVALID));
        return ERR_ARG;
    }
    for (unsigned i = 0; i < sizeof(posts) / sizeof(posts[0]); ++i) {
        if (!posts[i].connection) {
            posts[i] = (phone_post_t){.connection = connection, .action = action,
                                      .home_peer = trusted_home_peer(), .length = (uint16_t)length};
            return ERR_OK;
        }
    }
    snprintf(response_uri, response_length, "%s", reply_path(PHONE_BUSY));
    return ERR_MEM;
}

static phone_post_t *post_for(void *connection) {
    for (unsigned i = 0; i < sizeof(posts) / sizeof(posts[0]); ++i)
        if (posts[i].connection == connection) return &posts[i];
    return NULL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    phone_post_t *post = post_for(connection);
    err_t result = ERR_ARG;
    if (post && p->tot_len <= post->length - post->received) {
        uint16_t copied = pbuf_copy_partial(p, post->body + post->received, p->tot_len, 0);
        post->received += copied;
        if (copied == p->tot_len) result = ERR_OK;
    }
    pbuf_free(p);
    return result;
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_length) {
    phone_post_t *post = post_for(connection);
    phone_reply_t reply = PHONE_INVALID;
    if (post && post->received == post->length && !memchr(post->body, '\0', post->length)) {
        post->body[post->length] = '\0';
        uint8_t scope = direct_active ? PHONE_SCOPE_DIRECT : PHONE_SCOPE_HOME;
        switch (post->action) {
            case POST_PAIR: reply = phone_access_pair(&access, post->body, now_ms(), scope); break;
            case POST_TIMER: reply = phone_access_timer(&access, post->body, scope, post->home_peer); break;
            case POST_PLAYER: reply = phone_access_player(&access, post->body, scope, post->home_peer); break;
            case POST_SETTINGS: reply = phone_access_settings(&access, post->body, scope, post->home_peer); break;
            case POST_ACCESS: reply = phone_access_check(&access, post->body, scope); break;
            case POST_LOGOUT: reply = phone_access_logout(&access, post->body, scope); break;
        }
    }
    if (post) memset(post, 0, sizeof(*post));
    snprintf(response_uri, response_length, "%s", reply_path(reply));
}

int fs_open_custom(struct fs_file *file, const char *name) {
    const bool snapshot = strcmp(name, "/status.json") == 0;
    const char *status = NULL;
    if (!snapshot) {
        static const char *const codes[] = {"200 OK", "400 Bad Request", "403 Forbidden", "409 Conflict"};
        for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
            if (strcmp(name, reply_path((phone_reply_t)i)) == 0) status = codes[i];
        if (!status) return 0;
    }
    const size_t capacity = 1024;
    char *response = mem_malloc(capacity);
    if (!response) return 0;
    int len = snprintf(response, capacity, "HTTP/1.0 %s\r\nContent-Type: application/json\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n", snapshot ? "200 OK" : status);
    if (len < 0 || (size_t)len >= capacity) {
        mem_free(response);
        return 0;
    }
    int body_length;
    if (snapshot) {
        static const char *const results[] = {"pending", "applied", "blocked"};
        body_length = snprintf(response + len, capacity - (size_t)len,
        "{\"time\":\"%s\",\"timer_running\":%s,\"timer_seconds\":%" PRIu32
        ",\"player\":\"%s\",\"track\":%u,\"output\":\"%s\",\"timer_request\":%" PRIu32
        ",\"timer_result\":\"%s\",\"timer_nonce\":%" PRIu32 ",\"epoch\":%" PRIu32
        ",\"home_trusted\":%s,\"track_count\":%u,\"player_mode\":\"%s\""
        ",\"command_id\":%" PRIu32 ",\"command_nonce\":%" PRIu32 ",\"command_result\":\"%s\""
        ",\"settings\":{\"snapshot_id\":%" PRIu32 ",\"volume_local\":%u,\"volume_bt\":%u,\"language\":%u"
        ",\"audio_manual\":%u,\"audio_hourly\":%u,\"audio_timer\":%u,\"audio_alarm\":%u"
        ",\"light_mode\":%u,\"light_day\":%u,\"light_night\":%u"
        ",\"editing\":%s,\"idle\":%s,\"bt_supported\":%s,\"saving\":\"%s\"}}\n",
        published.time, published.timer_running ? "true" : "false",
        published.timer_seconds, published.player, (unsigned)published.track,
        published.bluetooth ? "BT" : "LOCAL", access.last.id,
        access.last.id ? results[access.result] : "none", access.last.nonce, access.epoch,
        trusted_home_peer() ? "true" : "false", (unsigned)published.track_count, published.player_mode,
        access.last.id, access.last.nonce, access.last.id ? results[access.result] : "none",
        published_command_id,
        (unsigned)published.preferences.volume_local, (unsigned)published.preferences.volume_bt,
        (unsigned)published.preferences.language,
        (unsigned)published.preferences.audio[0], (unsigned)published.preferences.audio[1],
        (unsigned)published.preferences.audio[2], (unsigned)published.preferences.audio[3],
        (unsigned)published.preferences.light.mode, (unsigned)published.preferences.light.day_percent,
        (unsigned)published.preferences.light.night_percent,
        published.preferences.editing ? "true" : "false", published.preferences.idle ? "true" : "false",
        published.preferences.bt_supported ? "true" : "false",
        published.preferences.retry ? "retry" : published.preferences.pending || access.changed ? "pending" : "saved");
    } else {
        body_length = snprintf(response + len, capacity - (size_t)len, "{}\n");
    }
    if (body_length < 0 || (size_t)body_length >= capacity - (size_t)len) {
        mem_free(response);
        return 0;
    }
    len += body_length;
    *file = (struct fs_file){.data = response, .len = len, .index = len,
                             .flags = FS_FILE_FLAGS_HEADER_INCLUDED};
    return 1;
}

void fs_close_custom(struct fs_file *file) {
    mem_free((void *)file->data);
}
