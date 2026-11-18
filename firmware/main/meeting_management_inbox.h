#pragma once
#include "meeting_config_ble.h"
#include "meeting_wifi_command.h"
/* Task context only. The consumer must revalidate token immediately before
 * any durable change and clear the command after use. */
bool meeting_management_inbox_take(meeting_config_ble_inbox *inbox,meeting_wifi_command *command,meeting_pairing_token *token);
