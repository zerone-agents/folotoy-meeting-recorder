#pragma once
#include "meeting_config_ble.h"
#include "meeting_wifi_command.h"
#include "meeting_wifi_operation.h"
/* Task context only. Caller serializes transaction/storage operations, must
 * stop BLE before starting the prepared Wi-Fi trial, and must treat an
 * unacknowledged durable prepare as queryable by the same operation ID. */
bool meeting_management_dispatch_switch(meeting_config_ble_inbox *,meeting_pairing_token,meeting_wifi_switch *,const meeting_wifi_slots_io *,meeting_wifi_operation *,const meeting_wifi_command *,bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *);
