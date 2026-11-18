#include "meeting_management_gate.h"
#include <limits.h>
#include <string.h>
static bool same_secret(const uint8_t *a,const uint8_t *b){unsigned diff=0;for(unsigned i=0;i<MEETING_MGMT_SECRET_SIZE;i++)diff|=a[i]^b[i];return diff==0;}
void meeting_mgmt_gate_clear(meeting_mgmt_gate *g){if(!g)return;if(g->sessions)meeting_pairing_session_reset(g->sessions);meeting_mgmt_clear(&g->record);g->state=MEETING_MGMT_GATE_UNAVAILABLE;}
meeting_mgmt_gate_state meeting_mgmt_gate_init(meeting_mgmt_gate *g,const meeting_mgmt_io *io,meeting_pairing_session *sessions){
    if(!g)return MEETING_MGMT_GATE_UNAVAILABLE;
    if(!io||!io->read||!io->write_sync||!sessions){meeting_mgmt_gate_clear(g);return MEETING_MGMT_GATE_UNAVAILABLE;}
    meeting_mgmt_io source=*io;
    meeting_mgmt_gate_clear(g);
    memset(g,0,sizeof(*g));g->state=MEETING_MGMT_GATE_UNAVAILABLE;
    g->io=source;g->sessions=sessions;meeting_pairing_session_reset(sessions);
    meeting_mgmt_result r=meeting_mgmt_load(&source,&g->record);
    if(r==MEETING_MGMT_EMPTY)g->state=MEETING_MGMT_GATE_EMPTY;
    else if(r==MEETING_MGMT_OK)g->state=g->record.state==MEETING_MGMT_ACTIVE?MEETING_MGMT_GATE_READY:MEETING_MGMT_GATE_REVOKED;
    return g->state;
}
meeting_mgmt_result meeting_mgmt_gate_update(meeting_mgmt_gate *g,meeting_mgmt_actor actor,const meeting_pairing_token *token,const meeting_mgmt_record *next){
    if(!g||!next||!g->sessions||g->state==MEETING_MGMT_GATE_UNAVAILABLE)return MEETING_MGMT_ERROR;
    if(actor!=MEETING_MGMT_ACTOR_USB&&actor!=MEETING_MGMT_ACTOR_SESSION)return MEETING_MGMT_ERROR;
    if(actor==MEETING_MGMT_ACTOR_SESSION&&(g->state!=MEETING_MGMT_GATE_READY||!token||!meeting_pairing_session_valid(g->sessions,*token)))return MEETING_MGMT_CONFLICT;
    if(g->state!=MEETING_MGMT_GATE_READY&&actor!=MEETING_MGMT_ACTOR_USB)return MEETING_MGMT_CONFLICT;
    uint64_t expected=g->state==MEETING_MGMT_GATE_EMPTY?0:g->record.generation;
    if(expected==UINT64_MAX||next->generation!=expected+1)return MEETING_MGMT_CONFLICT;
    if(g->state!=MEETING_MGMT_GATE_EMPTY&&memcmp(next->device_id,g->record.device_id,MEETING_MGMT_ID_SIZE))return MEETING_MGMT_CONFLICT;
    if(g->state==MEETING_MGMT_GATE_EMPTY&&next->state!=MEETING_MGMT_ACTIVE)return MEETING_MGMT_CONFLICT;
    if(g->state==MEETING_MGMT_GATE_REVOKED&&next->state!=MEETING_MGMT_ACTIVE)return MEETING_MGMT_CONFLICT;
    if(g->state==MEETING_MGMT_GATE_READY&&next->state==MEETING_MGMT_ACTIVE&&same_secret(next->secret,g->record.secret))return MEETING_MGMT_CONFLICT;
    uint8_t encoded[MEETING_MGMT_RECORD_SIZE];
    if(!meeting_mgmt_encode(next,encoded))return MEETING_MGMT_ERROR;
    volatile uint8_t *p=encoded;for(unsigned i=0;i<sizeof(encoded);i++)p[i]=0;
    meeting_mgmt_record target=*next;
    meeting_mgmt_gate_clear(g);
    meeting_mgmt_result result=meeting_mgmt_replace(&g->io,expected,&target);
    if(result==MEETING_MGMT_OK){g->record=target;g->state=target.state==MEETING_MGMT_ACTIVE?MEETING_MGMT_GATE_READY:MEETING_MGMT_GATE_REVOKED;}
    meeting_mgmt_clear(&target);return result;
}
