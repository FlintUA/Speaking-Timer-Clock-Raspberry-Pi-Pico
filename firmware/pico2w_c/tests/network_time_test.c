#include <assert.h>
#include "../services/network_time.c"

int cyw43_state;
static uint64_t test_now;
static int test_link, connect_result;
static unsigned connect_calls, dns_calls;
static struct udp_pcb test_udp;

absolute_time_t get_absolute_time(void) { return test_now; }
uint64_t to_ms_since_boot(absolute_time_t value) { return value; }
uint64_t to_us_since_boot(absolute_time_t value) { return value * 1000u; }
uint64_t time_us_64(void) { return test_now * 1000u; }
void cyw43_arch_enable_sta_mode(void) {}
void cyw43_arch_lwip_begin(void) {}
void cyw43_arch_lwip_end(void) {}
int cyw43_wifi_leave(int *state, int interface) { (void)state; (void)interface; return 0; }
int cyw43_tcpip_link_status(int *state, int interface) {
    (void)state; (void)interface; return test_link;
}
int cyw43_arch_wifi_connect_async(const char *ssid, const char *password, uint32_t auth) {
    assert(!strcmp(ssid, "TestNetwork") && !strcmp(password, "test-password"));
    assert(auth == CYW43_AUTH_WPA2_AES_PSK);
    ++connect_calls;
    return connect_result;
}
struct udp_pcb *udp_new_ip_type(int type) { (void)type; return &test_udp; }
void udp_recv(struct udp_pcb *udp, void (*callback)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t), void *arg) {
    (void)udp; (void)callback; (void)arg;
}
err_t udp_sendto(struct udp_pcb *udp, struct pbuf *p, const ip_addr_t *address, u16_t port) {
    (void)udp; (void)p; (void)address; (void)port; return ERR_OK;
}
struct pbuf *pbuf_alloc(int layer, u16_t size, int type) { (void)layer; (void)size; (void)type; return NULL; }
err_t pbuf_take(struct pbuf *p, const void *data, u16_t size) { (void)p; (void)data; (void)size; return ERR_OK; }
u16_t pbuf_copy_partial(const struct pbuf *p, void *data, u16_t size, u16_t offset) {
    (void)p; (void)data; (void)size; (void)offset; return 0;
}
void pbuf_free(struct pbuf *p) { (void)p; }
bool ip_addr_cmp(const ip_addr_t *a, const ip_addr_t *b) { return a->address == b->address; }
err_t dns_gethostbyname(const char *host, ip_addr_t *address,
                       void (*callback)(const char *, const ip_addr_t *, void *), void *arg) {
    (void)host; (void)address; (void)callback; (void)arg; ++dns_calls; return -1;
}

static bool service(uint64_t now, bool idle, bool join_allowed) {
    ds1302_datetime_t local;
    test_now = now;
    return network_time_service(now, idle, join_allowed, &local);
}

int main(void) {
    network_time_init(false);
    assert(!network_time_reference_status(0).available);
    assert(network_time_home_status(0).state == NETWORK_HOME_OFFLINE);
    network_time_init(true);
    assert(network_time_home_status(0).state == NETWORK_HOME_UNCONFIGURED);
    network_time_configure("TestNetwork", "test-password");
    network_time_suspend_home(true);
    assert(network_time_home_status(0).state == NETWORK_HOME_SUSPENDED);
    assert(!service(0, true, true) && connect_calls == 0);
    network_time_suspend_home(false);
    assert(!service(1000, false, false) && connect_calls == 0);
    assert(network_time_home_status(1000).state == NETWORK_HOME_WAIT_IDLE);
    // The phone menu allows a Wi-Fi join while RTC updates remain blocked.
    assert(!service(1250, false, true) && connect_calls == 1);
    assert(network_time_home_status(2250).state == NETWORK_HOME_CONNECTING);
    assert(network_time_home_status(2250).seconds == 1);
    test_link = CYW43_LINK_NOIP;
    assert(!service(2500, false, true));
    assert(network_time_home_status(2500).state == NETWORK_HOME_ADDRESS);
    test_link = CYW43_LINK_UP;
    assert(!service(3000, false, true) && dns_calls == 0);
    assert(network_time_home_status(3000).state == NETWORK_HOME_READY);
    anchor_valid = true; apply_pending = true; anchor_epoch = 1794384000u; anchor_ms = 3000;
    assert(!service(3250, false, true));
    assert(service(3500, true, true) && dns_calls == 1);
    network_reference_status_t reference=network_time_reference_status(3500);
    assert(reference.available && reference.configured && reference.sampled);
    assert(reference.pending && !reference.synced);
    network_time_applied(false);
    assert(!network_time_reference_status(3500).synced);
    assert(network_time_reference_status(3500).retry);
    network_time_applied(true);
    reference=network_time_reference_status(4500);
    assert(reference.synced && !reference.pending && !reference.retry && reference.sync_age==1);
    assert(reference.offset==clock_berlin_offset(anchor_epoch));
    anchor_valid = false;
    network_time_suspend_home(true);
    network_time_suspend_home(false);
    test_link = CYW43_LINK_DOWN;
    assert(!service(4000, false, true) && connect_calls == 2);
    assert(!service(19000, false, true));
    assert(network_time_home_status(19000).state == NETWORK_HOME_RETRY);
    assert(network_time_home_status(19001).seconds == 10);
    assert(!service(28999, false, true) && connect_calls == 2);
    assert(!service(29000, false, true) && connect_calls == 3);
    test_link = CYW43_LINK_FAIL;
    assert(!service(29250, false, true));
    assert(network_time_home_status(29250).seconds == 10);
    test_link = CYW43_LINK_DOWN;
    connect_result = -1;
    assert(!service(39250, false, true) && connect_calls == 4);
    assert(network_time_home_status(39250).seconds == 60);
    assert(network_time_reference_status(UINT64_MAX).sync_age==UINT32_MAX);
    test_now=50000;test_link=CYW43_LINK_UP;
    waiting=true;dns_pending=true;received=true;anchor_valid=true;apply_pending=true;
    request_now=true;request_failed=true;applied_offset=3600;next_apply=90000;
    network_time_manual_adjusted();
    reference=network_time_reference_status(test_now);
    assert(!reference.synced && !reference.sampled && !reference.pending && !reference.retry && !reference.failed);
    assert(!waiting && !received && !request_now && !applied_offset && next_ntp==3650000);
    assert(dns_pending);dns_found("old",NULL,NULL);assert(!dns_pending && !waiting);
    unsigned requests=dns_calls;assert(!service(51000,true,true) && dns_calls==requests);
    network_time_request();assert(network_time_reference_status(51000).requesting);
    assert(!service(51250,true,true) && dns_calls==requests+1);
    assert(network_time_reference_status(51250).failed);
    network_time_request();assert(!network_time_reference_status(51250).failed);
    waiting=true;request_now=false;deadline=52000;next_ntp=100000;
    assert(!service(52000,true,true));assert(network_time_reference_status(52000).failed);
    received=true;sample_epoch=1794384000;sample_ms=52000;
    assert(service(52250,true,true));assert(!network_time_reference_status(52250).failed);
    puts("network_time_test: PASS");
    return 0;
}
