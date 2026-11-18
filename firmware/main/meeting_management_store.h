#pragma once
#include "meeting_management_session.h"
#include "nvs.h"
typedef struct {
    nvs_handle_t identity_handle,wifi_handle;
    meeting_mgmt_io identity;
    meeting_wifi_slots_io slots;
    meeting_switch_io transaction_io;
    meeting_wifi_switch transaction;
    meeting_wifi_operation operation;
    meeting_management_session *session;
    uint8_t device_id[16];
} meeting_management_store;
/* Coordinator task only, after networking and BLE have relinquished radio. */
bool meeting_management_store_open(meeting_management_store *store,const uint8_t mac[6]);
/* Requires the management session and any trial radio to be stopped first. */
bool meeting_management_store_close(meeting_management_store *store);
