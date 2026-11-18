#include "meeting_upload_priority.h"
#include <assert.h>
#include <stdio.h>
static meeting_activity_snapshot a;
static meeting_priority_decision tick(meeting_upload_priority *s,uint64_t t){return meeting_upload_priority_tick(s,t,&a);}
int main(void){
 meeting_upload_priority s={0};
 assert(!tick(&s,0).request_window);assert(!tick(&s,24999).request_window);assert(tick(&s,25000).request_window);
 meeting_upload_priority_reset(&s,0);a.generation=1;a.phase=MEETING_ACTIVITY_PREPARING;
 assert(tick(&s,1).upload_priority);assert(!tick(&s,44999).request_window);
 assert(!tick(&s,45001).upload_priority);assert(tick(&s,45001).request_window);
 a.generation=2;assert(!tick(&s,46000).upload_priority);
 a.phase=MEETING_ACTIVITY_UPLOADING;a.progress_serial=1;assert(tick(&s,47000).upload_priority);
 assert(tick(&s,120001).request_window);
 meeting_upload_priority_reset(&s,121000);a.generation=3;a.progress_serial=2;
 assert(tick(&s,121001).upload_priority);
 a.phase=MEETING_ACTIVITY_CONFIRMED;a.progress_serial=3;assert(!tick(&s,122000).upload_priority);
 a.generation=4;a.phase=MEETING_ACTIVITY_PREPARING;assert(tick(&s,123000).upload_priority);
 a.phase=MEETING_ACTIVITY_UPLOADING;a.progress_serial=4;assert(tick(&s,200000).upload_priority);
 assert(tick(&s,241001).request_window);
 meeting_upload_priority_reset(&s,300000);a.generation=10;a.phase=MEETING_ACTIVITY_UPLOADING;a.progress_serial=10;
 assert(tick(&s,300001).upload_priority);a.generation=9;a.progress_serial=11;
 assert(!tick(&s,300002).upload_priority); /* stale input is ignored, never extends */
 meeting_upload_priority_reset(&s,400000);a.generation=12;a.progress_serial=12;a.phase=MEETING_ACTIVITY_PREPARING;
 assert(tick(&s,400001).upload_priority);assert(!tick(&s,399999).upload_priority);
 assert(!tick(&s,400000).upload_priority); /* rollback must not grant another grace period */
 a.progress_serial=13;assert(tick(&s,400001).upload_priority);
 meeting_upload_priority_reset(&s,500000);a.generation=13;a.progress_serial=13;a.phase=MEETING_ACTIVITY_PREPARING;
 assert(tick(&s,500001).upload_priority);
 /* Failure and a new attempt can occur between controller ticks. */
 a.generation=14;a.failure_serial=1;a.failure_progress_serial=13;
 assert(!tick(&s,500002).upload_priority);
 a.progress_serial=14;assert(tick(&s,500003).upload_priority);
 meeting_upload_priority_reset(&s,600000);a.generation=15;a.phase=MEETING_ACTIVITY_PREPARING;
 a.progress_serial=14;a.failure_serial=2;a.failure_progress_serial=14;
 assert(!tick(&s,600001).upload_priority); /* failure predates first tick */
 meeting_upload_priority_reset(&s,700000);a.failure_serial=0;a.generation=16;a.progress_serial=20;
 assert(tick(&s,700001).upload_priority);
 meeting_upload_priority_window_end(&s,701000,true); /* drain could not release radio */
 a.generation=17;assert(!tick(&s,701001).upload_priority);
 a.progress_serial=21;assert(tick(&s,701002).upload_priority);
 assert(!tick(&s,746002).upload_priority);
 meeting_upload_priority_window_end(&s,746100,false);a.generation=18;
 assert(!tick(&s,746101).upload_priority); /* a BLE window does not erase stall */
 a.progress_serial=22;assert(tick(&s,746102).upload_priority);
 puts("upload priority: ok");
}
