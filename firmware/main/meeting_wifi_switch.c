#include "meeting_wifi_switch.h"
#include <string.h>
static bool nonzero(const uint8_t *p, unsigned n) { unsigned v=0; while(n--)v|=*p++; return v!=0; }
static bool valid(const meeting_switch_record *r) {
    return r && nonzero(r->request.id,16) && nonzero(r->request.digest,32) &&
           r->state>=MEETING_SWITCH_WAITING && r->state<=MEETING_SWITCH_COMMITTING &&
           (!r->acknowledged || r->state==MEETING_SWITCH_COMPLETE || r->state==MEETING_SWITCH_ROLLED_BACK);
}
static bool same_record(const meeting_switch_record *a,const meeting_switch_record *b) {
    return a->state==b->state && a->acknowledged==b->acknowledged && !memcmp(&a->request,&b->request,sizeof(a->request));
}
void meeting_wifi_switch_init(meeting_wifi_switch *s,const meeting_switch_io *io) {
    if(!s)return;
    memset(s,0,sizeof(*s));
    if(io)s->io=*io;
}
meeting_switch_result meeting_wifi_switch_recover(meeting_wifi_switch *s) {
    if(!s||!s->io.load||s->uncertain||s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    meeting_switch_record r={0};int status=s->io.load(s->io.ctx,&r);
    if(status==1){s->present=false;s->recovered=true;return MEETING_SWITCH_OK;}
    if(status!=0||!valid(&r)){s->recovered=false;return MEETING_SWITCH_STORAGE_ERROR;}
    s->record=r;s->present=true;s->recovered=true;return MEETING_SWITCH_OK;
}
static meeting_switch_result commit(meeting_wifi_switch *s,const meeting_switch_record *r) {
    s->attempted=*r;
    if(!s->io.save || !s->io.save(s->io.ctx,r)){s->uncertain=true;return MEETING_SWITCH_STORAGE_ERROR;}
    s->record=*r;s->present=true;return MEETING_SWITCH_OK;
}
meeting_switch_result meeting_wifi_switch_begin(meeting_wifi_switch *s,const meeting_switch_request *r) {
    if(!s||!r||!nonzero(r->id,16)||!nonzero(r->digest,32))return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(s->present){
        if(!memcmp(&s->record.request,r,sizeof(*r)))return MEETING_SWITCH_OK;
        if(!s->record.acknowledged || !memcmp(s->record.request.id,r->id,16))return MEETING_SWITCH_CONFLICT;
    }
    meeting_switch_record next={.request=*r,.state=MEETING_SWITCH_WAITING};return commit(s,&next);
}
meeting_switch_result meeting_wifi_switch_advance(meeting_wifi_switch *s,const uint8_t id[16],meeting_switch_state next) {
    if(!s||!id)return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(!s->present||memcmp(id,s->record.request.id,16))return MEETING_SWITCH_CONFLICT;
    meeting_switch_state old=s->record.state;
    if(!((old==MEETING_SWITCH_WAITING && (next==MEETING_SWITCH_TESTING||next==MEETING_SWITCH_ROLLED_BACK)) ||
         (old==MEETING_SWITCH_TESTING && (next==MEETING_SWITCH_WAITING||next==MEETING_SWITCH_COMMITTING||next==MEETING_SWITCH_ROLLED_BACK))))return MEETING_SWITCH_INVALID;
    meeting_switch_record r=s->record;r.state=next;return commit(s,&r);
}
meeting_switch_result meeting_wifi_switch_commit_confirmed(meeting_wifi_switch *s,const uint8_t id[16],
    bool (*verify)(void *,const meeting_switch_request *),void *ctx) {
    if(!s||!id||!verify)return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(!s->present||memcmp(id,s->record.request.id,16))return MEETING_SWITCH_CONFLICT;
    if(s->record.state!=MEETING_SWITCH_COMMITTING||!verify(ctx,&s->record.request))return MEETING_SWITCH_INVALID;
    meeting_switch_record r=s->record;r.state=MEETING_SWITCH_COMPLETE;return commit(s,&r);
}
meeting_switch_result meeting_wifi_switch_rollback_confirmed(meeting_wifi_switch *s,const uint8_t id[16],
    bool (*verify)(void *,const meeting_switch_request *),void *ctx) {
    if(!s||!id||!verify)return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(!s->present||memcmp(id,s->record.request.id,16))return MEETING_SWITCH_CONFLICT;
    if(s->record.state!=MEETING_SWITCH_COMMITTING||!verify(ctx,&s->record.request))return MEETING_SWITCH_INVALID;
    meeting_switch_record r=s->record;r.state=MEETING_SWITCH_ROLLED_BACK;return commit(s,&r);
}
meeting_switch_result meeting_wifi_switch_query(const meeting_wifi_switch *s,const uint8_t id[16],meeting_switch_state *out) {
    if(!s||!id||!out||!nonzero(id,16))return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(!s->present||memcmp(id,s->record.request.id,16))return MEETING_SWITCH_UNKNOWN;
    *out=s->record.state;return MEETING_SWITCH_OK;
}
meeting_switch_result meeting_wifi_switch_ack(meeting_wifi_switch *s,const uint8_t id[16],bool confirmed) {
    if(!s||!id||!confirmed)return MEETING_SWITCH_INVALID;
    if(s->uncertain||!s->recovered)return MEETING_SWITCH_STORAGE_ERROR;
    if(!s->present||memcmp(id,s->record.request.id,16))return MEETING_SWITCH_CONFLICT;
    if(s->record.state!=MEETING_SWITCH_COMPLETE&&s->record.state!=MEETING_SWITCH_ROLLED_BACK)return MEETING_SWITCH_INVALID;
    if(s->record.acknowledged)return MEETING_SWITCH_OK;
    meeting_switch_record acknowledged=s->record;acknowledged.acknowledged=true;
    return commit(s,&acknowledged);
}
meeting_switch_result meeting_wifi_switch_reconcile(meeting_wifi_switch *s,const meeting_switch_record *r) {
    if(!s||!s->uncertain||!valid(r))return MEETING_SWITCH_INVALID;
    if(!same_record(r,&s->attempted) && (!s->present || !same_record(r,&s->record)))return MEETING_SWITCH_CONFLICT;
    s->record=*r;s->present=true;s->uncertain=false;return MEETING_SWITCH_OK;
}
