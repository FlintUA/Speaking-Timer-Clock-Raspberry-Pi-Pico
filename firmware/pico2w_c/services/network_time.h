#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "clock_datetime.h"
void network_time_init(bool radio_ready);
void network_time_configure(const char *ssid, const char *password);
void network_time_request(void);
void network_time_status(void);
// Network callbacks only stage time. RTC updates are returned to the idle main loop.
bool network_time_service(uint64_t now_ms, bool idle, ds1302_datetime_t *local);
void network_time_applied(bool success);
bool network_time_is_dst_change(void);
