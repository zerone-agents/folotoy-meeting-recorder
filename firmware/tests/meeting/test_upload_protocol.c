#include "meeting_upload_protocol.h"
#include "meeting_receipt_json.h"
#include <assert.h>
#include <string.h>
static meeting_manifest expected;static int calls,stop_after,fail_at,bad_receipt,resume_mode,chunk_reads,supported_mode;
static unsigned progress_events;
static void progress(void *ctx,uint32_t next){(void)ctx;(void)next;progress_events++;}
bool meeting_upload_manifest(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,meeting_manifest *out){(void)f;(void)s;(void)ht;(void)rt;(void)trust;*out=expected;return true;}
bool meeting_upload_manifest_cancel(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,meeting_manifest *out,meeting_cancel_fn cancelled,void *ctx){if(cancelled&&cancelled(ctx))return false;return meeting_upload_manifest(f,s,ht,rt,trust,out);}
bool meeting_upload_chunk(FILE *f,const meeting_snapshot *s,uint32_t seq,uint8_t *buf,size_t cap,size_t *len,uint8_t hash[32]){(void)f;(void)s;(void)seq;chunk_reads++;assert(cap>=3);memcpy(buf,"abc",3);memset(hash,7,32);*len=3;return true;}
bool meeting_receipt_json_matches(const char *json,size_t len,int status,const meeting_manifest *m){(void)json;(void)len;(void)status;(void)m;return !bad_receipt;}
bool meeting_receipt_json_integrity_error(const char *json,size_t len,int status){(void)json;(void)len;(void)status;return false;}
bool meeting_repair_json_parse(const char *json,size_t len,int status,const meeting_manifest *m,uint8_t *bitmap,size_t size)
{(void)json;(void)len;(void)status;(void)m;(void)bitmap;(void)size;return false;}
bool meeting_chunk_window_json_parse(const char *j,size_t n,int status,const char *id,uint32_t first,meeting_remote_chunk entries[16]){(void)j;(void)n;(void)status;(void)id;(void)first;memset(entries,0,16*sizeof(*entries));return supported_mode;}
bool meeting_legacy_chunk_window_json_parse(const char *j,size_t n,int status,const char *id,uint32_t first,meeting_remote_chunk entries[16]){(void)j;(void)n;(void)status;(void)id;(void)first;(void)entries;return false;}
static bool cancelled(void *ctx){(void)ctx;return stop_after && calls>=stop_after;}
static bool request(void *ctx,const char *method,const char *path,const char *type,const char *sha,const uint8_t *body,size_t body_len,int *status,char *response,size_t cap,size_t *len){(void)ctx;(void)type;(void)body;(void)body_len;assert(cap>=2);if(!strcmp(method,"GET")){*status=supported_mode?200:404;*len=0;return true;}calls++;if(calls==fail_at)return false;if(calls==1)assert(!strcmp(method,"POST")&&strstr(path,"/meetings/"));if(calls==2&&!resume_mode)assert(!strcmp(method,"PUT")&&sha&&strlen(sha)==64);if(calls==2&&resume_mode)assert(!strcmp(method,"POST")&&strstr(path,"/finalize"));*status=calls==1?201:200;memcpy(response,"{}",2);*len=2;return true;}
int main(void){memcpy(expected.id,"12345678-1234-4123-8123-123456789abc",37);expected.count=1;expected.bytes=100;expected.reason=3;memset(expected.digest,7,32);meeting_upload_transport t={NULL,cancelled,request};meeting_snapshot s={0};FILE *f=tmpfile();assert(f);supported_mode=1;stop_after=2;assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_PAUSED&&chunk_reads==1);stop_after=0;resume_mode=1;calls=0;assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_CONFIRMED&&calls==2&&chunk_reads==1);resume_mode=0;supported_mode=0;calls=0;meeting_manifest forged=expected;forged.digest[0]^=1;assert(meeting_upload_run(f,&s,&forged,&t)==MEETING_UPLOAD_FAILED&&calls==0);assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_CONFIRMED&&calls==3);calls=0;stop_after=1;assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_PAUSED&&calls==1);calls=0;stop_after=0;fail_at=3;assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_FAILED);calls=0;fail_at=0;bad_receipt=1;expected.id[35]='d';assert(meeting_upload_run(f,&s,&expected,&t)==MEETING_UPLOAD_FAILED);
 /* A legacy server can reply 200 for an old block after reboot even if its
  * inventory endpoint is unavailable. That is not fresh progress. */
 calls=0;bad_receipt=0;expected.id[35]='e';progress_events=0;
 assert(meeting_upload_run_observed(f,&s,&expected,&t,progress,NULL)==MEETING_UPLOAD_CONFIRMED);
 assert(progress_events==0);
 fclose(f);return 0;}
