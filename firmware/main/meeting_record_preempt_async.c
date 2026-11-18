#include "meeting_record_preempt_async.h"
#include <limits.h>
void meeting_record_preempt_init(meeting_record_preempt_async *p)
{
    if(!p)return;
    p->next=0;atomic_init(&p->active,0);atomic_init(&p->completed,0);
    atomic_init(&p->busy,false);atomic_init(&p->result,0);
}
bool meeting_record_preempt_begin(meeting_record_preempt_async *p,uint32_t *generation)
{
    if(!p||!generation||atomic_load(&p->busy)||p->next==UINT32_MAX)return false;
    *generation=++p->next;
    atomic_store(&p->result,0);atomic_store(&p->active,*generation);atomic_store(&p->busy,true);
    return true;
}
bool meeting_record_preempt_finish(meeting_record_preempt_async *p,uint32_t generation,bool success)
{
    if(!p||!generation||!atomic_load(&p->busy)||atomic_load(&p->active)!=generation)return false;
    atomic_store(&p->result,success?1:-1);atomic_store(&p->completed,generation);atomic_store(&p->busy,false);return true;
}
bool meeting_record_preempt_take(meeting_record_preempt_async *p,uint32_t generation,bool *success)
{
    if(!p||!generation||!success||atomic_load(&p->busy)||atomic_load(&p->completed)!=generation)return false;
    int result=atomic_load(&p->result);if(!result)return false;*success=result>0;return true;
}
