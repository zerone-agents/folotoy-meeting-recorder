#include "meeting_management_credential.h"
#include <limits.h>
#include <string.h>

static void wipe(void *p,unsigned n){volatile uint8_t *q=p;while(n--)*q++=0;}
void meeting_mgmt_clear(meeting_mgmt_record *r){if(r)wipe(r,sizeof(*r));}
static uint32_t crc32(const uint8_t *p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static bool valid(const meeting_mgmt_record *r){
    if(!r||!r->generation||(r->state!=MEETING_MGMT_ACTIVE&&r->state!=MEETING_MGMT_REVOKED))return false;
    unsigned id=0,secret=0;for(unsigned i=0;i<MEETING_MGMT_ID_SIZE;i++)id|=r->device_id[i];
    for(unsigned i=0;i<MEETING_MGMT_SECRET_SIZE;i++)secret|=r->secret[i];
    return id&&(r->state==MEETING_MGMT_ACTIVE?secret!=0:secret==0);
}
bool meeting_mgmt_encode(const meeting_mgmt_record *r,uint8_t out[MEETING_MGMT_RECORD_SIZE]){
    if(!out||!valid(r))return false;
    memset(out,0,MEETING_MGMT_RECORD_SIZE);
    memcpy(out,r->state==MEETING_MGMT_ACTIVE?"MGA1":"MGR1",4);
    put64(out+4,r->generation);memcpy(out+12,r->device_id,16);memcpy(out+28,r->secret,32);put32(out+60,crc32(out,60));return true;
}
bool meeting_mgmt_decode(const uint8_t data[MEETING_MGMT_RECORD_SIZE],meeting_mgmt_record *out){
    if(!data||!out||get32(data+60)!=crc32(data,60))return false;
    meeting_mgmt_record r={0};
    if(!memcmp(data,"MGA1",4))r.state=MEETING_MGMT_ACTIVE;
    else if(!memcmp(data,"MGR1",4))r.state=MEETING_MGMT_REVOKED;
    else return false;
    r.generation=get64(data+4);memcpy(r.device_id,data+12,16);memcpy(r.secret,data+28,32);
    if(!valid(&r)){meeting_mgmt_clear(&r);return false;}*out=r;meeting_mgmt_clear(&r);return true;
}
meeting_mgmt_result meeting_mgmt_load(const meeting_mgmt_io *io,meeting_mgmt_record *out){
    if(!out)return MEETING_MGMT_ERROR;
    meeting_mgmt_clear(out);
    if(!io||!io->read)return MEETING_MGMT_ERROR;
    uint8_t data[MEETING_MGMT_RECORD_SIZE];int rc=io->read(io->ctx,data);
    if(rc==1){wipe(data,sizeof(data));return MEETING_MGMT_EMPTY;}
    bool ok=rc==0&&meeting_mgmt_decode(data,out);wipe(data,sizeof(data));return ok?MEETING_MGMT_OK:MEETING_MGMT_ERROR;
}
meeting_mgmt_result meeting_mgmt_replace(const meeting_mgmt_io *io,uint64_t expected,const meeting_mgmt_record *next){
    if(!io||!io->read||!io->write_sync||!valid(next)||expected==UINT64_MAX||next->generation!=expected+1)return MEETING_MGMT_ERROR;
    meeting_mgmt_record old={0},verify={0};uint8_t encoded[MEETING_MGMT_RECORD_SIZE],actual[MEETING_MGMT_RECORD_SIZE];
    meeting_mgmt_result status=meeting_mgmt_load(io,&old),result=MEETING_MGMT_ERROR;
    if((status==MEETING_MGMT_EMPTY&&expected!=0)||(status==MEETING_MGMT_OK&&(old.generation!=expected||memcmp(old.device_id,next->device_id,16)))){result=MEETING_MGMT_CONFLICT;goto done;}
    if(status!=MEETING_MGMT_OK&&status!=MEETING_MGMT_EMPTY)goto done;
    if(!meeting_mgmt_encode(next,encoded)||!io->write_sync(io->ctx,encoded))goto done;
    if(io->read(io->ctx,actual)!=0||!meeting_mgmt_decode(actual,&verify)||memcmp(encoded,actual,sizeof(encoded)))goto done;
    result=MEETING_MGMT_OK;
 done: meeting_mgmt_clear(&old);meeting_mgmt_clear(&verify);wipe(encoded,sizeof(encoded));wipe(actual,sizeof(actual));return result;
}
