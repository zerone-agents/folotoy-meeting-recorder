#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MEETING_ACTIVITY_IDLE, MEETING_ACTIVITY_PREPARING, MEETING_ACTIVITY_UPLOADING,
    MEETING_ACTIVITY_PAUSED, MEETING_ACTIVITY_FAILED, MEETING_ACTIVITY_CONFIRMED
} meeting_activity_phase;
typedef struct {
    uint64_t generation;
    meeting_activity_phase phase;
    char meeting_id[37];
    uint8_t digest[32];
    uint32_t confirmed_next;
    uint64_t progress_serial;
    uint64_t failure_serial;
    uint64_t failure_progress_serial;
} meeting_activity_snapshot;
uint64_t meeting_upload_activity_begin(void);
bool meeting_upload_activity_bind(uint64_t generation,const char *meeting_id,const uint8_t digest[32]);
bool meeting_upload_activity_progress(uint64_t generation,uint32_t confirmed_next);
bool meeting_upload_activity_finish(uint64_t generation,meeting_activity_phase phase);
/* Only after the worker has durably recorded cleanup intent and removed the
 * confirmed local file. Never call for an unconfirmed recording. */
bool meeting_upload_activity_retire(uint64_t generation);
meeting_activity_snapshot meeting_upload_activity_snapshot(void);
