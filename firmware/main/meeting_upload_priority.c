#include "meeting_upload_priority.h"
#include <string.h>
enum { NORMAL_MS=25000, STALL_MS=45000, OPPORTUNITY_MS=120000 };
static bool elapsed(uint64_t now,uint64_t start,uint64_t limit){return now>=start&&now-start>=limit;}
void meeting_upload_priority_reset(meeting_upload_priority *s,uint64_t now){
    if(!s)return;
    memset(s,0,sizeof(*s));s->initialized=true;s->last_now=now;s->ordinary_since=now;
}
void meeting_upload_priority_window_end(meeting_upload_priority *s,uint64_t now,bool drain_failed){
    if(!s)return;
    if(!s->initialized){meeting_upload_priority_reset(s,now);return;}
    if(now<s->last_now){meeting_upload_priority_reset(s,now);s->stalled=true;return;}
    s->ordinary_since=now;s->last_now=now;s->chance=false;s->previous_active=false;
    if(drain_failed)s->stalled=true;
}
meeting_priority_decision meeting_upload_priority_tick(meeting_upload_priority *s,uint64_t now,const meeting_activity_snapshot *a){
    meeting_priority_decision d={0};if(!s||!a)return d;
    if(!s->initialized){meeting_upload_priority_reset(s,now);}
    if(now<s->last_now){meeting_upload_priority_reset(s,now);s->stalled=true;return d;}
    s->last_now=now;
    if(s->baseline&&(a->generation<s->generation||a->progress_serial<s->serial||a->failure_serial<s->failure_serial)){d.request_window=elapsed(now,s->ordinary_since,NORMAL_MS);return d;}
    bool active=a->phase==MEETING_ACTIVITY_PREPARING||a->phase==MEETING_ACTIVITY_UPLOADING;
    bool progressed=s->baseline&&a->progress_serial>s->serial;
    bool failed=s->baseline?a->failure_serial>s->failure_serial:
        a->failure_serial>0&&a->progress_serial<=a->failure_progress_serial;
    s->baseline=true;s->serial=a->progress_serial;s->generation=a->generation;s->failure_serial=a->failure_serial;
    if(failed)s->stalled=true;
    if(s->previous_active&&!active&&(a->phase==MEETING_ACTIVITY_FAILED||a->phase==MEETING_ACTIVITY_PAUSED))s->stalled=true;
    if(active&&!s->chance&&!s->stalled){s->chance=true;s->chance_since=now;s->last_progress=now;}
    if(progressed&&(!failed||a->progress_serial>a->failure_progress_serial)){s->stalled=false;s->last_progress=now;if(active&&!s->chance){s->chance=true;s->chance_since=now;}}
    if(active&&s->chance&&!s->stalled&&elapsed(now,s->last_progress,STALL_MS))s->stalled=true;
    s->previous_active=active;
    if(active&&s->chance&&!s->stalled){d.upload_priority=true;d.request_window=elapsed(now,s->chance_since,OPPORTUNITY_MS);}
    else d.request_window=elapsed(now,s->ordinary_since,NORMAL_MS);
    return d;
}
