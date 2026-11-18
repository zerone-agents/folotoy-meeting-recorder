#pragma once
#include "meeting_wifi_slots.h"
#include "meeting_wifi_switch.h"
typedef struct { const meeting_wifi_slots_io *io; uint64_t generation; } meeting_wifi_slot_proof;
/* Caller must serialize pointer changes and transaction publication. */
bool meeting_wifi_slot_commit_proof(void *ctx,const meeting_switch_request *request);
bool meeting_wifi_slot_rollback_proof(void *ctx,const meeting_switch_request *request);
