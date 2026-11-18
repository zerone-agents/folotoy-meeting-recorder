#include "meeting_upload_activity.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static void *reader(void *unused) {
    (void)unused;
    for (int i=0;i<100000;i++) {
        meeting_activity_snapshot s=meeting_upload_activity_snapshot();
        if (s.meeting_id[0]) assert((s.meeting_id[0]=='A' && s.digest[0]==1) || (s.meeting_id[0]=='B' && s.digest[0]==2));
    }
    return NULL;
}
int main(void) {
    uint8_t a[32]={1},b[32]={2};
    uint64_t g=meeting_upload_activity_begin();
    assert(meeting_upload_activity_bind(g,"A",a));
    assert(!meeting_upload_activity_retire(g));
    assert(meeting_upload_activity_progress(g,1));
    uint64_t serial=meeting_upload_activity_snapshot().progress_serial;
    assert(serial==1);
    assert(meeting_upload_activity_progress(g,1));
    assert(meeting_upload_activity_snapshot().progress_serial==serial);
    uint64_t old=g;g=meeting_upload_activity_begin();
    assert(!meeting_upload_activity_retire(old));
    assert(meeting_upload_activity_bind(g,"A",a));
    assert(!meeting_upload_activity_progress(old,2));
    assert(!meeting_upload_activity_finish(old,MEETING_ACTIVITY_CONFIRMED));
    assert(meeting_upload_activity_progress(g,1));
    assert(meeting_upload_activity_snapshot().progress_serial==serial);
    assert(meeting_upload_activity_progress(g,2));
    assert(meeting_upload_activity_snapshot().progress_serial==++serial);
    assert(meeting_upload_activity_finish(g,MEETING_ACTIVITY_CONFIRMED));
    assert(meeting_upload_activity_snapshot().progress_serial==++serial);
    g=meeting_upload_activity_begin();assert(meeting_upload_activity_bind(g,"B",b));
    assert(meeting_upload_activity_progress(g,1));
    assert(meeting_upload_activity_snapshot().progress_serial==++serial);
    pthread_t thread;assert(!pthread_create(&thread,NULL,reader,NULL));
    for(int i=0;i<10000;i++){g=meeting_upload_activity_begin();assert(meeting_upload_activity_bind(g,i%2?"A":"B",i%2?a:b));}
    assert(!pthread_join(thread,NULL));
    /* Completed and locally retired files must not exhaust the bounded table
     * after a few dozen meetings in one uninterrupted device run. */
    for(int i=0;i<40;i++){
        char id[20];snprintf(id,sizeof(id),"done-%d",i);
        g=meeting_upload_activity_begin();assert(meeting_upload_activity_bind(g,id,a));
        uint64_t before=meeting_upload_activity_snapshot().progress_serial;
        assert(meeting_upload_activity_progress(g,1));
        assert(meeting_upload_activity_snapshot().progress_serial==before+1);
        assert(meeting_upload_activity_finish(g,MEETING_ACTIVITY_CONFIRMED));
        assert(meeting_upload_activity_retire(g));
    }
    puts("upload activity: ok");
}
