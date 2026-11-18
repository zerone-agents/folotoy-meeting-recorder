#include "meeting_idle_coordinator.h"
#include <limits.h>

bool meeting_idle_preempt_before_deadline(int64_t deadline,int64_t (*clock_ms)(void *),
                                        bool (*preempt)(void *),void *ctx)
{
    return clock_ms&&preempt&&meeting_idle_before_deadline(clock_ms(ctx),deadline)&&
           preempt(ctx)&&meeting_idle_before_deadline(clock_ms(ctx),deadline);
}

uint32_t meeting_idle_request_record(meeting_idle_coordinator *c)
{
    if (!c || c->active || c->draining_resources || c->generation == UINT32_MAX) return 0;
    ++c->generation;
    c->active = true;
    c->ble_stopped = false;
    c->network_stopped = false;
    c->capture_started = false;
    return c->generation;
}

bool meeting_idle_record_event(meeting_idle_coordinator *c, uint32_t generation,
                               meeting_idle_event event)
{
    if (!c || !c->active || !generation || generation != c->generation) return false;
    switch (event) {
    case MEETING_IDLE_BLE_STOPPED:
        if (c->ble_stopped || c->capture_started ||
            (c->draining_resources & MEETING_IDLE_RESOURCE_BLE)) return false;
        c->ble_stopped = true;
        return true;
    case MEETING_IDLE_NETWORK_STOPPED:
        if (c->network_stopped || c->capture_started ||
            (c->draining_resources & MEETING_IDLE_RESOURCE_NETWORK)) return false;
        c->network_stopped = true;
        return true;
    case MEETING_IDLE_CAPTURE_STARTED:
        if (!c->ble_stopped || !c->network_stopped || c->draining_resources || c->capture_started) return false;
        c->capture_started = true;
        return true;
    }
    return false;
}

bool meeting_idle_release_started(meeting_idle_coordinator *c, uint32_t generation,
                                  meeting_idle_resource resource)
{
    if (!c || !c->active || c->capture_started || c->generation != generation ||
        (resource != MEETING_IDLE_RESOURCE_BLE && resource != MEETING_IDLE_RESOURCE_NETWORK) ||
        (resource == MEETING_IDLE_RESOURCE_BLE && c->ble_stopped) ||
        (resource == MEETING_IDLE_RESOURCE_NETWORK && c->network_stopped) ||
        (c->draining_resources & (unsigned)resource)) return false;
    c->draining_generation = generation;
    c->draining_resources |= (unsigned)resource;
    return true;
}

bool meeting_idle_release_finished(meeting_idle_coordinator *c, uint32_t generation,
                                   meeting_idle_resource resource)
{
    if (!c || c->draining_generation != generation ||
        (resource != MEETING_IDLE_RESOURCE_BLE && resource != MEETING_IDLE_RESOURCE_NETWORK) ||
        !(c->draining_resources & (unsigned)resource)) return false;
    c->draining_resources &= ~(unsigned)resource;
    if (!c->draining_resources) c->draining_generation = 0;
    return true;
}

void meeting_idle_record_timeout(meeting_idle_coordinator *c, uint32_t generation)
{
    if (c && c->active && c->generation == generation && !c->capture_started)
        c->active = false;
}

bool meeting_idle_record_finish(meeting_idle_coordinator *c, uint32_t generation)
{
    if (!c || !c->active || !c->capture_started || !generation ||
        c->generation != generation || c->draining_resources) return false;
    c->active = false;
    c->capture_started = false;
    return true;
}
