#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CYW43_ITF_STA 0
#define CYW43_LINK_DOWN 0
#define CYW43_LINK_JOIN 1
#define CYW43_LINK_NOIP 2
#define CYW43_LINK_UP 3
#define CYW43_LINK_FAIL (-1)
#define CYW43_AUTH_OPEN 0u
#define CYW43_AUTH_WPA2_AES_PSK 1u
#define IPADDR_TYPE_V4 0
#define PBUF_TRANSPORT 0
#define PBUF_RAM 0
#define ERR_OK 0
#define ERR_INPROGRESS (-5)

typedef int err_t;
typedef uint16_t u16_t;
typedef uint64_t absolute_time_t;
typedef struct { uint32_t address; } ip_addr_t;
struct udp_pcb { int unused; };
struct pbuf { u16_t tot_len; };

extern int cyw43_state;
absolute_time_t get_absolute_time(void);
uint64_t to_ms_since_boot(absolute_time_t value);
uint64_t to_us_since_boot(absolute_time_t value);
uint64_t time_us_64(void);
void cyw43_arch_enable_sta_mode(void);
void cyw43_arch_lwip_begin(void);
void cyw43_arch_lwip_end(void);
int cyw43_wifi_leave(int *state, int interface);
int cyw43_tcpip_link_status(int *state, int interface);
int cyw43_arch_wifi_connect_async(const char *ssid, const char *password, uint32_t auth);
struct udp_pcb *udp_new_ip_type(int type);
void udp_recv(struct udp_pcb *pcb, void (*callback)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t), void *arg);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *address, u16_t port);
struct pbuf *pbuf_alloc(int layer, u16_t size, int type);
err_t pbuf_take(struct pbuf *p, const void *data, u16_t size);
u16_t pbuf_copy_partial(const struct pbuf *p, void *data, u16_t size, u16_t offset);
void pbuf_free(struct pbuf *p);
bool ip_addr_cmp(const ip_addr_t *a, const ip_addr_t *b);
err_t dns_gethostbyname(const char *host, ip_addr_t *address,
                       void (*callback)(const char *, const ip_addr_t *, void *), void *arg);
