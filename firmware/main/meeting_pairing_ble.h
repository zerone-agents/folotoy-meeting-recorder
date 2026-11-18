#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "meeting_pairing_session.h"

/* Task-only, serialized API. The caller must stop the legacy BLE service
 * first, display the generated code privately, enforce physical confirmation,
 * and stop this service after completion or timeout. Requests are decrypted
 * and authenticated by ESP-IDF Security 2 before they reach this handler.
 * Handler runs in the BLE host context and must not block. */
typedef esp_err_t (*meeting_pairing_handler)(uint32_t session,const uint8_t *request,size_t len,
                                            uint8_t **response,ssize_t *response_len,void *ctx);
/* reset runs synchronously in the BLE host context at connect, disconnect and
 * before every Security 2 handshake message. It must discard partial data. */
typedef void (*meeting_pairing_reset)(void *ctx);
esp_err_t meeting_pairing_ble_start(char code[13], meeting_pairing_handler handler,
                                    meeting_pairing_reset reset, void *ctx);
/* Caller has loaded and validated an active, device-specific management
 * credential. Session authentication does not itself authorize operations. */
esp_err_t meeting_pairing_ble_start_management(const uint8_t secret[32], meeting_pairing_handler handler,
                                               meeting_pairing_reset reset, void *ctx);
esp_err_t meeting_pairing_ble_stop(void);
/* Capture only from the authenticated request handler. The token expires on
 * disconnect, any fresh handshake or service stop, even if handles recur. */
bool meeting_pairing_ble_capture(uint32_t session, meeting_pairing_token *token);
bool meeting_pairing_ble_session_valid(meeting_pairing_token token);
/* Transport connection only; it does not imply successful authentication. */
bool meeting_pairing_ble_connected(void);
/* True after an authenticated application request, until session reset. */
bool meeting_pairing_ble_authenticated(void);

#define MEETING_PAIRING_PEER_NAME_SIZE 33
void meeting_pairing_ble_peer_name(char out[MEETING_PAIRING_PEER_NAME_SIZE]);
