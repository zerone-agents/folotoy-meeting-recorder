#pragma once
#include "meeting_config.h"
#include <stdbool.h>
/* Restore only a verified committed slot. Unknown or unfinished state must
 * prevent networking until the transaction recovery path resolves it. */
bool meeting_wifi_boot_restore(meeting_config *config);
