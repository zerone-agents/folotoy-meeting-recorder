#pragma once
#include "meeting_wifi_slots.h"
#include "nvs.h"
/* Caller owns the namespace handle and serializes all accesses; after any
 * failed write it must close/reopen the handle before reading or retrying. */
bool meeting_wifi_slots_nvs_bind(nvs_handle_t *handle,meeting_wifi_slots_io *out);
