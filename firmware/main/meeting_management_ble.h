#pragma once
#include "meeting_management_credential.h"
#include "meeting_pairing_ble.h"
/* Caller serializes credential updates, session start, and service shutdown.
 * expected_id must come from trusted local provisioning, not a BLE request. */
esp_err_t meeting_management_ble_start(const meeting_mgmt_io *io,const uint8_t expected_id[16],
                                       meeting_pairing_handler handler,meeting_pairing_reset reset,void *ctx);
