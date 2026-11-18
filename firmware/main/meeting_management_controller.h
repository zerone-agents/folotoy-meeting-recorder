#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Called only from the core task; legacy BLE must already be stopped. */
bool meeting_management_controller_init(const uint8_t mac[6]);
void meeting_management_controller_tick(uint64_t now_ms,bool idle);
/* Called before a recording request; false means a resource is still held. */
bool meeting_management_controller_preempt(uint64_t now_ms);
bool meeting_management_controller_owns_radio(void);
bool meeting_management_controller_window_active(void);
bool meeting_management_controller_window_connected(void);
bool meeting_management_controller_window_authenticated(void);
bool meeting_management_controller_trial_active(void);

bool meeting_management_controller_request(void);
void meeting_management_controller_cancel(void);
bool meeting_management_controller_toggle(void);
