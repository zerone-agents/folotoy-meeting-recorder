#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Storage must atomically replace and durably commit exactly one record.
 * CRC detects accidental corruption; it does not authenticate Flash. */
#define MEETING_MGMT_RECORD_SIZE 64u
#define MEETING_MGMT_ID_SIZE 16u
#define MEETING_MGMT_SECRET_SIZE 32u
typedef enum { MEETING_MGMT_ACTIVE=1, MEETING_MGMT_REVOKED=2 } meeting_mgmt_state;
typedef struct {
    uint64_t generation;
    uint8_t device_id[MEETING_MGMT_ID_SIZE];
    uint8_t secret[MEETING_MGMT_SECRET_SIZE];
    meeting_mgmt_state state;
} meeting_mgmt_record;
typedef struct {
    void *ctx;
    /* 0 present, 1 missing, negative on I/O error. */
    int (*read)(void *ctx,uint8_t out[MEETING_MGMT_RECORD_SIZE]);
    /* An error may still have committed; callers must re-read. */
    bool (*write_sync)(void *ctx,const uint8_t data[MEETING_MGMT_RECORD_SIZE]);
} meeting_mgmt_io;
typedef enum {MEETING_MGMT_OK, MEETING_MGMT_EMPTY, MEETING_MGMT_CONFLICT,
              MEETING_MGMT_ERROR} meeting_mgmt_result;
bool meeting_mgmt_encode(const meeting_mgmt_record *record,uint8_t out[MEETING_MGMT_RECORD_SIZE]);
bool meeting_mgmt_decode(const uint8_t data[MEETING_MGMT_RECORD_SIZE],meeting_mgmt_record *out);
meeting_mgmt_result meeting_mgmt_load(const meeting_mgmt_io *io,meeting_mgmt_record *out);
/* Storage does not authenticate callers or prove that a missing record is a
 * new device: only an authorized USB workflow may create/re-provision it.
 * Serialize with management sessions. expected_generation=0
 * permits initial creation only; rotations and revocations increment exactly
 * once. A revoked identity can be re-provisioned only via the authorized USB
 * maintenance path. On ambiguous write failure, disable management and close/reopen storage before
 * reconciling; a failure never authorizes the new credential. */
meeting_mgmt_result meeting_mgmt_replace(const meeting_mgmt_io *io,uint64_t expected_generation,
                                        const meeting_mgmt_record *next);
void meeting_mgmt_clear(meeting_mgmt_record *record);
