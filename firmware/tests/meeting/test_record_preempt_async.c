#include "meeting_record_preempt_async.h"
#include <assert.h>
int main(void){
 meeting_record_preempt_async p;meeting_record_preempt_init(&p);uint32_t first,second;bool ok;
 assert(meeting_record_preempt_begin(&p,&first));
 assert(!meeting_record_preempt_begin(&p,&second));
 assert(!meeting_record_preempt_take(&p,first,&ok));
 assert(meeting_record_preempt_finish(&p,first,true));
 /* Caller timed out and no longer accepts the first result. */
 assert(meeting_record_preempt_begin(&p,&second)&&second!=first);
 assert(!meeting_record_preempt_take(&p,first,&ok));
 assert(!meeting_record_preempt_finish(&p,first,true));
 assert(meeting_record_preempt_finish(&p,second,false));
 assert(meeting_record_preempt_take(&p,second,&ok)&&!ok);
 return 0;
}
