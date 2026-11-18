#pragma once
#include "meeting_management_credential.h"
/* Public target identifier only; the independent secret authenticates. */
bool meeting_agent_mgmt_device_id(const uint8_t mac[6],uint8_t id[16]);
/* USB-only maintenance: expected is the current generation; retrying an
 * already committed identical update is idempotent. A revoked record can be
 * recovered only through this physical maintenance path. */
meeting_mgmt_result meeting_agent_mgmt_maintain(const meeting_mgmt_io *io,const uint8_t device_id[16],
    const char *secret_hex,uint64_t expected,bool revoke,bool allowed);
/* Maintenance-only: the caller verifies the physical target before allowing
 * installation. Existing different credentials are never replaced. */
meeting_mgmt_result meeting_agent_mgmt_install(const meeting_mgmt_io *io,const uint8_t device_id[16],
                                               const char *secret_hex,bool device_allowed);
