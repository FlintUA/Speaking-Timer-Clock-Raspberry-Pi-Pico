#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "clock_datetime.h"
typedef enum {
    NETWORK_HOME_OFFLINE, NETWORK_HOME_UNCONFIGURED, NETWORK_HOME_SUSPENDED,
    NETWORK_HOME_WAIT_IDLE, NETWORK_HOME_CONNECTING, NETWORK_HOME_ADDRESS,
    NETWORK_HOME_RETRY, NETWORK_HOME_READY
} network_home_state_t;
typedef struct {
    network_home_state_t state;
    uint32_t seconds;
} network_home_status_t;
network_home_status_t network_time_home_status(uint64_t now_ms);
typedef struct {
    bool available, configured, sampled, synced, pending, requesting, retry, failed;
    uint32_t sample_age, sync_age;
    int offset;
} network_reference_status_t;
network_reference_status_t network_time_reference_status(uint64_t now_ms);
void network_time_init(bool radio_ready);
void network_time_configure(const char *ssid, const char *password);
void network_time_request(void);
// Drop stale pending reference data after an explicit RTC edit; hourly sync remains enabled.
void network_time_manual_adjusted(void);
void network_time_status(void);
#ifdef CLOCK_ENABLE_PHONE_WEB
void network_time_suspend_home(bool suspended);
#endif
// Network callbacks only stage time. RTC updates are returned to the idle main loop.
bool network_time_service(uint64_t now_ms, bool idle, bool join_allowed, ds1302_datetime_t *local);
void network_time_applied(bool success);
bool network_time_is_dst_change(void);
