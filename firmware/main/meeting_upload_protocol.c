#include "meeting_upload_protocol.h"
#include "meeting_receipt_json.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_log.h"
static const char *TAG="upload_protocol";

static void hex32(const uint8_t hash[32],char out[65]){
 static const char h[]="0123456789abcdef";for(unsigned i=0;i<32;i++){out[i*2]=h[hash[i]>>4];out[i*2+1]=h[hash[i]&15];}out[64]='\0';
}
static bool same_manifest(const meeting_manifest *a,const meeting_manifest *b){return meeting_receipt_matches(a,b,true);}
/* Volatile optimisation: only consecutive acknowledged blocks are skipped.
 * The hint is bound to meeting ID and whole digest. Finalize still validates
 * every block and the whole digest; any 409 invalidates the hint. Reboot
 * loses hints safely, and a stale hint is repaired by the server protocol. */
typedef struct {char id[37];uint8_t digest[32];uint32_t next;uint32_t pending_seq;bool pending;} legacy_hint;
static legacy_hint hints[16];static unsigned hint_next;static atomic_flag hint_lock=ATOMIC_FLAG_INIT;
static void lock_hints(void){while(atomic_flag_test_and_set_explicit(&hint_lock,memory_order_acquire)) {}}
static void unlock_hints(void){atomic_flag_clear_explicit(&hint_lock,memory_order_release);}
static legacy_hint *find_hint(const meeting_manifest *m,bool create){
 for(unsigned i=0;i<16;i++)if(!strcmp(hints[i].id,m->id)&&!memcmp(hints[i].digest,m->digest,32))return &hints[i];
 if(!create)return NULL;
 legacy_hint *h=&hints[hint_next++%16];memset(h,0,sizeof(*h));memcpy(h->id,m->id,sizeof(h->id));memcpy(h->digest,m->digest,32);return h;
}
static uint32_t hint_read(const meeting_manifest *m){lock_hints();legacy_hint *h=find_hint(m,false);uint32_t next=h?h->next:0;unlock_hints();return next;}
static bool hint_attempt(const meeting_manifest *m,uint32_t seq){lock_hints();legacy_hint *h=find_hint(m,true);bool retry=h->pending&&h->pending_seq==seq;h->pending=true;h->pending_seq=seq;unlock_hints();return retry;}
static bool hint_ack(const meeting_manifest *m,uint32_t seq){lock_hints();legacy_hint *h=find_hint(m,true);bool was_pending=h->pending&&h->pending_seq==seq;if(was_pending)h->pending=false;if(h->next==seq)h->next++;unlock_hints();return was_pending;}
static void hint_clear(const meeting_manifest *m){lock_hints();legacy_hint *h=find_hint(m,false);if(h)memset(h,0,sizeof(*h));unlock_hints();}
meeting_upload_result meeting_upload_run(FILE *source,const meeting_snapshot *snapshot,
                                          const meeting_manifest *manifest,const meeting_upload_transport *t)
{
 if(!source||!snapshot||!manifest||!t||!t->request||!t->cancelled)return MEETING_UPLOAD_FAILED;
 if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
 meeting_manifest check;
 if(!meeting_upload_manifest_cancel(source,snapshot,manifest->recorded_at_present,manifest->recorded_at,manifest->time_trust,&check,t->cancelled,t->ctx)||!same_manifest(manifest,&check)){ESP_LOGW(TAG,"manifest verification failed");return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;}
 return meeting_upload_run_prevalidated(source,snapshot,manifest,t);
}
meeting_upload_result meeting_upload_run_prevalidated(FILE *source,const meeting_snapshot *snapshot,
                                          const meeting_manifest *manifest,const meeting_upload_transport *t)
{
 return meeting_upload_run_observed(source,snapshot,manifest,t,NULL,NULL);
}
meeting_upload_result meeting_upload_run_observed(FILE *source,const meeting_snapshot *snapshot,
                                          const meeting_manifest *manifest,const meeting_upload_transport *t,
                                          meeting_upload_progress_fn progress,void *progress_ctx)
{
 if(!source||!snapshot||!manifest||!t||!t->request||!t->cancelled)return MEETING_UPLOAD_FAILED;
 if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
 ESP_LOGI(TAG,"manifest verified; starting transfer");
 char path[100];uint8_t io_buffer[MEETING_UPLOAD_CHUNK_SIZE+1];
 char *response=(char *)io_buffer;uint8_t *block=io_buffer;size_t response_len;int status;
 int n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s",manifest->id);
 if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
 if(!t->request(t->ctx,"POST",path,"application/json",NULL,NULL,0,&status,response,sizeof(io_buffer)-1,&response_len)){ESP_LOGW(TAG,"create request failed");return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;}
 if(status!=200&&status!=201){ESP_LOGW(TAG,"create status %d",status);return MEETING_UPLOAD_FAILED;}
 uint8_t digest[32];char hex[65];
 meeting_remote_chunk remote[16];bool windows_supported=true,legacy_unavailable=false,window_loaded=false;
 for(uint32_t seq=0;seq<manifest->count;seq++){
  if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
  if(seq<hint_read(manifest)){
   if(seq%16==0)memset(remote,0,sizeof(remote));
   continue;
  }
  if(!window_loaded||seq%16==0){
   uint32_t first=seq-(seq%16);
   window_loaded=true;
   if(!windows_supported&&legacy_unavailable){memset(remote,0,sizeof(remote));goto window_ready;}
   n=windows_supported?snprintf(path,sizeof(path),"/meeting-service/meetings/%s/chunks-window/%" PRIu32,manifest->id,first):
                       snprintf(path,sizeof(path),"/meeting-service/meetings/%s",manifest->id);
   if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
   bool received=t->request(t->ctx,"GET",path,NULL,NULL,NULL,0,&status,response,sizeof(io_buffer)-1,&response_len);
   if(windows_supported&&received&&status==404){
    windows_supported=false;
    n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s",manifest->id);
    if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
    received=t->request(t->ctx,"GET",path,NULL,NULL,NULL,0,&status,response,sizeof(io_buffer)-1,&response_len);
   }
   if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
   if(windows_supported){if(!received||!meeting_chunk_window_json_parse(response,response_len,status,manifest->id,first,remote)){ESP_LOGW(TAG,"chunk window failed");return MEETING_UPLOAD_FAILED;}}
   else if(!received||!meeting_legacy_chunk_window_json_parse(response,response_len,status,manifest->id,first,remote)){memset(remote,0,sizeof(remote));legacy_unavailable=true;}
  }
window_ready:
  if(seq<hint_read(manifest))continue;
  size_t size;if(!meeting_upload_chunk(source,snapshot,seq,block,MEETING_UPLOAD_CHUNK_SIZE,&size,digest))return MEETING_UPLOAD_FAILED;
  meeting_remote_chunk *entry=&remote[seq%16];
  if(entry->present){
   if(entry->size!=size||memcmp(entry->digest,digest,32)){ESP_LOGW(TAG,"chunk %lu: remote digest/size mismatch",(unsigned long)seq);return MEETING_UPLOAD_FAILED;}
   if(entry->valid){if(hint_ack(manifest,seq)&&progress)progress(progress_ctx,seq+1);continue;}
  }
  hex32(digest,hex);n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/chunks/%" PRIu32,manifest->id,seq);
  if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
  bool retry_after_uncertain_response=hint_attempt(manifest,seq);
  if(!t->request(t->ctx,"PUT",path,"application/octet-stream",hex,block,size,&status,response,sizeof(io_buffer)-1,&response_len)){ESP_LOGW(TAG,"chunk %lu: request failed",(unsigned long)seq);return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;}
  if(status!=200&&status!=201){ESP_LOGW(TAG,"chunk %lu: status %d",(unsigned long)seq,status);return MEETING_UPLOAD_FAILED;}
  if(hint_ack(manifest,seq)&&progress&&(status==201||retry_after_uncertain_response))progress(progress_ctx,seq+1);
 }
 if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
 static const char *reasons[]={"user","limit","storage","interrupted"},*trusts[]={"uncalibrated","unverified","trusted"};
 if(manifest->reason>3||manifest->time_trust>2)return MEETING_UPLOAD_FAILED;
 char time[24],body[320],whole[65];hex32(manifest->digest,whole);
 if(manifest->recorded_at_present)n=snprintf(time,sizeof(time),"%" PRId64,manifest->recorded_at);else n=snprintf(time,sizeof(time),"null");
 if(n<0||(size_t)n>=sizeof(time))return MEETING_UPLOAD_FAILED;
 n=snprintf(body,sizeof(body),"{\"count\":%" PRIu32 ",\"bytes\":%" PRIu64 ",\"digest\":\"%s\",\"reason\":\"%s\",\"recordedAt\":%s,\"timeTrust\":\"%s\"}",manifest->count,manifest->bytes,whole,reasons[manifest->reason],time,trusts[manifest->time_trust]);
 if(n<0||(size_t)n>=sizeof(body))return MEETING_UPLOAD_FAILED;
 int body_len=n;n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/finalize",manifest->id);if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
 if(!t->request(t->ctx,"POST",path,"application/json",NULL,(const uint8_t*)body,(size_t)body_len,&status,response,sizeof(io_buffer)-1,&response_len)){ESP_LOGW(TAG,"finalize request failed");return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;}
 if(response_len>sizeof(io_buffer)-1)return MEETING_UPLOAD_FAILED;
 if(meeting_receipt_json_matches(response,response_len,status,manifest)){hint_clear(manifest);return MEETING_UPLOAD_CONFIRMED;}
 if(status==409)hint_clear(manifest);
 if(!meeting_receipt_json_integrity_error(response,response_len,status)){ESP_LOGW(TAG,"finalize receipt mismatch, status %d",status);return MEETING_UPLOAD_FAILED;}
 n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/repair-status",manifest->id);
 if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
 if(!t->request(t->ctx,"GET",path,NULL,NULL,NULL,0,&status,response,sizeof(io_buffer)-1,&response_len))
  return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;
 uint8_t damaged[1024];
 if(!meeting_repair_json_parse(response,response_len,status,manifest,damaged,sizeof(damaged)))return MEETING_UPLOAD_FAILED;
 for(uint32_t seq=0;seq<manifest->count;seq++){
  if(!(damaged[seq/8]&(1u<<(seq%8))))continue;
  if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
  size_t size;if(!meeting_upload_chunk(source,snapshot,seq,block,MEETING_UPLOAD_CHUNK_SIZE,&size,digest))return MEETING_UPLOAD_FAILED;
  hex32(digest,hex);n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/chunks/%" PRIu32,manifest->id,seq);
  if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
  if(!t->request(t->ctx,"PUT",path,"application/octet-stream",hex,block,size,&status,response,sizeof(io_buffer)-1,&response_len))
   return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;
  if(status!=200&&status!=201)return MEETING_UPLOAD_FAILED;
 }
 if(t->cancelled(t->ctx))return MEETING_UPLOAD_PAUSED;
 n=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/finalize",manifest->id);
 if(n<0||(size_t)n>=sizeof(path))return MEETING_UPLOAD_FAILED;
 if(!t->request(t->ctx,"POST",path,"application/json",NULL,(const uint8_t*)body,(size_t)body_len,&status,response,sizeof(io_buffer)-1,&response_len))
  return t->cancelled(t->ctx)?MEETING_UPLOAD_PAUSED:MEETING_UPLOAD_FAILED;
 return meeting_receipt_json_matches(response,response_len,status,manifest)?MEETING_UPLOAD_CONFIRMED:MEETING_UPLOAD_FAILED;
}
