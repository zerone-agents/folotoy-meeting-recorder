#pragma once
#include "meeting_pairing_ble.h"
#include "meeting_wifi_switch.h"
#include "meeting_wifi_slots.h"
/* 24 = acknowledged (including already absent), 22 = different operation,
 * 23 = unconfirmed, unauthorized, or recovery required. */
uint8_t meeting_management_ack(meeting_pairing_token token,meeting_wifi_switch *txn,
                               const meeting_wifi_slots_io *slots,const uint8_t id[16]);
