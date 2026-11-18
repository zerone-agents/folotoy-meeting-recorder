#include "meeting_wifi_switch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { meeting_switch_record record; int present; int fail; } fake_store;
static bool save(void *ctx, const meeting_switch_record *record) {
    fake_store *f=ctx; if(f->fail)return false; f->record=*record; f->present=1; return true;
}
static int load(void *ctx, meeting_switch_record *record) {
    fake_store *f=ctx; if(f->fail)return -1; if(!f->present)return 1; *record=f->record; return 0;
}
static bool erase(void *ctx){fake_store *f=ctx;if(f->fail)return false;f->present=0;return true;}
static bool proof(void *ctx,const meeting_switch_request *r) {
    (void)r;return *(bool *)ctx;
}
int main(void) {
    fake_store f={0}; meeting_wifi_switch s; meeting_switch_io io={&f,save,load,erase};
    meeting_switch_request a={.id={1},.digest={2}}, b=a;
    meeting_wifi_switch_init(&s,&io);
    assert(meeting_wifi_switch_begin(&s,&a)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_recover(&s)==MEETING_SWITCH_OK);
    meeting_switch_state state=0; uint8_t unknown[16]={9};
    assert(meeting_wifi_switch_query(&s,unknown,&state)==MEETING_SWITCH_UNKNOWN);
    assert(meeting_wifi_switch_begin(&s,&a)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_begin(&s,&a)==MEETING_SWITCH_OK);
    b.digest[0]=3;
    assert(meeting_wifi_switch_begin(&s,&b)==MEETING_SWITCH_CONFLICT);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_TESTING)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_query(&s,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_TESTING);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_COMPLETE)==MEETING_SWITCH_INVALID);
    f.fail=1;
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_COMMITTING)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_query(&s,a.id,&state)==MEETING_SWITCH_STORAGE_ERROR);
    assert(s.record.state==MEETING_SWITCH_TESTING);
    f.fail=0;
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_WAITING)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_reconcile(&s,&f.record)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_WAITING)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_COMPLETE)==MEETING_SWITCH_INVALID);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_TESTING)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_COMMITTING)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_advance(&s,a.id,MEETING_SWITCH_COMPLETE)==MEETING_SWITCH_INVALID);
    bool confirmed=false;
    assert(meeting_wifi_switch_commit_confirmed(&s,a.id,proof,&confirmed)==MEETING_SWITCH_INVALID);
    assert(meeting_wifi_switch_query(&s,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMMITTING);
    confirmed=true;
    assert(meeting_wifi_switch_commit_confirmed(&s,a.id,proof,&confirmed)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_query(&s,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMPLETE);
    meeting_wifi_switch rebooted; meeting_wifi_switch_init(&rebooted,&io);
    assert(meeting_wifi_switch_recover(&rebooted)==MEETING_SWITCH_OK);
    assert(rebooted.present && rebooted.record.state==MEETING_SWITCH_COMPLETE);
    assert(meeting_wifi_switch_query(&rebooted,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMPLETE);
    assert(meeting_wifi_switch_ack(&rebooted,unknown,true)==MEETING_SWITCH_CONFLICT);
    assert(meeting_wifi_switch_ack(&rebooted,a.id,false)==MEETING_SWITCH_INVALID);
    assert(meeting_wifi_switch_ack(&rebooted,a.id,true)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_query(&rebooted,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMPLETE);
    meeting_wifi_switch_init(&rebooted,&io);
    assert(meeting_wifi_switch_recover(&rebooted)==MEETING_SWITCH_OK && rebooted.record.acknowledged);
    assert(meeting_wifi_switch_query(&rebooted,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMPLETE);
    assert(meeting_wifi_switch_ack(&rebooted,a.id,true)==MEETING_SWITCH_OK);
    b.id[0]=4;
    /* Replacement may report failure before or after the new record lands. */
    f.fail=1;
    assert(meeting_wifi_switch_begin(&rebooted,&b)==MEETING_SWITCH_STORAGE_ERROR);
    f.fail=0;
    assert(meeting_wifi_switch_reconcile(&rebooted,&f.record)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_query(&rebooted,a.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_COMPLETE);
    f.fail=1;
    assert(meeting_wifi_switch_begin(&rebooted,&b)==MEETING_SWITCH_STORAGE_ERROR);
    f.fail=0;f.record=rebooted.attempted;
    assert(meeting_wifi_switch_reconcile(&rebooted,&f.record)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_begin(&rebooted,&b)==MEETING_SWITCH_OK);
    b.id[0]=5;
    assert(meeting_wifi_switch_begin(&rebooted,&b)==MEETING_SWITCH_CONFLICT);
    f.fail=1; meeting_wifi_switch_init(&rebooted,&io);
    assert(meeting_wifi_switch_recover(&rebooted)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_begin(&rebooted,&a)==MEETING_SWITCH_STORAGE_ERROR);
    puts("wifi switch transition persistence: PASS");
}
