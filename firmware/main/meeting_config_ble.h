#pragma once
#include "meeting_pairing_ble.h"
#include "meeting_config_fragments.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
typedef struct {
    meeting_config_fragments fragments;
    meeting_pairing_token token;
    SemaphoreHandle_t lock;
    atomic_bool reset_pending;
    bool delivered;
    bool committed, acknowledged;
    uint8_t result_code;
    atomic_bool ack_latched;
    meeting_pairing_token ack_token;
} meeting_config_ble_inbox;
/* Init/destroy in task context. Destroy only after the BLE transport has
 * stopped and the consumer has exited. The BLE callbacks never wait. */
bool meeting_config_ble_init(meeting_config_ble_inbox *inbox);
bool meeting_config_ble_init_sized(meeting_config_ble_inbox *inbox,size_t capacity);
void meeting_config_ble_destroy(meeting_config_ble_inbox *inbox);
/* Called before starting a new transport, never while callbacks are active. */
void meeting_config_ble_prepare(meeting_config_ble_inbox *inbox);
esp_err_t meeting_config_ble_handle(uint32_t session,const uint8_t *request,size_t length,
                                    uint8_t **response,ssize_t *response_len,void *ctx);
void meeting_config_ble_reset(void *ctx);
/* Task context only; the caller owns and must wipe the copied JSON. A token
 * must be checked again before any use or durable commit. */
bool meeting_config_ble_take(meeting_config_ble_inbox *inbox,uint8_t *out,size_t capacity,
                             size_t *length,meeting_pairing_token *token);
bool meeting_config_ble_publish(meeting_config_ble_inbox *inbox,meeting_pairing_token token);
/* Codes 16..31 represent durable management results; 0,1,2 retain the
 * existing pending/rejected/provisioning semantics. */
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *inbox,meeting_pairing_token token,uint8_t result_code);
bool meeting_config_ble_acknowledged(meeting_config_ble_inbox *inbox,meeting_pairing_token token);
