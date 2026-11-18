#include "meeting_stack_lease.h"
#include <stddef.h>
#include <stdint.h>

static bool valid_owner(meeting_stack_owner owner)
{
    return owner == MEETING_STACK_AUDIO || owner == MEETING_STACK_TLS;
}

static bool matches(const meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token)
{
    return lease && valid_owner(owner) && token && lease->owner == owner &&
           lease->generation == token;
}

uint32_t meeting_stack_acquire(meeting_stack_lease *lease, meeting_stack_owner owner)
{
    if (!lease || !valid_owner(owner) || lease->owner != MEETING_STACK_IDLE || lease->references || lease->closing ||
        lease->generation == UINT32_MAX) return 0;
    lease->owner = owner;
    return ++lease->generation;
}

uint32_t meeting_stack_enter(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token)
{
    if (!matches(lease, owner, token) || lease->closing || lease->next_reference == UINT32_MAX ||
        lease->references >= MEETING_STACK_MAX_REFERENCES) return 0;
    for (unsigned i = 0; i < MEETING_STACK_MAX_REFERENCES; ++i) {
        if (!lease->active_references[i]) {
            uint32_t reference = ++lease->next_reference;
            lease->active_references[i] = reference;
            ++lease->references;
            return reference;
        }
    }
    return 0;
}

bool meeting_stack_leave(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token, uint32_t reference)
{
    if (!matches(lease, owner, token) || !reference || !lease->references) return false;
    for (unsigned i = 0; i < MEETING_STACK_MAX_REFERENCES; ++i) {
        if (lease->active_references[i] == reference) {
            lease->active_references[i] = 0;
            --lease->references;
            return true;
        }
    }
    return false;
}

bool meeting_stack_close(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token)
{
    if (!matches(lease, owner, token)) return false;
    lease->closing = true;
    return true;
}

bool meeting_stack_release(meeting_stack_lease *lease, meeting_stack_owner owner, uint32_t token)
{
    if (!matches(lease, owner, token) || !lease->closing || lease->references) return false;
    lease->owner = MEETING_STACK_IDLE;
    lease->closing = false;
    return true;
}
