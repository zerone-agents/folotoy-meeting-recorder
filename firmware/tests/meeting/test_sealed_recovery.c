#include "meeting_recovery.h"
#include <assert.h>
#include <string.h>
#include <mbedtls/sha256.h>
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=1;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;c->value=1;return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)c->value=c->value*33+p[i];return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){for(int i=0;i<32;i++)out[i]=(unsigned char)(c->value>>((i%4)*8));return 0;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
typedef struct {uint8_t slots[2][MEETING_SNAPSHOT_SIZE];bool present[2],fail_write;} fixture;
static int rd(void *ctx,unsigned slot,uint8_t *out){fixture *f=ctx;if(!f->present[slot])return 1;memcpy(out,f->slots[slot],MEETING_SNAPSHOT_SIZE);return 0;}
static bool wr(void *ctx,unsigned slot,const uint8_t *in){fixture *f=ctx;if(f->fail_write)return false;memcpy(f->slots[slot],in,MEETING_SNAPSHOT_SIZE);f->present[slot]=true;return true;}
static uint32_t test_crc32(const uint8_t *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
int main(void){
 unsigned char bytes[100]={0};memcpy(bytes,"SCR1",4);bytes[4]=1;bytes[8]=1;memset(bytes+20,255,4);bytes[96]=3;bytes[97]=0xf8;bytes[98]=0xff;bytes[99]=0xfe;
 FILE *f=tmpfile();assert(f&&fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes)&&fflush(f)==0);
 fixture store={0};meeting_snapshot_io io={&store,rd,wr};meeting_snapshot out;
 meeting_seal seal={.file_id=1,.meeting_id={1},.frames=1,.bytes=100,.reason=1};
 meeting_seal wrong=seal;wrong.frames=2;assert(meeting_sealed_prepare_cancel(f,&io,&wrong,&out,NULL,NULL)==MEETING_RECOVERY_SOURCE_ERROR);
 wrong=seal;wrong.file_id=2;assert(meeting_sealed_prepare_cancel(f,&io,&wrong,&out,NULL,NULL)==MEETING_RECOVERY_SOURCE_ERROR);
 wrong=seal;wrong.bytes=99;assert(meeting_sealed_prepare_cancel(f,&io,&wrong,&out,NULL,NULL)==MEETING_RECOVERY_SOURCE_ERROR);
 store.fail_write=true;assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_STORAGE_ERROR);
 assert(!store.present[0]&&!store.present[1]);store.fail_write=false;
 assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_OK&&out.reason==1&&out.frames==1);
 memcpy(store.slots[1],store.slots[0],MEETING_SNAPSHOT_SIZE);store.slots[1][77]=1;
 uint32_t crc=test_crc32(store.slots[1],84);for(unsigned i=0;i<4;i++)store.slots[1][84+i]=(uint8_t)(crc>>(8*i));store.present[1]=true;
 uint8_t before[sizeof store.slots];memcpy(before,store.slots,sizeof before);
 assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_STORAGE_ERROR);
 assert(!memcmp(before,store.slots,sizeof before));store.present[1]=false;

 wrong=seal;wrong.meeting_id[0]=2;assert(meeting_sealed_prepare_cancel(f,&io,&wrong,&out,NULL,NULL)==MEETING_RECOVERY_ID_CONFLICT);
 assert(fseek(f,0,SEEK_SET)==0);unsigned char after[100];assert(fread(after,1,sizeof(after),f)==sizeof(after)&&!memcmp(after,bytes,sizeof(bytes)));
 /* A persisted snapshot does not waive revalidation after restart. */
 assert(fseek(f,99,SEEK_SET)==0&&fputc(0xfd,f)!=EOF&&fflush(f)==0);
 assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_SOURCE_ERROR);
 assert(fseek(f,99,SEEK_SET)==0&&fputc(0xfe,f)!=EOF&&fflush(f)==0);
 assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_OK);
 assert(fseek(f,0,SEEK_END)==0&&fputc(3,f)!=EOF&&fflush(f)==0);
 assert(meeting_sealed_prepare_cancel(f,&io,&seal,&out,NULL,NULL)==MEETING_RECOVERY_SOURCE_ERROR);
 fclose(f);return 0;
}
