#include "meeting_management_gate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct{uint8_t data[MEETING_MGMT_RECORD_SIZE];int present,write_fail,commit_then_fail,read_fail;} fixture;
static int read_blob(void *p,uint8_t out[MEETING_MGMT_RECORD_SIZE]){fixture *s=p;if(s->read_fail)return -1;if(!s->present)return 1;memcpy(out,s->data,sizeof s->data);return 0;}
static bool write_blob(void *p,const uint8_t data[MEETING_MGMT_RECORD_SIZE]){fixture *s=p;if(s->write_fail)return false;memcpy(s->data,data,sizeof s->data);s->present=1;return !s->commit_then_fail;}
int main(void){
 fixture f={0};meeting_mgmt_io io={&f,read_blob,write_blob};meeting_pairing_session sessions;meeting_pairing_session_init(&sessions);meeting_mgmt_gate g={0};
 assert(meeting_mgmt_gate_init(&g,&io,&sessions)==MEETING_MGMT_GATE_EMPTY);
 meeting_mgmt_record a={.generation=1,.device_id={1},.secret={2},.state=MEETING_MGMT_ACTIVE};meeting_pairing_token t;
 meeting_pairing_session_accept(&sessions,7);assert(meeting_pairing_session_capture(&sessions,7,&t));
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&a)==MEETING_MGMT_CONFLICT);
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_USB,NULL,&a)==MEETING_MGMT_OK&&g.state==MEETING_MGMT_GATE_READY);
 assert(!meeting_pairing_session_valid(&sessions,t));
 meeting_pairing_session_accept(&sessions,8);assert(meeting_pairing_session_capture(&sessions,8,&t));
 meeting_mgmt_record b=a;b.generation=2;assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&b)==MEETING_MGMT_CONFLICT);
 b.secret[0]=3;b.device_id[0]=9;assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&b)==MEETING_MGMT_CONFLICT);b.device_id[0]=1;
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&b)==MEETING_MGMT_OK);assert(!meeting_pairing_session_valid(&sessions,t));
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&b)==MEETING_MGMT_CONFLICT);
 meeting_pairing_session_accept(&sessions,9);assert(meeting_pairing_session_capture(&sessions,9,&t));
 meeting_mgmt_record revoked=b;revoked.generation=3;revoked.state=MEETING_MGMT_REVOKED;memset(revoked.secret,0,sizeof revoked.secret);
 f.commit_then_fail=1;assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&revoked)==MEETING_MGMT_ERROR);assert(g.state==MEETING_MGMT_GATE_UNAVAILABLE&&!meeting_pairing_session_valid(&sessions,t));
 for(unsigned i=0;i<sizeof g.record.secret;i++)assert(!g.record.secret[i]);
 f.commit_then_fail=0;assert(meeting_mgmt_gate_init(&g,&io,&sessions)==MEETING_MGMT_GATE_REVOKED);
 meeting_mgmt_record c=revoked;c.generation=4;c.state=MEETING_MGMT_ACTIVE;c.secret[0]=4;
 meeting_pairing_session_accept(&sessions,10);assert(meeting_pairing_session_capture(&sessions,10,&t));
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&c)==MEETING_MGMT_CONFLICT);
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_USB,NULL,&c)==MEETING_MGMT_OK);
 meeting_pairing_session_accept(&sessions,11);assert(meeting_pairing_session_capture(&sessions,11,&t));b=c;b.generation=5;b.secret[0]=5;f.write_fail=1;
 assert(meeting_mgmt_gate_update(&g,MEETING_MGMT_ACTOR_SESSION,&t,&b)==MEETING_MGMT_ERROR&&g.state==MEETING_MGMT_GATE_UNAVAILABLE&&!meeting_pairing_session_valid(&sessions,t));
 f.write_fail=0;f.read_fail=1;assert(meeting_mgmt_gate_init(&g,&g.io,&sessions)==MEETING_MGMT_GATE_UNAVAILABLE);
 meeting_mgmt_gate_clear(&g);puts("management gate failure and session tests: PASS");
}
