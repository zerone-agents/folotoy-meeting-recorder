#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
/* begin/take are serialized by the core task; finish is called by the
 * existing transfer task. A timed-out caller discards its generation. */
typedef struct {
    uint32_t next;
    atomic_uint active,completed;
    atomic_bool busy;
    atomic_int result;
} meeting_record_preempt_async;
void meeting_record_preempt_init(meeting_record_preempt_async *p);
bool meeting_record_preempt_begin(meeting_record_preempt_async *p,uint32_t *generation);
bool meeting_record_preempt_finish(meeting_record_preempt_async *p,uint32_t generation,bool success);
bool meeting_record_preempt_take(meeting_record_preempt_async *p,uint32_t generation,bool *success);
