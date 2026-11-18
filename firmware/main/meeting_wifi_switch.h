#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Isolated transaction model: caller serializes access and must close/reopen
 * storage before reconcile after any uncertain write. This does not implement
 * candidate credentials, atomic configuration commit, time or authentication. */
typedef enum { MEETING_SWITCH_WAITING=1, MEETING_SWITCH_TESTING, MEETING_SWITCH_COMPLETE,
               MEETING_SWITCH_ROLLED_BACK, MEETING_SWITCH_COMMITTING } meeting_switch_state;
typedef enum { MEETING_SWITCH_OK, MEETING_SWITCH_CONFLICT, MEETING_SWITCH_INVALID,
               MEETING_SWITCH_STORAGE_ERROR, MEETING_SWITCH_UNKNOWN } meeting_switch_result;
typedef struct { uint8_t id[16], digest[32]; } meeting_switch_request;
typedef struct { meeting_switch_request request; meeting_switch_state state; bool acknowledged; } meeting_switch_record;
typedef struct { void *ctx; bool (*save)(void *, const meeting_switch_record *); int (*load)(void *, meeting_switch_record *); bool (*erase)(void *); } meeting_switch_io;
typedef struct { meeting_switch_io io; meeting_switch_record record, attempted; bool present, uncertain, recovered; } meeting_wifi_switch;
void meeting_wifi_switch_init(meeting_wifi_switch *s, const meeting_switch_io *io);
/* load returns 0 for a durable record, 1 for absent, negative on failure. */
meeting_switch_result meeting_wifi_switch_recover(meeting_wifi_switch *s);
meeting_switch_result meeting_wifi_switch_begin(meeting_wifi_switch *s, const meeting_switch_request *request);
meeting_switch_result meeting_wifi_switch_advance(meeting_wifi_switch *s, const uint8_t id[16], meeting_switch_state next);
/* verify must read the durable commit and bind it to request; callers must
 * hold the same lock across verification and result publication. */
meeting_switch_result meeting_wifi_switch_commit_confirmed(meeting_wifi_switch *s,const uint8_t id[16],
    bool (*verify)(void *,const meeting_switch_request *),void *ctx);
meeting_switch_result meeting_wifi_switch_rollback_confirmed(meeting_wifi_switch *s,const uint8_t id[16],
    bool (*verify)(void *,const meeting_switch_request *),void *ctx);
meeting_switch_result meeting_wifi_switch_query(const meeting_wifi_switch *s, const uint8_t id[16], meeting_switch_state *out);
/* authenticated_ack_and_cleanup means an authorized Agent acknowledged
 * this result and all required candidate cleanup has been verified. */
meeting_switch_result meeting_wifi_switch_ack(meeting_wifi_switch *s,const uint8_t id[16],bool authenticated_ack_and_cleanup);
meeting_switch_result meeting_wifi_switch_reconcile(meeting_wifi_switch *s, const meeting_switch_record *durable);
