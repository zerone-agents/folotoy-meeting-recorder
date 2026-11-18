#pragma once
#include "meeting_wifi_slots.h"
#include "meeting_wifi_switch.h"
#include "meeting_wifi_trial.h"
typedef enum {MEETING_OP_IDLE,MEETING_OP_READY,MEETING_OP_RUNNING,MEETING_OP_COMPLETE,MEETING_OP_WAITING,
              MEETING_OP_ROLLED_BACK,MEETING_OP_STORAGE_ERROR,MEETING_OP_RADIO_ERROR} meeting_wifi_operation_result;
typedef struct {meeting_wifi_switch *txn;const meeting_wifi_slots_io *slots;meeting_wifi_trial trial;meeting_switch_request request;uint64_t generation;meeting_wifi_operation_result state;} meeting_wifi_operation;
/* All callers and storage callbacks are serialized. A returned RADIO_ERROR or
 * STORAGE_ERROR requires reconciliation before network/upload resumes. */
meeting_wifi_operation_result meeting_wifi_operation_prepare(meeting_wifi_operation *,meeting_wifi_switch *,const meeting_wifi_slots_io *,const meeting_wifi_slot *);
/* Only after both stores have been reopened and transaction recovered. */
meeting_wifi_operation_result meeting_wifi_operation_restore_waiting(meeting_wifi_operation *,meeting_wifi_switch *,const meeting_wifi_slots_io *,uint64_t generation);
/* Start only after the caller confirms BLE and prior STA have stopped. */
meeting_wifi_operation_result meeting_wifi_operation_start_trial(meeting_wifi_operation *,const meeting_wifi_trial_io *,uint64_t now);
meeting_wifi_operation_result meeting_wifi_operation_poll(meeting_wifi_operation *,uint64_t now,bool record_requested);
/* Call only after reopening and recovering both stores. Match the new active
 * slot for success, or the old active slot plus candidate for rollback;
 * anything else remains unresolved. Candidate cleanup is separate. */
meeting_wifi_operation_result meeting_wifi_operation_recover_commit(meeting_wifi_switch *,const meeting_wifi_slots_io *,uint64_t generation);
meeting_wifi_operation_result meeting_wifi_operation_recover_testing(meeting_wifi_switch *,const meeting_wifi_slots_io *,uint64_t generation);
meeting_wifi_operation_result meeting_wifi_operation_cleanup_rollback(const meeting_wifi_switch *,const meeting_wifi_slots_io *,uint64_t generation);
