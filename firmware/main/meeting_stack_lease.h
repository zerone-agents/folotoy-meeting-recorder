#pragma once
#include <stdbool.h>
#include <stdint.h>

/* The caller zero-initializes the lease once and serializes every operation
 * with a single lock. A token protects ownership transitions, not raw pointer
 * accesses: stop the task and drain all outstanding references before release.
 * Never reinitialize a live lease or reuse its storage across a device reset. */
typedef enum {
    MEETING_STACK_IDLE = 0,
    MEETING_STACK_AUDIO = 1,
    MEETING_STACK_TLS = 2
} meeting_stack_owner;
enum { MEETING_STACK_MAX_REFERENCES = 16 };
typedef struct {
    uint32_t generation;
    meeting_stack_owner owner;
    uint32_t references;
    uint32_t next_reference;
    uint32_t active_references[MEETING_STACK_MAX_REFERENCES];
    bool closing;
} meeting_stack_lease;

/* Zero means unavailable, invalid, or generation exhausted. */
uint32_t meeting_stack_acquire(meeting_stack_lease *lease, meeting_stack_owner owner);
/* Begin shutdown before waiting for all registered users to leave. All calls
 * must share the same external lock, including reference acquisition. */
/* Each successful entry returns a distinct nonzero reference token. A leave
 * consumes exactly that token; duplicate and late leaves cannot drain other
 * users. Exhaustion or the fixed reference limit fails closed. */
uint32_t meeting_stack_enter(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token);
bool meeting_stack_leave(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token, uint32_t reference);
bool meeting_stack_close(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token);
bool meeting_stack_release(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token);
