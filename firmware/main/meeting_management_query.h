#pragma once
#include "meeting_config_ble.h"
#include "meeting_wifi_command.h"
#include "meeting_wifi_switch.h"
/* Caller serializes transaction access; publish revalidates the session. */
bool meeting_management_query_publish(meeting_config_ble_inbox *,meeting_pairing_token,const meeting_wifi_switch *,const meeting_wifi_command *);
