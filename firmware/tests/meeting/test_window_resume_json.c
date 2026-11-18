#include "meeting_upload_protocol.h"
#include "meeting_receipt_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static meeting_manifest expected;
static meeting_remote_chunk server[16];
static unsigned put_count[3];
static int lose_reply;
bool meeting_upload_manifest_cancel(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,meeting_manifest *out,meeting_cancel_fn cancel,void *ctx)
{(void)f;(void)s;(void)ht;(void)rt;(void)trust;if(cancel(ctx))return false;*out=expected;return true;}
bool meeting_upload_chunk(FILE *f,const meeting_snapshot *s,uint32_t seq,uint8_t *buf,size_t cap,size_t *len,uint8_t hash[32])
{(void)f;(void)s;assert(seq<3 && cap>=3);memcpy(buf,"abc",3);memset(hash,(int)seq+1,32);*len=3;return true;}
static bool cancelled(void *ctx){(void)ctx;return false;}
static bool request(void *ctx,const char *method,const char *path,const char *type,const char *sha,const uint8_t *body,size_t body_len,int *status,char *response,size_t cap,size_t *len)
{
 (void)ctx;(void)type;(void)sha;(void)body;(void)body_len;assert(cap>=2);memcpy(response,"{}",2);*len=2;*status=200;
 if(!strcmp(method,"GET")){
  assert(strstr(path,"chunks-window/0"));size_t used=0;
  int k=snprintf(response,cap,"{\"id\":\"%s\",\"first\":0,\"chunks\":[",expected.id);assert(k>0&&(size_t)k<cap);used=(size_t)k;
  bool comma=false;for(unsigned i=0;i<3;i++)if(server[i].present){
   char hex[65];for(unsigned j=0;j<32;j++)snprintf(hex+j*2,3,"%02x",server[i].digest[j]);
   k=snprintf(response+used,cap-used,"%s{\"seq\":%u,\"size\":%u,\"digest\":\"%s\",\"valid\":%s}",comma?",":"",i,server[i].size,hex,server[i].valid?"true":"false");
   assert(k>0&&(size_t)k<cap-used);used+=(size_t)k;comma=true;
  } assert(used+2<cap);response[used++]=']';response[used++]='}';*len=used;return true;
 }
 if(!strcmp(method,"POST") && strstr(path,"/finalize")){
  int k=snprintf(response,cap,"{\"id\":\"%s\",\"committed\":true,\"manifest\":{\"count\":3,\"bytes\":9,\"digest\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"reason\":\"interrupted\",\"recordedAt\":null,\"timeTrust\":\"uncalibrated\"}}",expected.id);
  assert(k>0&&(size_t)k<cap);*len=(size_t)k;return true;
 }
 if(!strcmp(method,"PUT")){unsigned seq=99;assert(sscanf(strrchr(path,'/')+1,"%u",&seq)==1 && seq<3);put_count[seq]++;
  server[seq].present=true;server[seq].valid=true;server[seq].size=3;memset(server[seq].digest,(int)seq+1,32);
  if(lose_reply && seq==1){lose_reply=0;return false;}
 }
 return true;
}
int main(void)
{
 memcpy(expected.id,"12345678-1234-4123-8123-123456789abc",37);expected.count=3;expected.bytes=9;expected.reason=3;
 server[0].present=server[0].valid=true;server[0].size=3;memset(server[0].digest,1,32);
 meeting_upload_transport t={NULL,cancelled,request};meeting_snapshot snapshot={0};FILE *f=tmpfile();assert(f);
 lose_reply=1;assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_FAILED);
 assert(!put_count[0] && put_count[1]==1 && !put_count[2]);
 assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_CONFIRMED);
 assert(!put_count[0] && put_count[1]==1 && put_count[2]==1);
 server[1].digest[0]^=1;assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_FAILED);
 assert(put_count[1]==1);server[1].digest[0]^=1;server[1].valid=false;
 assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_CONFIRMED && put_count[1]==2);
 fclose(f);puts("window resume real JSON response-loss tests: PASS");
}
