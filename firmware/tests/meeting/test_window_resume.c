#include "meeting_upload_protocol.h"
#include "meeting_receipt_json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static meeting_manifest expected;
static meeting_remote_chunk server[16];
static unsigned put_count[3];
static int lose_reply,simulate_server_loss;
static unsigned events;static uint32_t last_next;
static void progress(void *ctx,uint32_t next){(void)ctx;events++;last_next=next;}
bool meeting_upload_manifest_cancel(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,meeting_manifest *out,meeting_cancel_fn cancel,void *ctx)
{(void)f;(void)s;(void)ht;(void)rt;(void)trust;if(cancel(ctx))return false;*out=expected;return true;}
bool meeting_upload_chunk(FILE *f,const meeting_snapshot *s,uint32_t seq,uint8_t *buf,size_t cap,size_t *len,uint8_t hash[32])
{(void)f;(void)s;assert(seq<3 && cap>=3);memcpy(buf,"abc",3);memset(hash,(int)seq+1,32);*len=3;return true;}
bool meeting_chunk_window_json_parse(const char *j,size_t n,int status,const char *id,uint32_t first,meeting_remote_chunk entries[16])
{(void)j;(void)n;assert(status==200 && !strcmp(id,expected.id) && first==0);memcpy(entries,server,sizeof server);return true;}
bool meeting_legacy_chunk_window_json_parse(const char *j,size_t n,int status,const char *id,uint32_t first,meeting_remote_chunk entries[16])
{(void)j;(void)n;(void)status;(void)id;(void)first;(void)entries;return false;}
bool meeting_receipt_json_matches(const char *j,size_t n,int status,const meeting_manifest *m)
{(void)j;(void)n;(void)m;return status==200;}
bool meeting_receipt_json_integrity_error(const char *j,size_t n,int status){(void)j;(void)n;return status==409;}
bool meeting_repair_json_parse(const char *j,size_t n,int status,const meeting_manifest *m,uint8_t *bitmap,size_t size)
{(void)j;(void)n;(void)m;if(status!=200||!size)return false;memset(bitmap,0,size);bitmap[0]=1;return true;}
static bool cancelled(void *ctx){(void)ctx;return false;}
static bool request(void *ctx,const char *method,const char *path,const char *type,const char *sha,const uint8_t *body,size_t body_len,int *status,char *response,size_t cap,size_t *len)
{
 (void)ctx;(void)type;(void)sha;(void)body;(void)body_len;assert(cap>=2);memcpy(response,"{}",2);*len=2;*status=200;
 if(!strcmp(method,"GET")){assert(strstr(path,"chunks-window/0")||strstr(path,"repair-status"));return true;}
 if(!strcmp(method,"POST")&&strstr(path,"/finalize")&&simulate_server_loss&&!server[0].valid){*status=409;simulate_server_loss=0;return true;}
 if(!strcmp(method,"PUT")){unsigned seq=99;assert(sscanf(strrchr(path,'/')+1,"%u",&seq)==1 && seq<3);put_count[seq]++;
  *status=server[seq].present?200:201;
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
 lose_reply=1;assert(meeting_upload_run_observed(f,&snapshot,&expected,&t,progress,NULL)==MEETING_UPLOAD_FAILED);
 assert(!put_count[0] && put_count[1]==1 && !put_count[2]);
 assert(events==0);
 assert(meeting_upload_run_observed(f,&snapshot,&expected,&t,progress,NULL)==MEETING_UPLOAD_CONFIRMED);
 assert(events==2&&last_next==3);
 assert(!put_count[0] && put_count[1]==1 && put_count[2]==1);
 server[1].digest[0]^=1;assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_FAILED);
 assert(put_count[1]==1);server[1].digest[0]^=1;server[1].valid=false;
 assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_CONFIRMED && put_count[1]==2);
 /* A server that loses a previously acknowledged block must reject finalize
  * and request repair even if the in-memory resume hint skips that block. */
 memset(server,0,sizeof server);memset(put_count,0,sizeof put_count);
 server[0].present=server[0].valid=true;server[0].size=3;memset(server[0].digest,1,32);
 lose_reply=1;assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_FAILED);
 server[0].present=server[0].valid=false;simulate_server_loss=1;
 assert(meeting_upload_run(f,&snapshot,&expected,&t)==MEETING_UPLOAD_CONFIRMED);
 assert(put_count[0]==1&&server[0].valid);
 fclose(f);puts("window resume response-loss tests: PASS");
}
