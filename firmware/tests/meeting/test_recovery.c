#define _POSIX_C_SOURCE 200809L
#include "meeting_recovery.h"
#include <mbedtls/sha256.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=1;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;c->value=1;return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)c->value=c->value*33+p[i];return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){for(int i=0;i<32;i++)out[i]=(unsigned char)(c->value>>((i%4)*8));return 0;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
typedef struct {unsigned char data[2][MEETING_SNAPSHOT_SIZE];int exists[2],fail;} fixture;
static int rd(void *p,unsigned i,uint8_t out[MEETING_SNAPSHOT_SIZE]){fixture *f=p;if(!f->exists[i])return 1;memcpy(out,f->data[i],MEETING_SNAPSHOT_SIZE);return 0;}
static bool wr(void *p,unsigned i,const uint8_t data[MEETING_SNAPSHOT_SIZE]){fixture *f=p;if(f->fail)return false;memcpy(f->data[i],data,MEETING_SNAPSHOT_SIZE);f->exists[i]=1;return true;}
static bool stop_now(void *ctx){(void)ctx;return true;}
static void test_read_only_source(const unsigned char *bytes,size_t length,const uint8_t id[16]){
 char path[]="/tmp/meeting-readonly-XXXXXX";int fd=mkstemp(path);assert(fd>=0);
 FILE *f=fdopen(fd,"wb");assert(f&&fwrite(bytes,1,length,f)==length&&fclose(f)==0);
 f=fopen(path,"rb");assert(f);fixture store={0};meeting_snapshot_io io={&store,rd,wr};meeting_snapshot snap;
 store.fail=1;assert(meeting_recovery_prepare(f,&io,id,&snap)==MEETING_RECOVERY_STORAGE_ERROR);
 unsigned char unchanged[101];assert(fseek(f,0,SEEK_SET)==0&&fread(unchanged,1,length,f)==length&&!memcmp(unchanged,bytes,length));
 assert(!store.exists[0]&&!store.exists[1]);store.fail=0;
 assert(meeting_recovery_prepare(f,&io,id,&snap)==MEETING_RECOVERY_OK);
 assert(snap.frames==1&&snap.valid_end==100&&snap.reason==3);
 assert(meeting_recovery_prepare(f,&io,id,&snap)==MEETING_RECOVERY_OK);
 unsigned char actual[101];assert(fseek(f,0,SEEK_SET)==0&&fread(actual,1,length,f)==length&&!memcmp(actual,bytes,length));
 assert(fclose(f)==0&&unlink(path)==0);
}
static void check_projection(FILE *f,const meeting_snapshot *snapshot){
 meeting_scr1_scan_result scan={.valid_end=snapshot->valid_end,.frames=snapshot->frames};uint8_t projected[100];
 assert(meeting_scr1_read_projected(f,&scan,0,projected,sizeof(projected)));
 assert(projected[20]==1&&projected[21]==0&&projected[22]==0&&projected[23]==0);
 assert(projected[96]==3&&projected[97]==0xf8&&projected[98]==0xff&&projected[99]==0xfe);
 assert(!meeting_scr1_read_projected(f,&scan,100,projected,1));
}
static uint32_t test_crc32(const unsigned char *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void test_unknown_snapshot(FILE *source,const uint8_t id[16],const meeting_snapshot *known){
 fixture store={0};meeting_snapshot_io io={&store,rd,wr};meeting_snapshot out;
 assert(meeting_snapshot_encode(known,store.data[0]));store.exists[0]=1;
 memcpy(store.data[1],store.data[0],MEETING_SNAPSHOT_SIZE);store.data[1][77]=1;
 uint32_t crc=test_crc32(store.data[1],84);for(unsigned i=0;i<4;i++)store.data[1][84+i]=(unsigned char)(crc>>(8*i));store.exists[1]=1;
 unsigned char before[sizeof store.data];memcpy(before,store.data,sizeof before);
 assert(meeting_recovery_prepare(source,&io,id,&out)==MEETING_RECOVERY_STORAGE_ERROR);
 assert(meeting_closed_prepare(source,&io,id,known->reason,&out)==MEETING_RECOVERY_STORAGE_ERROR);
 assert(!memcmp(before,store.data,sizeof before) && store.exists[0] && store.exists[1]);
}
int main(void){unsigned char b[101]={0},id[16]={1},other[16]={2};memcpy(b,"SCR1",4);b[4]=1;memset(b+20,255,4);b[96]=3;b[97]=0xf8;b[98]=0xff;b[99]=0xfe;b[100]=3;test_read_only_source(b,sizeof b,id);FILE *f=tmpfile();assert(f&&fwrite(b,1,sizeof b,f)==sizeof b&&fflush(f)==0);fixture store={0};meeting_snapshot_io io={&store,rd,wr};meeting_snapshot result;assert(meeting_recovery_prepare_cancel(f,&io,id,&result,stop_now,NULL)==MEETING_RECOVERY_SOURCE_ERROR);assert(!store.exists[0]&&!store.exists[1]);store.fail=1;assert(meeting_recovery_prepare(f,&io,id,&result)==MEETING_RECOVERY_STORAGE_ERROR);store.fail=0;assert(meeting_recovery_prepare(f,&io,id,&result)==MEETING_RECOVERY_OK);assert(result.frames==1&&result.valid_end==100&&result.reason==3);test_unknown_snapshot(f,id,&result);check_projection(f,&result);unsigned char original[sizeof b];assert(fseek(f,0,SEEK_SET)==0&&fread(original,1,sizeof original,f)==sizeof original&&!memcmp(original,b,sizeof b));assert(meeting_recovery_prepare(f,&io,id,&result)==MEETING_RECOVERY_OK);assert(meeting_recovery_prepare(f,&io,other,&result)==MEETING_RECOVERY_ID_CONFLICT);meeting_snapshot altered=result;altered.reason=0;assert(meeting_snapshot_encode(&altered,store.data[0]));assert(meeting_recovery_prepare(f,&io,id,&result)==MEETING_RECOVERY_SOURCE_ERROR);altered.reason=3;assert(meeting_snapshot_encode(&altered,store.data[0]));assert(fseek(f,12,SEEK_SET)==0&&fputc(42,f)!=EOF&&fflush(f)==0);assert(meeting_recovery_prepare(f,&io,id,&result)==MEETING_RECOVERY_SOURCE_ERROR);fclose(f);return 0;}
