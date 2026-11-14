#include "network_time.h"
#include "clock_time.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/dns.h"
#include "lwip/udp.h"
#include <stdio.h>
#include <string.h>

static bool ready, configured, joining, waiting, dns_pending, request_now, home_suspended;
static bool received, anchor_valid, apply_pending;
static bool request_failed;
static char ssid_value[33], password_value[65];
static uint64_t join_started, next_join, deadline, next_ntp;
static uint64_t sample_epoch, sample_ms, anchor_epoch, anchor_ms;
static uint64_t last_sync_ms;
static uint64_t last_applied_ms;
static uint64_t next_apply;
static unsigned join_failures;
static unsigned join_attempts;
static int last_link = CYW43_LINK_DOWN;
static int applied_offset;
static ip_addr_t server;
static struct udp_pcb *pcb;
static uint8_t nonce[8];

static uint64_t now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
static void send_request(void) {
    if (!waiting || !pcb) return;
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,48,PBUF_RAM);
    if (!p) { waiting=false; request_failed=true; return; }
    uint8_t data[48]={0}; data[0]=0x23; // NTPv4 client.
    uint64_t value=time_us_64() ^ ((uint64_t)to_us_since_boot(get_absolute_time())<<17);
    for (unsigned i=0;i<8;i++) nonce[i]=(uint8_t)(value>>(i*8));
    memcpy(data+40,nonce,8);
    if (pbuf_take(p,data,sizeof(data)) != ERR_OK ||
        udp_sendto(pcb,p,&server,123) != ERR_OK) {waiting=false;request_failed=true;}
    pbuf_free(p);
}
static void dns_found(const char *host, const ip_addr_t *addr, void *arg) {
    (void)host; (void)arg; dns_pending=false;
    if (!waiting) return;
    if (!addr) { waiting=false; request_failed=true; return; }
    server=*addr; send_request();
}
static void receive(void *arg, struct udp_pcb *udp, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)udp;
    if (!p) return;
    uint8_t data[48]; uint64_t epoch;
    if (waiting && !dns_pending && port==123 && ip_addr_cmp(addr,&server) &&
        p->tot_len>=48 && pbuf_copy_partial(p,data,48,0)==48 &&
        clock_ntp_decode(data,sizeof(data),nonce,&epoch)) {
        sample_epoch=epoch; sample_ms=now_ms(); received=true; waiting=false;
    }
    pbuf_free(p);
}
void network_time_init(bool radio_ready) {
    ready=radio_ready;
    if (!ready) return;
    cyw43_arch_enable_sta_mode();
    cyw43_arch_lwip_begin();
    pcb=udp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb) udp_recv(pcb,receive,NULL);
    cyw43_arch_lwip_end();
    ready=pcb!=NULL;
}
void network_time_configure(const char *ssid, const char *password) {
    snprintf(ssid_value,sizeof(ssid_value),"%s",ssid?ssid:"");
    snprintf(password_value,sizeof(password_value),"%s",password?password:"");
    configured=ssid_value[0]!=0; joining=false; next_join=0; join_failures=0;
    join_attempts=0; last_link=CYW43_LINK_DOWN;
    if (ready) {
        cyw43_arch_lwip_begin();
        waiting=false; received=false;
        cyw43_arch_lwip_end();
    }
    next_ntp=0; request_now=true; request_failed=false;
    if (ready && !home_suspended) cyw43_wifi_leave(&cyw43_state,CYW43_ITF_STA);
    puts(configured ? "[WIFI] Configuration accepted (password hidden)" :
                      "[WIFI] Disabled; RTC continues offline");
}
void network_time_request(void) { request_now=true; request_failed=false; }
void network_time_manual_adjusted(void) {
    if (ready) cyw43_arch_lwip_begin();
    waiting=false; received=false; anchor_valid=false; apply_pending=false;
    applied_offset=0; next_apply=0; request_now=false; request_failed=false;
    next_ntp=now_ms()+3600000u;
    // Leave dns_pending for its callback; waiting=false makes it harmless.
    if (ready) cyw43_arch_lwip_end();
    puts("[NTP] Manual RTC edit; old reference discarded, next periodic sync in 1h");
}
#ifdef CLOCK_ENABLE_PHONE_WEB
void network_time_suspend_home(bool suspended) {
    home_suspended = suspended;
    joining = false;
    next_join = 0;
    join_failures = 0;
    join_attempts = 0;
    last_link = CYW43_LINK_DOWN;
    request_now = true;
    cyw43_arch_lwip_begin();
    waiting = false;
    received = false;
    // Let an outstanding DNS callback clear dns_pending. It cannot send
    // another NTP request while waiting is false.
    cyw43_arch_lwip_end();
}
#endif
network_home_status_t network_time_home_status(uint64_t now) {
    network_home_status_t status = {0};
    if (!ready) status.state = NETWORK_HOME_OFFLINE;
    else if (!configured) status.state = NETWORK_HOME_UNCONFIGURED;
    else if (home_suspended) status.state = NETWORK_HOME_SUSPENDED;
    else if (last_link == CYW43_LINK_UP) status.state = NETWORK_HOME_READY;
    else if (joining) {
        status.state = last_link == CYW43_LINK_JOIN || last_link == CYW43_LINK_NOIP ?
            NETWORK_HOME_ADDRESS : NETWORK_HOME_CONNECTING;
        status.seconds = (uint32_t)((now - join_started) / 1000u);
    } else if (now < next_join) {
        status.state = NETWORK_HOME_RETRY;
        status.seconds = (uint32_t)((next_join - now + 999u) / 1000u);
    } else status.state = NETWORK_HOME_WAIT_IDLE;
    return status;
}
void network_time_status(void) {
    printf("[NTP] zone=Europe/Berlin configured=%s radio=%s synced=%s offset=%d last_sync_age=%llus\n",
        configured?"YES":"NO",ready?"READY":"OFFLINE",anchor_valid?"YES":"NO",
        applied_offset,(unsigned long long)(anchor_valid?(now_ms()-last_sync_ms)/1000:0));
}
static uint32_t age_seconds(uint64_t now, uint64_t then) {
    uint64_t seconds = now >= then ? (now - then) / 1000u : 0;
    return seconds > UINT32_MAX ? UINT32_MAX : (uint32_t)seconds;
}
network_reference_status_t network_time_reference_status(uint64_t now) {
    if (ready) cyw43_arch_lwip_begin();
    network_reference_status_t status = {
        .available = ready, .configured = configured, .sampled = anchor_valid,
        .synced = applied_offset != 0, .offset = applied_offset,
        .pending = anchor_valid && (apply_pending ||
            applied_offset != clock_berlin_offset(anchor_epoch +
                (now >= anchor_ms ? (now - anchor_ms) / 1000u : 0))),
          .requesting = request_now || waiting || dns_pending || received,
          .retry = anchor_valid && next_apply > now, .failed=request_failed,
        .sample_age = anchor_valid ? age_seconds(now, last_sync_ms) : 0,
        .sync_age = applied_offset ? age_seconds(now, last_applied_ms) : 0
    };
    if (ready) cyw43_arch_lwip_end();
    return status;
}
bool network_time_service(uint64_t now, bool idle, bool join_allowed, ds1302_datetime_t *local) {
    if (!ready) return false;
    bool fresh=false, timed_out=false;
    cyw43_arch_lwip_begin();
    if (received) {
        anchor_epoch=sample_epoch; anchor_ms=sample_ms; received=false;
        anchor_valid=true; apply_pending=true; last_sync_ms=now;
          next_ntp=now+3600000; fresh=true; request_failed=false;
    }
      if (waiting && now>=deadline) { waiting=false; timed_out=true; request_failed=true; }
    cyw43_arch_lwip_end();
    if (fresh) puts("[NTP] Validated UTC sample received");
    if (timed_out) puts("[NTP] Request timed out; RTC remains active");
    if (configured && !home_suspended) {
        int link=cyw43_tcpip_link_status(&cyw43_state,CYW43_ITF_STA);
        last_link=link;
        if (link!=CYW43_LINK_UP) {
            if (joining && (now-join_started>=15000 || link<0)) {
                joining=false; ++join_failures;
                next_join=now+(join_failures<3?10000:60000);
                printf("[WIFI] Join attempt=%u failed link=%d elapsed=%llums; retry in %llus\n",
                    join_attempts, link, (unsigned long long)(now-join_started),
                    (unsigned long long)((next_join-now)/1000u));
            }
            if (!joining && join_allowed && now>=next_join) {
                uint32_t auth=password_value[0]?CYW43_AUTH_WPA2_AES_PSK:CYW43_AUTH_OPEN;
                int rc=cyw43_arch_wifi_connect_async(ssid_value,password_value,auth);
                joining=rc==0; join_started=now;
                next_join=now+60000;
                ++join_attempts;
                printf("[WIFI] Join attempt=%u started status=%d%s\n", join_attempts, rc,
                    joining ? "" : "; retry in 60s");
            }
        } else {
            if (joining) {
                joining=false; join_failures=0;
                printf("[WIFI] Connected attempt=%u elapsed=%llums; IPv4 ready\n",
                    join_attempts, (unsigned long long)(now-join_started));
            }
            cyw43_arch_lwip_begin();
            if (!waiting && !dns_pending && idle && (request_now || now>=next_ntp)) {
                request_now=false; request_failed=false; waiting=true; deadline=now+10000; next_ntp=now+60000;
                err_t rc=dns_gethostbyname("pool.ntp.org",&server,dns_found,NULL);
                if (rc==ERR_OK) send_request();
                else if (rc==ERR_INPROGRESS) dns_pending=true;
                else {waiting=false;request_failed=true;}
            }
            cyw43_arch_lwip_end();
        }
    }
    if (anchor_valid && idle && now>=next_apply) {
        uint64_t epoch=anchor_epoch+(now-anchor_ms)/1000;
        if ((apply_pending || applied_offset!=clock_berlin_offset(epoch)) &&
            clock_berlin_datetime(epoch,local)) return true;
    }
    return false;
}
bool network_time_is_dst_change(void) {
    return anchor_valid && applied_offset!=0 &&
        applied_offset!=clock_berlin_offset(anchor_epoch+(now_ms()-anchor_ms)/1000);
}
void network_time_applied(bool success) {
    if (success) {
        last_applied_ms=now_ms();
        applied_offset=clock_berlin_offset(anchor_epoch+(now_ms()-anchor_ms)/1000);
        apply_pending=false;
        next_apply=0;
        printf("[NTP] RTC synchronized, Europe/Berlin UTC%+d\n",applied_offset/3600);
    } else {
        next_apply=now_ms()+60000;
        puts("[NTP] RTC write failed; retry in 60 seconds when idle");
    }
}
