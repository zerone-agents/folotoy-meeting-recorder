#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The caller serializes ticks and callbacks under one lock and performs
 * actions only after the preceding action has completed. This isolated model
 * does not itself stop tasks, bind identities, or implement BLE. */
typedef enum {
    MEETING_WINDOW_NONE, MEETING_WINDOW_STOP_UPLOAD, MEETING_WINDOW_STOP_STA,
    MEETING_WINDOW_START_BLE, MEETING_WINDOW_STOP_BLE, MEETING_WINDOW_RESUME_UPLOAD,
    MEETING_WINDOW_CANCEL_WINDOW, MEETING_WINDOW_RECORD_READY, MEETING_WINDOW_FAIL_CLOSED
} meeting_window_action;
typedef enum {
    MEETING_WINDOW_UPLOAD_STOPPED, MEETING_WINDOW_STA_STOPPED,
    MEETING_WINDOW_BLE_STARTED, MEETING_WINDOW_BLE_STOPPED, MEETING_WINDOW_UPLOAD_RESUMED,
    MEETING_WINDOW_ACTION_FAILED
} meeting_window_event_type;
typedef enum {
    WINDOW_ONLINE, WINDOW_DRAIN, WINDOW_DRAIN_CANCELLED, WINDOW_STOPPING_STA,
    WINDOW_STARTING_BLE, WINDOW_BLE, WINDOW_STOPPING_BLE, WINDOW_RECORDING, WINDOW_RESUMING,
    WINDOW_FAULT
} meeting_window_state;
typedef struct {
    meeting_window_state state;
    uint64_t since;
    uint32_t generation;
    bool record_pending;
    bool cancel_window;
} meeting_idle_window;
void meeting_window_init(meeting_idle_window *w, uint64_t now_ms);
meeting_window_action meeting_window_tick(meeting_idle_window *w, uint64_t now_ms, bool recording_requested);
meeting_window_action meeting_window_event(meeting_idle_window *w, uint32_t generation, meeting_window_event_type e, uint64_t now_ms);
meeting_window_action meeting_window_record_finished(meeting_idle_window *w, uint32_t generation);
meeting_window_action meeting_window_cancel_record(meeting_idle_window *w, uint32_t generation);
