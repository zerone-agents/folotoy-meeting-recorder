#pragma once
#include "meeting_management_credential.h"
#include "meeting_pairing_session.h"

/* Caller zero-initializes the gate, serializes all operations and closes the NVS handle after a
 * storage error before constructing a new gate. USB authority is supplied
 * only by the local physical provisioning dispatcher, never by a BLE packet. */
typedef enum {MEETING_MGMT_ACTOR_USB, MEETING_MGMT_ACTOR_SESSION} meeting_mgmt_actor;
typedef enum {MEETING_MGMT_GATE_READY, MEETING_MGMT_GATE_EMPTY,
              MEETING_MGMT_GATE_REVOKED, MEETING_MGMT_GATE_UNAVAILABLE} meeting_mgmt_gate_state;
typedef struct {
    meeting_mgmt_io io;
    meeting_pairing_session *sessions;
    meeting_mgmt_record record;
    meeting_mgmt_gate_state state;
} meeting_mgmt_gate;
meeting_mgmt_gate_state meeting_mgmt_gate_init(meeting_mgmt_gate *gate,const meeting_mgmt_io *io,meeting_pairing_session *sessions);
/* Caller must first authenticate the session cryptographically and capture the
 * transport token; this module checks its continued validity, not the proof. */
meeting_mgmt_result meeting_mgmt_gate_update(meeting_mgmt_gate *gate,meeting_mgmt_actor actor,
                                            const meeting_pairing_token *token,const meeting_mgmt_record *next);
void meeting_mgmt_gate_clear(meeting_mgmt_gate *gate);
