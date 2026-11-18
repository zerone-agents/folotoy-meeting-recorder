#define _POSIX_C_SOURCE 200809L
#include "meeting_upload.h"
#include <mbedtls/sha256.h>
#include <assert.h>
#include <string.h>
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=1;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;c->value=1;return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)c->value=c->value*33+p[i];return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){for(int i=0;i<32;i++)out[i]=(unsigned char)(c->value>>((i%4)*8));return 0;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
static void test_multichunk(void){
    FILE *f=tmpfile();assert(f);
    unsigned char header[96]={0},frame[100];memcpy(header,"SCR1",4);header[4]=1;
    memset(header+24,0xa5,48);assert(fwrite(header,1,sizeof header,f)==sizeof header);
    for(unsigned i=0;i<90;i++){
        unsigned char len=100;memset(frame,(int)i,sizeof frame);
        assert(fwrite(&len,1,1,f)==1&&fwrite(frame,1,sizeof frame,f)==sizeof frame);
    }
    assert(fflush(f)==0);
    meeting_snapshot s={.frames=90,.valid_end=96+90*101};
    meeting_scr1_scan_result scan={.frames=s.frames,.valid_end=s.valid_end};
    unsigned char buffer[MEETING_UPLOAD_CHUNK_SIZE],actual[32],reference[32];
    for(unsigned seq=0;seq<3;seq++){
        size_t n=0;assert(meeting_upload_chunk(f,&s,seq,buffer,sizeof buffer,&n,actual));
        assert(meeting_projection_digest(f,&scan,(uint64_t)seq*MEETING_UPLOAD_CHUNK_SIZE,n,reference));
        assert(!memcmp(actual,reference,sizeof actual));
        if(seq==2)assert(n<MEETING_UPLOAD_CHUNK_SIZE);
    }
    fclose(f);
}
int main(void){test_multichunk();unsigned char b[100]={0},buf[100],digest[32];memcpy(b,"SCR1",4);b[4]=1;b[20]=1;memset(b+24,0xa5,48);b[96]=3;b[97]=0xf8;b[98]=0xff;b[99]=0xfe;FILE *f=tmpfile();assert(f&&fwrite(b,1,sizeof b,f)==sizeof b&&fflush(f)==0);meeting_snapshot s={.frames=1,.valid_end=100,.reason=3};memset(s.meeting_id,1,16);assert(meeting_snapshot_source_hash(f,s.source_sha256));meeting_manifest m;assert(meeting_upload_manifest(f,&s,false,0,0,&m));assert(m.count==1&&m.bytes==100&&m.reason==3);size_t len=0;assert(meeting_upload_chunk(f,&s,0,buf,sizeof buf,&len,digest));assert(len==100&&!memcmp(m.digest,digest,32));for(int i=24;i<72;i++)assert(buf[i]==0);assert(!meeting_upload_chunk(f,&s,1,buf,sizeof buf,&len,digest));assert(!meeting_upload_chunk(f,&s,0,buf,99,&len,digest));assert(!meeting_upload_manifest(f,&s,false,0,2,&m));fclose(f);return 0;}
