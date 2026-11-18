#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MEETING_RECORD_WAIT_MS 3000

static inline bool meeting_idle_before_deadline(int64_t now, int64_t deadline)
{
    return now < deadline;
}
bool meeting_idle_preempt_before_deadline(int64_t deadline,int64_t (*clock_ms)(void *),
                                        bool (*preempt)(void *),void *ctx);

/* All calls, including event delivery, require the same external lock. */
typedef enum {
    MEETING_IDLE_BLE_STOPPED,
    MEETING_IDLE_NETWORK_STOPPED,
    MEETING_IDLE_CAPTURE_STARTED
} meeting_idle_event;
typedef enum {
    MEETING_IDLE_RESOURCE_BLE = 1,
    MEETING_IDLE_RESOURCE_NETWORK = 2
} meeting_idle_resource;

typedef struct {
    uint32_t generation;
    bool active;
    bool ble_stopped;
    bool network_stopped;
    bool capture_started;
    uint32_t draining_generation;
    unsigned draining_resources;
} meeting_idle_coordinator;

uint32_t meeting_idle_request_record(meeting_idle_coordinator *c);
bool meeting_idle_record_event(meeting_idle_coordinator *c, uint32_t generation,
                               meeting_idle_event event);
void meeting_idle_record_timeout(meeting_idle_coordinator *c, uint32_t generation);
bool meeting_idle_record_finish(meeting_idle_coordinator *c, uint32_t generation);
bool meeting_idle_release_started(meeting_idle_coordinator *c, uint32_t generation,
                                  meeting_idle_resource resource);
bool meeting_idle_release_finished(meeting_idle_coordinator *c, uint32_t generation,
                                   meeting_idle_resource resource);
