#include "meeting_idle_window.h"

enum { UPLOAD_OPPORTUNITY_MS = 20000, DRAIN_TIMEOUT_MS = 3000, BLE_WINDOW_MS = 5000 };
static bool elapsed(uint64_t now, uint64_t since, uint64_t duration)
{ return now >= since && now - since >= duration; }
void meeting_window_init(meeting_idle_window *w, uint64_t now_ms)
{ if (w) *w = (meeting_idle_window){.state=WINDOW_ONLINE,.since=now_ms}; }
meeting_window_action meeting_window_tick(meeting_idle_window *w, uint64_t now, bool record)
{
    if (!w) return MEETING_WINDOW_NONE;
    if (record) w->record_pending = true;
    if (w->state == WINDOW_ONLINE && (w->record_pending || elapsed(now,w->since,UPLOAD_OPPORTUNITY_MS))) {
        if (w->generation == UINT32_MAX) return MEETING_WINDOW_NONE;
        ++w->generation;
        w->cancel_window=false;
        w->state=WINDOW_DRAIN; w->since=now; return MEETING_WINDOW_STOP_UPLOAD;
    }
    if (w->state == WINDOW_DRAIN && elapsed(now,w->since,DRAIN_TIMEOUT_MS)) {
        w->state=WINDOW_DRAIN_CANCELLED; return MEETING_WINDOW_CANCEL_WINDOW;
    }
    if (w->state == WINDOW_BLE && (w->record_pending || elapsed(now,w->since,BLE_WINDOW_MS))) {
        w->state=WINDOW_STOPPING_BLE; return MEETING_WINDOW_STOP_BLE;
    }
    return MEETING_WINDOW_NONE;
}
meeting_window_action meeting_window_event(meeting_idle_window *w, uint32_t generation, meeting_window_event_type e, uint64_t now)
{
    if (!w || !generation || generation != w->generation) return MEETING_WINDOW_NONE;
    if (e == MEETING_WINDOW_ACTION_FAILED && w->state != WINDOW_ONLINE &&
        w->state != WINDOW_RECORDING && w->state != WINDOW_FAULT) {
        w->state=WINDOW_FAULT; return MEETING_WINDOW_FAIL_CLOSED;
    }
    if (e == MEETING_WINDOW_UPLOAD_STOPPED && w->state == WINDOW_DRAIN_CANCELLED) {
        if (w->record_pending) { w->state=WINDOW_STOPPING_STA; return MEETING_WINDOW_STOP_STA; }
        w->state=WINDOW_RESUMING; return MEETING_WINDOW_RESUME_UPLOAD;
    }
    if (e == MEETING_WINDOW_UPLOAD_STOPPED && w->state == WINDOW_DRAIN) {
        w->state=WINDOW_STOPPING_STA; return MEETING_WINDOW_STOP_STA;
    }
    if (e == MEETING_WINDOW_STA_STOPPED && w->state == WINDOW_STOPPING_STA) {
        if (w->record_pending) { w->state=WINDOW_RECORDING; return MEETING_WINDOW_RECORD_READY; }
        if (w->cancel_window) { w->state=WINDOW_RESUMING; return MEETING_WINDOW_RESUME_UPLOAD; }
        w->state=WINDOW_STARTING_BLE; return MEETING_WINDOW_START_BLE;
    }
    if (e == MEETING_WINDOW_BLE_STARTED && w->state == WINDOW_STARTING_BLE) {
        if (w->record_pending || w->cancel_window) { w->state=WINDOW_STOPPING_BLE; return MEETING_WINDOW_STOP_BLE; }
        w->state=WINDOW_BLE; w->since=now; return MEETING_WINDOW_NONE;
    }
    if (e == MEETING_WINDOW_BLE_STOPPED && w->state == WINDOW_STOPPING_BLE) {
        if (w->record_pending) { w->state=WINDOW_RECORDING; return MEETING_WINDOW_RECORD_READY; }
        w->state=WINDOW_RESUMING; return MEETING_WINDOW_RESUME_UPLOAD;
    }
    if (e == MEETING_WINDOW_UPLOAD_RESUMED && w->state == WINDOW_RESUMING) {
        if (w->record_pending) {
            if (w->generation == UINT32_MAX) return MEETING_WINDOW_NONE;
            ++w->generation; w->state=WINDOW_DRAIN; w->since=now;
            return MEETING_WINDOW_STOP_UPLOAD;
        }
        w->state=WINDOW_ONLINE; w->since=now; return MEETING_WINDOW_NONE;
    }
    return MEETING_WINDOW_NONE;
}

meeting_window_action meeting_window_cancel_record(meeting_idle_window *w, uint32_t generation)
{
    if (!w || !generation || generation != w->generation || !w->record_pending || w->state == WINDOW_RECORDING)
        return MEETING_WINDOW_NONE;
    w->record_pending=false;
    w->cancel_window=true;
    if (w->state == WINDOW_DRAIN) {
        w->state=WINDOW_DRAIN_CANCELLED;
        return MEETING_WINDOW_CANCEL_WINDOW;
    }
    return MEETING_WINDOW_NONE;
}

meeting_window_action meeting_window_record_finished(meeting_idle_window *w, uint32_t generation)
{
    if (!w || !generation || generation != w->generation || w->state != WINDOW_RECORDING)
        return MEETING_WINDOW_NONE;
    w->record_pending=false;
    w->state=WINDOW_RESUMING;
    return MEETING_WINDOW_RESUME_UPLOAD;
}
