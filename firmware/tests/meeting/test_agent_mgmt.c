#include "meeting_agent_mgmt.h"
#include <assert.h>
#include <string.h>
typedef struct {unsigned char bytes[64];int present,commit_then_fail;unsigned writes;} fake;
static int read_(void *ctx,uint8_t out[64]){fake *f=ctx;if(!f->present)return 1;memcpy(out,f->bytes,64);return 0;}
static bool write_(void *ctx,const uint8_t in[64]){fake *f=ctx;memcpy(f->bytes,in,64);f->present=1;f->writes++;return !f->commit_then_fail;}
int main(void){
 uint8_t mac[6]={0x98,0xc3,0x77,0xf6,0xce,0xa4},bound[16];
 assert(meeting_agent_mgmt_device_id(mac,bound));
 assert(!memcmp(bound,"MGM1",4)&&!memcmp(bound+4,mac,6));
 memset(mac,0,sizeof(mac));assert(!meeting_agent_mgmt_device_id(mac,bound));
 fake f={0};meeting_mgmt_io io={&f,read_,write_};uint8_t id[16]={1};
 const char *secret="0101010101010101010101010101010101010101010101010101010101010101";
 assert(meeting_agent_mgmt_install(&io,id,secret,false)==MEETING_MGMT_ERROR&&f.writes==0);
 assert(meeting_agent_mgmt_install(&io,id,secret,true)==MEETING_MGMT_OK&&f.writes==1);
 assert(meeting_agent_mgmt_install(&io,id,secret,true)==MEETING_MGMT_OK&&f.writes==1);
 assert(meeting_agent_mgmt_install(&io,id,"bad",true)==MEETING_MGMT_ERROR&&f.writes==1);
 id[0]=2;assert(meeting_agent_mgmt_install(&io,id,secret,true)==MEETING_MGMT_CONFLICT&&f.writes==1);
 id[0]=1;
 const char *replacement="0202020202020202020202020202020202020202020202020202020202020202";
 id[0]=2;assert(meeting_agent_mgmt_maintain(&io,id,replacement,1,false,true)==MEETING_MGMT_CONFLICT&&f.writes==1);id[0]=1;
 assert(meeting_agent_mgmt_maintain(&io,id,"bad",1,false,true)==MEETING_MGMT_ERROR&&f.writes==1);
 assert(meeting_agent_mgmt_maintain(&io,id,replacement,1,true,true)==MEETING_MGMT_ERROR&&f.writes==1);
 assert(meeting_agent_mgmt_maintain(&io,id,replacement,1,false,false)==MEETING_MGMT_ERROR&&f.writes==1);
 assert(meeting_agent_mgmt_maintain(&io,id,secret,1,false,true)==MEETING_MGMT_CONFLICT&&f.writes==1);
 assert(meeting_agent_mgmt_maintain(&io,id,replacement,2,false,true)==MEETING_MGMT_CONFLICT&&f.writes==1);
 assert(meeting_agent_mgmt_maintain(&io,id,replacement,1,false,true)==MEETING_MGMT_OK&&f.writes==2);
 assert(meeting_agent_mgmt_maintain(&io,id,replacement,1,false,true)==MEETING_MGMT_OK&&f.writes==2);
 assert(meeting_agent_mgmt_maintain(&io,id,"",2,true,true)==MEETING_MGMT_OK&&f.writes==3);
 assert(meeting_agent_mgmt_maintain(&io,id,"",2,true,true)==MEETING_MGMT_OK&&f.writes==3);
 meeting_mgmt_record current={0};assert(meeting_mgmt_load(&io,&current)==MEETING_MGMT_OK&&current.state==MEETING_MGMT_REVOKED&&current.generation==3);
 assert(meeting_agent_mgmt_maintain(&io,id,secret,3,false,true)==MEETING_MGMT_OK&&f.writes==4);
 assert(meeting_mgmt_load(&io,&current)==MEETING_MGMT_OK&&current.state==MEETING_MGMT_ACTIVE&&current.generation==4);
 f.commit_then_fail=1;
 assert(meeting_agent_mgmt_maintain(&io,id,"",4,true,true)==MEETING_MGMT_ERROR&&f.writes==5);
 f.commit_then_fail=0;
 assert(meeting_agent_mgmt_maintain(&io,id,"",4,true,true)==MEETING_MGMT_OK&&f.writes==5);
 meeting_mgmt_clear(&current);
 return 0;
}
