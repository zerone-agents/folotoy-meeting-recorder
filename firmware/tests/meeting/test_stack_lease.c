#include "meeting_stack_lease.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

int main(void)
{
    meeting_stack_lease lease = {0};
    assert(!meeting_stack_acquire(NULL, MEETING_STACK_AUDIO));
    assert(!meeting_stack_acquire(&lease, MEETING_STACK_IDLE));
    uint32_t audio = meeting_stack_acquire(&lease, MEETING_STACK_AUDIO);
    assert(audio && !meeting_stack_acquire(&lease, MEETING_STACK_TLS));
    assert(!meeting_stack_release(&lease, MEETING_STACK_TLS, audio));
    uint32_t first = meeting_stack_enter(&lease, MEETING_STACK_AUDIO, audio);
    uint32_t second = meeting_stack_enter(&lease, MEETING_STACK_AUDIO, audio);
    assert(first && second && first != second);
    assert(!meeting_stack_release(&lease, MEETING_STACK_AUDIO, audio));
    assert(meeting_stack_close(&lease, MEETING_STACK_AUDIO, audio));
    assert(!meeting_stack_enter(&lease, MEETING_STACK_AUDIO, audio));
    assert(meeting_stack_leave(&lease, MEETING_STACK_AUDIO, audio, first));
    assert(!meeting_stack_leave(&lease, MEETING_STACK_AUDIO, audio, first));
    assert(!meeting_stack_release(&lease, MEETING_STACK_AUDIO, audio));
    assert(meeting_stack_leave(&lease, MEETING_STACK_AUDIO, audio, second));
    assert(!meeting_stack_leave(&lease, MEETING_STACK_AUDIO, audio, second));
    assert(meeting_stack_release(&lease, MEETING_STACK_AUDIO, audio));

    uint32_t tls = meeting_stack_acquire(&lease, MEETING_STACK_TLS);
    assert(tls && tls != audio && !meeting_stack_acquire(&lease, MEETING_STACK_AUDIO));
    assert(!meeting_stack_release(&lease, MEETING_STACK_TLS, audio));
    assert(!meeting_stack_acquire(&lease, MEETING_STACK_AUDIO));
    assert(!meeting_stack_close(&lease, MEETING_STACK_TLS, audio));
    assert(meeting_stack_close(&lease, MEETING_STACK_TLS, tls));
    assert(meeting_stack_release(&lease, MEETING_STACK_TLS, tls));
    uint32_t next_audio = meeting_stack_acquire(&lease, MEETING_STACK_AUDIO);
    assert(next_audio && next_audio != audio);
    assert(!meeting_stack_release(&lease, MEETING_STACK_AUDIO, audio));
    assert(meeting_stack_close(&lease, MEETING_STACK_AUDIO, next_audio));
    assert(meeting_stack_release(&lease, MEETING_STACK_AUDIO, next_audio));

    meeting_stack_lease bounded = {0};
    uint32_t owner = meeting_stack_acquire(&bounded, MEETING_STACK_TLS);
    uint32_t refs[MEETING_STACK_MAX_REFERENCES];
    for (unsigned i = 0; i < MEETING_STACK_MAX_REFERENCES; ++i) {
        refs[i] = meeting_stack_enter(&bounded, MEETING_STACK_TLS, owner);
        assert(refs[i]);
    }
    assert(!meeting_stack_enter(&bounded, MEETING_STACK_TLS, owner));
    assert(meeting_stack_leave(&bounded, MEETING_STACK_TLS, owner, refs[0]));
    uint32_t replacement = meeting_stack_enter(&bounded, MEETING_STACK_TLS, owner);
    assert(replacement && replacement != refs[0]);
    assert(!meeting_stack_leave(&bounded, MEETING_STACK_TLS, owner, refs[0]));
    assert(meeting_stack_close(&bounded, MEETING_STACK_TLS, owner));
    assert(!meeting_stack_release(&bounded, MEETING_STACK_TLS, owner));
    for (unsigned i = 1; i < MEETING_STACK_MAX_REFERENCES; ++i)
        assert(meeting_stack_leave(&bounded, MEETING_STACK_TLS, owner, refs[i]));
    assert(meeting_stack_leave(&bounded, MEETING_STACK_TLS, owner, replacement));
    assert(meeting_stack_release(&bounded, MEETING_STACK_TLS, owner));
    owner = meeting_stack_acquire(&bounded, MEETING_STACK_TLS);
    assert(owner && !meeting_stack_leave(&bounded, MEETING_STACK_TLS, owner, replacement));
    bounded.next_reference = UINT32_MAX;
    assert(!meeting_stack_enter(&bounded, MEETING_STACK_TLS, owner));
    assert(meeting_stack_close(&bounded, MEETING_STACK_TLS, owner));
    assert(meeting_stack_release(&bounded, MEETING_STACK_TLS, owner));

    lease.generation = UINT32_MAX - 1;
    uint32_t last = meeting_stack_acquire(&lease, MEETING_STACK_TLS);
    assert(last == UINT32_MAX && meeting_stack_close(&lease, MEETING_STACK_TLS, last));
    assert(meeting_stack_release(&lease, MEETING_STACK_TLS, last));
    assert(!meeting_stack_acquire(&lease, MEETING_STACK_AUDIO));
    assert(lease.owner == MEETING_STACK_IDLE && lease.generation == UINT32_MAX);
    return 0;
}
