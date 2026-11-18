#include "meeting_agent_mgmt.h"
#include <stdint.h>
#include <string.h>
static int hexval(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;}
bool meeting_agent_mgmt_device_id(const uint8_t mac[6],uint8_t id[16])
{
    if(!mac||!id)return false;
    unsigned nonzero=0;for(unsigned i=0;i<6;i++)nonzero|=mac[i];
    if(!nonzero)return false;
    memset(id,0,16);memcpy(id,"MGM1",4);memcpy(id+4,mac,6);return true;
}
meeting_mgmt_result meeting_agent_mgmt_install(const meeting_mgmt_io *io,const uint8_t id[16],const char *text,bool allowed)
{
    if(!io||!id||!text||!allowed||strlen(text)!=64)return MEETING_MGMT_ERROR;
    meeting_mgmt_record next={.generation=1,.state=MEETING_MGMT_ACTIVE},old={0};
    memcpy(next.device_id,id,16);
    meeting_mgmt_result result=MEETING_MGMT_ERROR;
    for(unsigned i=0;i<32;i++){
        int hi=hexval(text[2*i]),lo=hexval(text[2*i+1]);
        if(hi<0||lo<0)goto done;
        next.secret[i]=(uint8_t)((hi<<4)|lo);
    }
    result=meeting_mgmt_load(io,&old);
    if(result==MEETING_MGMT_OK){
        result=old.generation==1&&old.state==MEETING_MGMT_ACTIVE&&
            !memcmp(old.device_id,next.device_id,16)&&!memcmp(old.secret,next.secret,32)?MEETING_MGMT_OK:MEETING_MGMT_CONFLICT;
    }else if(result==MEETING_MGMT_EMPTY)result=meeting_mgmt_replace(io,0,&next);
done:
    meeting_mgmt_clear(&next);meeting_mgmt_clear(&old);return result;
}

meeting_mgmt_result meeting_agent_mgmt_maintain(const meeting_mgmt_io *io,const uint8_t id[16],
                                                const char *text,uint64_t expected,bool revoke,bool allowed)
{
    if(!io||!id||!allowed||!expected||expected==UINT64_MAX)return MEETING_MGMT_ERROR;
    if((revoke&&text&&*text)||(!revoke&&(!text||strlen(text)!=64)))return MEETING_MGMT_ERROR;
    meeting_mgmt_record old={0},next={.generation=expected+1,
        .state=revoke?MEETING_MGMT_REVOKED:MEETING_MGMT_ACTIVE};
    memcpy(next.device_id,id,16);
    meeting_mgmt_result result=MEETING_MGMT_ERROR;
    if(!revoke){
        for(unsigned i=0;i<32;i++){
            int hi=hexval(text[2*i]),lo=hexval(text[2*i+1]);
            if(hi<0||lo<0)goto done;
            next.secret[i]=(uint8_t)((hi<<4)|lo);
        }
    }
    result=meeting_mgmt_load(io,&old);
    if(result!=MEETING_MGMT_OK){if(result==MEETING_MGMT_EMPTY)result=MEETING_MGMT_CONFLICT;goto done;}
    if(memcmp(old.device_id,id,16)){result=MEETING_MGMT_CONFLICT;goto done;}
    if(old.generation==next.generation&&old.state==next.state&&
       !memcmp(old.secret,next.secret,32)){result=MEETING_MGMT_OK;goto done;}
    if(old.generation!=expected||(revoke&&old.state!=MEETING_MGMT_ACTIVE)||
       (!revoke&&old.state==MEETING_MGMT_ACTIVE&&!memcmp(old.secret,next.secret,32))){
        result=MEETING_MGMT_CONFLICT;goto done;
    }
    result=meeting_mgmt_replace(io,expected,&next);
done:
    meeting_mgmt_clear(&next);meeting_mgmt_clear(&old);return result;
}
