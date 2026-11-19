#pragma once
#include "esp_err.h"
#include "sdkconfig.h"
#include <stdbool.h>
/* Task-only interface. Caller serializes all Wi-Fi owners, including sc_wifi;
 * these functions do not persist credentials or start upload. */
esp_err_t meeting_sta_start(const char *ssid, const char *password);
esp_err_t meeting_sta_stop(void);
/* Called by the network supervisor after a bounded backoff. */
esp_err_t meeting_sta_reconnect(void);
bool meeting_sta_has_ip(void);
bool meeting_sta_running(void);
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
/* Test-only, after DHCP; retain the LAN address but remove its gateway. */
esp_err_t meeting_sta_test_no_gateway(void);
#endif
#if CONFIG_MEETING_DEVICE_TIME_PROBE
/* Probe-only, volatile override after station gets an IP. */
esp_err_t meeting_sta_probe_dns(const char *address);
#endif

bool meeting_sta_ap_missing(void);
