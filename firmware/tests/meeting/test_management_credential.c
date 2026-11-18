#include "meeting_management_credential.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {uint8_t data[MEETING_MGMT_RECORD_SIZE];int present,read_error,mode;} store;
static int read_record(void *p,uint8_t out[MEETING_MGMT_RECORD_SIZE]){store *s=p;if(s->read_error)return -1;if(!s->present)return 1;memcpy(out,s->data,sizeof s->data);return 0;}
static bool write_record(void *p,const uint8_t data[MEETING_MGMT_RECORD_SIZE]){store *s=p;if(s->mode==1)return false;memcpy(s->data,data,sizeof s->data);s->present=1;if(s->mode==2)s->data[35]^=1;return s->mode!=3;}
int main(void){
 store s={0};meeting_mgmt_io io={&s,read_record,write_record};meeting_mgmt_record a={.generation=1,.device_id={1},.secret={2},.state=MEETING_MGMT_ACTIVE},out={0};
 assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_EMPTY);
 uint8_t encoded[MEETING_MGMT_RECORD_SIZE];assert(MEETING_MGMT_RECORD_SIZE==64u&&meeting_mgmt_encode(&a,encoded));
 assert(!memcmp(encoded,"MGA1",4));assert(meeting_mgmt_decode(encoded,&out));encoded[10]^=1;assert(!meeting_mgmt_decode(encoded,&out));encoded[10]^=1;
 meeting_mgmt_record invalid=a;invalid.state=MEETING_MGMT_REVOKED;assert(!meeting_mgmt_encode(&invalid,encoded));
 assert(meeting_mgmt_replace(&io,0,&a)==MEETING_MGMT_OK);assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_OK&&out.generation==1&&out.secret[0]==2);
 meeting_mgmt_record b=a;b.generation=2;b.secret[0]=3;
 assert(meeting_mgmt_replace(&io,0,&b)==MEETING_MGMT_ERROR);
 assert(meeting_mgmt_replace(&io,1,&b)==MEETING_MGMT_OK);
 assert(meeting_mgmt_replace(&io,1,&b)==MEETING_MGMT_CONFLICT);
 meeting_mgmt_record cross=b;cross.generation=3;cross.device_id[0]=2;assert(meeting_mgmt_replace(&io,2,&cross)==MEETING_MGMT_CONFLICT);
 meeting_mgmt_record revoked=b;revoked.generation=3;revoked.state=MEETING_MGMT_REVOKED;memset(revoked.secret,0,sizeof revoked.secret);
 s.mode=3;assert(meeting_mgmt_replace(&io,2,&revoked)==MEETING_MGMT_ERROR);s.mode=0;assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_OK&&out.state==MEETING_MGMT_REVOKED&&out.generation==3);
 assert(meeting_mgmt_replace(&io,2,&revoked)==MEETING_MGMT_CONFLICT);
 meeting_mgmt_record restore=revoked;restore.generation=4;restore.state=MEETING_MGMT_ACTIVE;restore.secret[0]=4;
 s.mode=2;assert(meeting_mgmt_replace(&io,3,&restore)==MEETING_MGMT_ERROR);assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_ERROR);for(unsigned i=0;i<sizeof out;i++)assert(!((unsigned char *)&out)[i]);
 s.mode=0;s.read_error=1;assert(meeting_mgmt_replace(&io,3,&restore)==MEETING_MGMT_ERROR);s.read_error=0;
 meeting_mgmt_clear(&out);for(unsigned i=0;i<sizeof out;i++)assert(!((unsigned char *)&out)[i]);
 puts("management credential persistence tests: PASS");
}
