#define _POSIX_C_SOURCE 200809L
#include "meeting_snapshot_file.h"
#include <mbedtls/sha256.h>
#include <assert.h>
#include <string.h>
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=1;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;c->value=1;return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)c->value=c->value*33+p[i];return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){for(int i=0;i<32;i++)out[i]=(unsigned char)(c->value>>((i%4)*8));return 0;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
static bool cancel_after(void *p){int *n=p;return --*n<=0;}
static void large_scan_hash(void){
 enum {FRAMES=80,SIZE=96+FRAMES*161};unsigned char b[SIZE];memset(b,0,sizeof b);memcpy(b,"SCR1",4);b[4]=1;memset(b+20,255,4);
 for(unsigned i=0;i<FRAMES;i++){size_t p=96+i*161;b[p]=160;b[p+1]=0xf8;memset(b+p+2,(int)i,159);}
 FILE *f=tmpfile();assert(f&&fwrite(b,1,sizeof b,f)==sizeof b&&fflush(f)==0);meeting_snapshot s={.frames=FRAMES,.valid_end=SIZE};
 assert(meeting_snapshot_source_hash(f,s.source_sha256)&&meeting_snapshot_matches_file(f,&s));
 assert(fseek(f,96+40*161+8,SEEK_SET)==0&&fputc(42,f)!=EOF&&fflush(f)==0);
 assert(!meeting_snapshot_matches_file(f,&s));assert(fclose(f)==0);
}
int main(void){large_scan_hash();unsigned char b[100]={0};memcpy(b,"SCR1",4);b[4]=1;b[20]=1;b[96]=3;b[97]=0xf8;b[98]=0xff;b[99]=0xfe;FILE *f=tmpfile();assert(f);assert(fwrite(b,1,sizeof b,f)==sizeof b);assert(fflush(f)==0);meeting_snapshot s={.frames=1,.valid_end=100};assert(meeting_snapshot_source_hash(f,s.source_sha256));assert(meeting_snapshot_matches_file(f,&s));meeting_scr1_scan_result scan;assert(meeting_scr1_scan_file(f,&scan));int budget=2;meeting_scr1_scan_result rejected={0};assert(!meeting_scr1_scan_file_cancel(f,&rejected,cancel_after,&budget));budget=1;uint8_t unchanged[32];memset(unchanged,0x5a,32);assert(!meeting_snapshot_source_hash_cancel(f,unchanged,cancel_after,&budget));for(int i=0;i<32;i++)assert(unchanged[i]==0x5a);budget=1;assert(!meeting_projection_digest_cancel(f,&scan,0,scan.valid_end,unchanged,cancel_after,&budget));uint8_t projected[32],again[32],part[32];assert(meeting_projection_digest(f,&scan,0,scan.valid_end,projected));assert(meeting_projection_digest(f,&scan,0,scan.valid_end,again));assert(!memcmp(projected,again,32));assert(meeting_projection_digest(f,&scan,20,12,part));assert(memcmp(projected,part,32));assert(!meeting_projection_digest(f,&scan,scan.valid_end,1,part));assert(fseek(f,12,SEEK_SET)==0);assert(fputc(1,f)!=EOF);assert(fflush(f)==0);assert(!meeting_snapshot_matches_file(f,&s));assert(fclose(f)==0);return 0;}
