#pragma once
#include "meeting_wifi_switch.h"
#include "nvs.h"
/* Handle lifetime and exclusive access belong to caller. Not an atomic
 * network configuration commit; only stores the transaction status. */
bool meeting_wifi_switch_nvs_bind(nvs_handle_t *handle, meeting_switch_io *out);
