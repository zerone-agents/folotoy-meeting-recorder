#pragma once
#include "meeting_management_credential.h"
#include "nvs.h"
/* Open a dedicated namespace; serialize all operations and keep the handle
 * alive. Do not erase this namespace as part of Wi-Fi or recording reset. */
bool meeting_mgmt_nvs_bind(nvs_handle_t *handle,meeting_mgmt_io *out);
