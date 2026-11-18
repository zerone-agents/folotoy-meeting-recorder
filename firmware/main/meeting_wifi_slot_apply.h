#pragma once
#include "meeting_config.h"
#include "meeting_wifi_slots.h"
/* Caller first verifies the selected slot and owns the config exclusively.
 * This helper does not commit or validate a network connection. */
bool meeting_wifi_slot_apply(meeting_config *config,const meeting_wifi_slot *slot);
/* Failure leaves config unchanged. Slot IO must serialize with writes. */
bool meeting_wifi_slot_restore(const meeting_wifi_slots_io *io,meeting_config *config);
