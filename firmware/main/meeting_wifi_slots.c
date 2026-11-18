#include "meeting_wifi_slots.h"
#include <string.h>
#include <stdint.h>
static bool valid(const meeting_wifi_slot *s){return s&&s->generation&&s->ssid[0]&&memchr(s->ssid,0,sizeof(s->ssid))&&memchr(s->password,0,sizeof(s->password));}
static bool nonzero(const uint8_t *p,unsigned n){unsigned value=0;while(n--)value|=*p++;return value!=0;}
static bool same(const meeting_wifi_slot *a,const meeting_wifi_slot *b){return a->generation==b->generation&&!strcmp(a->ssid,b->ssid)&&!strcmp(a->password,b->password)&&!memcmp(a->request_id,b->request_id,16)&&!memcmp(a->digest,b->digest,32);}
static void wipe(void *p,size_t n){volatile unsigned char *q=p;while(n--)*q++=0;}
meeting_wifi_slots_result meeting_wifi_slots_load(const meeting_wifi_slots_io *io,meeting_wifi_slot *out,unsigned *index){
 if(!io||!io->read_slot||!io->read_pointer||!out||!index)return MEETING_WIFI_SLOTS_INVALID;
 meeting_wifi_pointer p;meeting_wifi_slot s={0};int r=io->read_pointer(io->ctx,&p);
 if(r!=0)return r==1?MEETING_WIFI_SLOTS_CORRUPT:MEETING_WIFI_SLOTS_IO_ERROR;
 if(p.slot>1||!p.generation)return MEETING_WIFI_SLOTS_CORRUPT;
 r=io->read_slot(io->ctx,p.slot,&s);
 if(r!=0){wipe(&s,sizeof(s));return r==1?MEETING_WIFI_SLOTS_CORRUPT:MEETING_WIFI_SLOTS_IO_ERROR;}
 if(!valid(&s)||s.generation!=p.generation){wipe(&s,sizeof(s));return MEETING_WIFI_SLOTS_CORRUPT;}
 *out=s;*index=p.slot;wipe(&s,sizeof(s));return MEETING_WIFI_SLOTS_OK;
}
meeting_wifi_slots_result meeting_wifi_slots_initialize(const meeting_wifi_slots_io *io,const meeting_wifi_slot *initial){
 if(!io||!io->read_slot||!io->write_slot||!io->read_pointer||!io->write_pointer||!valid(initial)||initial->generation!=1)return MEETING_WIFI_SLOTS_INVALID;
 meeting_wifi_pointer ptr={0},checkptr={0};meeting_wifi_slot slot={0};unsigned index=0;int status=io->read_pointer(io->ctx,&ptr);
 meeting_wifi_slots_result result=MEETING_WIFI_SLOTS_IO_ERROR;
 if(status==0){result=meeting_wifi_slots_load(io,&slot,&index);if(result==MEETING_WIFI_SLOTS_OK&&(!same(&slot,initial)||index!=0))result=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
 if(status!=1)goto done;
 status=io->read_slot(io->ctx,1,&slot);
 if(status!=1){result=status==0?MEETING_WIFI_SLOTS_CORRUPT:MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 status=io->read_slot(io->ctx,0,&slot);
 if(status==0){if(!valid(&slot)||!same(&slot,initial)){result=MEETING_WIFI_SLOTS_CORRUPT;goto done;}}
 else if(status==1){if(!io->write_slot(io->ctx,0,initial))goto done;}
 else goto done;
 wipe(&slot,sizeof(slot));
 if(io->read_slot(io->ctx,0,&slot)!=0||!valid(&slot)||!same(&slot,initial))goto done;
 ptr.slot=0;ptr.generation=1;
 if(!io->write_pointer(io->ctx,&ptr)||io->read_pointer(io->ctx,&checkptr)!=0||checkptr.slot!=0||checkptr.generation!=1)goto done;
 result=MEETING_WIFI_SLOTS_OK;
done:wipe(&slot,sizeof(slot));return result;
}
meeting_wifi_slots_result meeting_wifi_slots_stage(const meeting_wifi_slots_io *io,const meeting_wifi_slot *candidate){
 if(!io||!io->write_slot||!valid(candidate)||!nonzero(candidate->request_id,16)||!nonzero(candidate->digest,32))return MEETING_WIFI_SLOTS_INVALID;
 meeting_wifi_slot active={0},check={0};unsigned slot;meeting_wifi_slots_result r=meeting_wifi_slots_load(io,&active,&slot);
 if(r!=MEETING_WIFI_SLOTS_OK)return r;
 if(active.generation==UINT64_MAX||candidate->generation!=active.generation+1){r=MEETING_WIFI_SLOTS_INVALID;goto done;}
 int existing=io->read_slot(io->ctx,1-slot,&check);
 if(existing==0){
  if(!valid(&check)){r=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
  if(check.generation==candidate->generation){r=same(candidate,&check)?MEETING_WIFI_SLOTS_OK:MEETING_WIFI_SLOTS_CORRUPT;goto done;}
  if(check.generation>=active.generation){r=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
 }else if(existing!=1){r=MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 wipe(&check,sizeof(check));
 if(!io->write_slot(io->ctx,1-slot,candidate)||io->read_slot(io->ctx,1-slot,&check)!=0||!valid(&check)||!same(candidate,&check)){r=MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 r=MEETING_WIFI_SLOTS_OK;
done:wipe(&active,sizeof(active));wipe(&check,sizeof(check));return r;
}
meeting_wifi_slots_result meeting_wifi_slots_commit(const meeting_wifi_slots_io *io,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]){
 if(!io||!io->write_pointer||!generation||!request_id||!digest||!nonzero(request_id,16)||!nonzero(digest,32))return MEETING_WIFI_SLOTS_INVALID;
 meeting_wifi_slot active={0},candidate={0};unsigned slot;meeting_wifi_slots_result r=meeting_wifi_slots_load(io,&active,&slot);
 if(r!=MEETING_WIFI_SLOTS_OK)return r;
 if(active.generation==UINT64_MAX||generation!=active.generation+1){r=MEETING_WIFI_SLOTS_INVALID;goto done;}
 if(io->read_slot(io->ctx,1-slot,&candidate)!=0||!valid(&candidate)||candidate.generation!=generation||memcmp(candidate.request_id,request_id,16)||memcmp(candidate.digest,digest,32)){r=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
 meeting_wifi_pointer p={1-slot,generation},check;
 if(!io->write_pointer(io->ctx,&p)||io->read_pointer(io->ctx,&check)!=0||check.slot!=p.slot||check.generation!=p.generation){r=MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 r=MEETING_WIFI_SLOTS_OK;
done:wipe(&active,sizeof(active));wipe(&candidate,sizeof(candidate));return r;
}
bool meeting_wifi_slots_verify_active(const meeting_wifi_slots_io *io,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]){
 if(!io||!generation||!request_id||!digest||!nonzero(request_id,16)||!nonzero(digest,32))return false;
 meeting_wifi_slot active={0};unsigned index;
 bool ok=meeting_wifi_slots_load(io,&active,&index)==MEETING_WIFI_SLOTS_OK&&active.generation==generation&&
         !memcmp(active.request_id,request_id,16)&&!memcmp(active.digest,digest,32);
 wipe(&active,sizeof(active));return ok;
}
bool meeting_wifi_slots_verify_uncommitted(const meeting_wifi_slots_io *io,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]){
 if(!io||!io->read_slot||generation<2||!request_id||!digest||!nonzero(request_id,16)||!nonzero(digest,32))return false;
 meeting_wifi_slot active={0},candidate={0};unsigned index;
 bool ok=meeting_wifi_slots_load(io,&active,&index)==MEETING_WIFI_SLOTS_OK&&active.generation==generation-1&&
         io->read_slot(io->ctx,1-index,&candidate)==0&&valid(&candidate)&&candidate.generation==generation&&
         !memcmp(candidate.request_id,request_id,16)&&!memcmp(candidate.digest,digest,32);
 wipe(&active,sizeof(active));wipe(&candidate,sizeof(candidate));return ok;
}
meeting_wifi_slots_result meeting_wifi_slots_cleanup_uncommitted(const meeting_wifi_slots_io *io,uint64_t generation,const uint8_t request_id[16],const uint8_t digest[32]){
 if(!io||!io->read_slot||!io->erase_slot||generation<2||!request_id||!digest||!nonzero(request_id,16)||!nonzero(digest,32))return MEETING_WIFI_SLOTS_INVALID;
 meeting_wifi_slot active={0},candidate={0};unsigned index;meeting_wifi_slots_result result=meeting_wifi_slots_load(io,&active,&index);
 if(result!=MEETING_WIFI_SLOTS_OK)goto done;
 if(active.generation!=generation-1){result=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
 int state=io->read_slot(io->ctx,1-index,&candidate);
 if(state==1){result=MEETING_WIFI_SLOTS_OK;goto done;}
 if(state!=0){result=MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 if(!valid(&candidate)||candidate.generation!=generation||memcmp(candidate.request_id,request_id,16)||memcmp(candidate.digest,digest,32)){result=MEETING_WIFI_SLOTS_CORRUPT;goto done;}
 if(!io->erase_slot(io->ctx,1-index)||io->read_slot(io->ctx,1-index,&candidate)!=1){result=MEETING_WIFI_SLOTS_IO_ERROR;goto done;}
 result=MEETING_WIFI_SLOTS_OK;
done:wipe(&active,sizeof(active));wipe(&candidate,sizeof(candidate));return result;
}
