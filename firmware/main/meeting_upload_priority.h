#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "meeting_upload_activity.h"
typedef struct {
    bool initialized, baseline, chance, stalled, previous_active;
    uint64_t last_now, ordinary_since, chance_since, last_progress, serial, generation, failure_serial;
} meeting_upload_priority;
typedef struct { bool upload_priority; bool request_window; } meeting_priority_decision;
void meeting_upload_priority_reset(meeting_upload_priority *state,uint64_t now_ms);
/* Called when a management window ends, or recording takes priority. A
 * failed drain must not grant repeated fresh preparation allowances. */
void meeting_upload_priority_window_end(meeting_upload_priority *state,uint64_t now_ms,bool drain_failed);
meeting_priority_decision meeting_upload_priority_tick(meeting_upload_priority *state,uint64_t now_ms,const meeting_activity_snapshot *activity);
