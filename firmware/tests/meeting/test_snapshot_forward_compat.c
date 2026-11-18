#include "meeting_snapshot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned char slot[2][MEETING_SNAPSHOT_SIZE]; int present[2]; unsigned writes; } fake_store;
static int read_slot(void *ctx,unsigned slot,uint8_t out[MEETING_SNAPSHOT_SIZE])
{fake_store *s=ctx;if(!s->present[slot])return 1;memcpy(out,s->slot[slot],MEETING_SNAPSHOT_SIZE);return 0;}
static bool write_slot(void *ctx,unsigned slot,const uint8_t in[MEETING_SNAPSHOT_SIZE])
{fake_store *s=ctx;s->writes++;memcpy(s->slot[slot],in,MEETING_SNAPSHOT_SIZE);s->present[slot]=1;return true;}
static uint32_t checksum(const uint8_t *p,size_t n)
{uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
int main(void)
{
 fake_store store={0};meeting_snapshot value={.generation=1,.valid_end=104,.frames=2,.reason=3},out;
 memset(value.meeting_id,1,16);memset(value.source_sha256,2,32);
 assert(meeting_snapshot_encode(&value,store.slot[0]));store.present[0]=1;
 memcpy(store.slot[1],store.slot[0],MEETING_SNAPSHOT_SIZE);
 store.slot[1][77]=1; /* A future field that old firmware does not understand. */
 uint32_t crc=checksum(store.slot[1],84);for(unsigned i=0;i<4;i++)store.slot[1][84+i]=(uint8_t)(crc>>(8*i));
 store.present[1]=1;meeting_snapshot_io io={&store,read_slot,write_slot};unsigned active=0;
 assert(meeting_snapshot_load(&io,&out,&active)==MEETING_SNAPSHOT_DEGRADED);
 value.generation=2;assert(!meeting_snapshot_commit(&io,&value));assert(!store.writes);
 store.present[0]=0;assert(meeting_snapshot_load(&io,&out,&active)==MEETING_SNAPSHOT_DEGRADED);
 assert(!meeting_snapshot_commit(&io,&value));assert(!store.writes);
 puts("snapshot unknown-format fail-closed test: PASS");
}
