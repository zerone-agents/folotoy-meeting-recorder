#pragma once
#include "meeting_clock.h"
#include "meeting_config.h"
#include "meeting_time_anchor.h"
#include "meeting_wifi_slots.h"
#include "esp_err.h"
#include <stdbool.h>
typedef enum { MEETING_NETWORK_OFF, MEETING_NETWORK_CONNECTING, MEETING_NETWORK_CLOCK_REQUIRED,
               MEETING_NETWORK_SYNCING, MEETING_NETWORK_IDLE, MEETING_NETWORK_RETRY, MEETING_NETWORK_STOPPING } meeting_network_state;

/* Task-only, single invocation serialized with the core's legacy Wi-Fi
 * switching (including its pending state), after the coordinator has
 * authenticated, confirmed and durably committed config and trusted time.
 * clock_io and its context must remain valid until the device restarts.
 * Reset/reprovision is not supported until revocation is implemented. */
esp_err_t meeting_network_activate(const meeting_config *config,const meeting_clock *clock,const meeting_clock_io *clock_io);
/* Restore a previously authenticated configuration and already committed
 * anchor. HTTP is only used for signed time, never for credentials. */
esp_err_t meeting_network_activate_restored(const meeting_config *config,const meeting_time_anchor *anchor,
                                           const char *http_origin,const meeting_clock_io *clock_io);
bool meeting_network_enabled(void);
/* A timestamp is trusted only while this boot's validated clock remains valid. */
bool meeting_network_trusted_time(int64_t *epoch_seconds);
typedef enum { MEETING_CLOCK_OFF, MEETING_CLOCK_CHECKING, MEETING_CLOCK_TRUSTED } meeting_clock_view;
/* Nonblocking UI snapshot; UTC offset is set only for a trusted clock. */
meeting_clock_view meeting_network_clock_view(int16_t *timezone_minutes);
/* Core/task context only. Cancel in-flight requests, wait for any management
 * owner to release its radio, stop Wi-Fi, wipe the in-memory configuration
 * and clock, then report completion. Completion is not guaranteed if an owner
 * cannot release; callers must not treat a timeout as successful shutdown.
 * The caller must separately serialize this operation with recording and
 * provisioning and arrange management preemption when needed. */
bool meeting_network_stop_request(void);
bool meeting_network_stop_complete(void);
/* Wake the upload supervisor after a recording has been durably saved. */
void meeting_network_record_saved(void);
/* Recording takes precedence: request an asynchronous Wi-Fi shutdown and
 * wait for readiness before allocating audio resources. Release on every
 * completion, cancellation, failure, or timeout. */
void meeting_network_record_request(void);
bool meeting_network_record_ready(void);
void meeting_network_record_release(void);
/* Management windows request the same radio drain independently of capture.
 * A release cannot clear a concurrent recording request. */
void meeting_network_management_request(void);
bool meeting_network_management_ready(void);
void meeting_network_management_release(void);
/* Single coordinator task only, with the management drain held, no
 * recording request, and slot storage serialized against writers. */
bool meeting_network_apply_wifi_slot(const meeting_wifi_slots_io *slots,uint64_t generation,
                                     const uint8_t request_id[16],const uint8_t digest[32]);
meeting_network_state meeting_network_get_state(void);

void meeting_network_target_ssid(char out[33]);
