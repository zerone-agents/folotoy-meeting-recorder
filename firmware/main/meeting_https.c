#include "meeting_https.h"
#include "meeting_dns.h"
#include "meeting_receipt_json.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "sdkconfig.h"
#if CONFIG_MEETING_SUPERVISED_TEST_PREEMPT_CHUNK
#include "meeting_commission.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
static atomic_bool s_test_preempt_ready;
bool meeting_https_test_preempt_ready(void){return atomic_load(&s_test_preempt_ready);}
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_UPLOAD_INTERRUPTS
#include "meeting_commission.h"
#endif
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>
static const char *TAG="meeting_https";
static const int64_t request_timeout_us=15000000;
static const int64_t file_timeout_us=180000000;
#define FILE_UPLOAD_BUFFER_SIZE 4096u

typedef struct {
 FILE *source;
 const meeting_manifest *manifest;
 meeting_scr1_scan_result scan;
 int64_t deadline, started, dns_done, open_started, open_done, write_done;
 size_t written;
 bool response_mode_present;
 bool retry_original_ca;
} file_upload;

static bool origin_hostname(const char *s,char host[181])
{
 if(!s||strncmp(s,"https://",8))return false;
 const char *start=s+8,*p=start;bool label=false;
 while(*p&&*p!=':'){
  if(*p=='.'){if(!label||p[-1]=='-')return false;label=false;}
  else if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9'))label=true;
  else if(*p!='-'||!label)return false;
  p++;
 }
 size_t n=(size_t)(p-start);if(!label||p[-1]=='-'||n>180)return false;
 if(*p==':'){unsigned port=0,digits=0;for(p++;*p;p++){if(*p<'0'||*p>'9'||++digits>5)return false;port=port*10u+(unsigned)(*p-'0');}if(!digits||!port||port>65535)return false;}
 if(host){memcpy(host,start,n);host[n]=0;}
 return true;
}
static bool token_valid(const char *s)
{
 if(!s)return false;
 size_t n=strlen(s);
 if(n<32||n>512)return false;
 for(size_t i=0;i<n;i++)if((unsigned char)s[i]<=32||(unsigned char)s[i]>=127)return false;
 return true;
}
/* A clock can become invalid after a request has begun (for example after a
 * wall-clock adjustment or a failed floor checkpoint). Never accept a late
 * response as an upload confirmation in that case. */
static bool cancelled(void *ctx){meeting_https_config *c=ctx;return c->is_cancelled(c->cancel_ctx)||!c->time_valid(c->cancel_ctx);}
static bool clear_header(esp_http_client_handle_t c,const char *name)
{
 esp_err_t result=esp_http_client_delete_header(c,name);return result==ESP_OK||result==ESP_ERR_NOT_FOUND;
}

static esp_err_t file_response_event(esp_http_client_event_t *event)
{
 if (event->event_id==HTTP_EVENT_ON_HEADER && event->user_data && event->header_key &&
     !strcasecmp(event->header_key,"X-Meeting-Upload-Mode")) {
  file_upload *upload=event->user_data;
  upload->response_mode_present=true;
 }
 return ESP_OK;
}

static int64_t progress_limit(int64_t now,int64_t deadline)
{
 return deadline-now>request_timeout_us?now+request_timeout_us:deadline;
}

static bool file_headers(esp_http_client_handle_t client,const meeting_manifest *manifest)
{
 static const char *reasons[]={"user","limit","storage","interrupted"};
 static const char *trust[]={"uncalibrated","unverified","trusted"};
 if (esp_http_client_set_header(client,"X-Meeting-End-Reason",reasons[manifest->reason])!=ESP_OK ||
     esp_http_client_set_header(client,"X-Meeting-Time-Trust",trust[manifest->time_trust])!=ESP_OK) return false;
 if (!manifest->recorded_at_present) return true;
 char recorded_at[24];
 int length=snprintf(recorded_at,sizeof(recorded_at),"%" PRId64,manifest->recorded_at);
 return length>0 && (size_t)length<sizeof(recorded_at) &&
     esp_http_client_set_header(client,"X-Meeting-Recorded-At",recorded_at)==ESP_OK;
}

static bool request_internal(void *ctx,const char *method,const char *path,const char *type,const char *sha,
    const uint8_t *body,size_t body_len,int *status,char *response,size_t cap,size_t *response_len,
    file_upload *upload)
{
 meeting_https_config *c=ctx;
 if(!origin_hostname(c->origin,NULL)||!token_valid(c->device_token)||!c->ca_pem||!c->ca_pem[0]||!c->time_valid(c->cancel_ctx)||cancelled(c)||!method||!path||strncmp(path,"/meeting-service/meetings/",26)||strchr(path,'?')||strchr(path,'#')||!status||!response||!response_len||body_len>INT_MAX||(!body&&body_len&&!upload))return false;
 const int64_t started=esp_timer_get_time();
 if(started<0||started>INT64_MAX-file_timeout_us)return false;
 const int64_t dns_deadline=started+request_timeout_us;
 const int64_t deadline=upload?upload->deadline:dns_deadline;
 if (upload) upload->started=started;
 if (started>=deadline) return false;
 char ip[16];
 if(!meeting_dns_resolve(c->tls_hostname,cancelled,c,dns_deadline<deadline?dns_deadline:deadline,ip))return false;
 if (upload) upload->dns_done=esp_timer_get_time();
 char url[300],authorization[520];int n=snprintf(url,sizeof(url),"https://%s%s%s",ip,c->origin+8+strlen(c->tls_hostname),path);if(n<0||(size_t)n>=sizeof(url))return false;
 n=snprintf(authorization,sizeof(authorization),"Bearer %s",c->device_token);if(n<0||(size_t)n>=sizeof(authorization))return false;
 size_t heap_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL),largest_before=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
 esp_http_client_config_t cfg={.url=url,.cert_pem=c->ca_pem,.timeout_ms=3000,.disable_auto_redirect=true,.skip_cert_common_name_check=false,.common_name=c->tls_hostname};
 if (upload) {
  cfg.event_handler=file_response_event;
  cfg.user_data=upload;
 }
 esp_http_client_handle_t client=c->client;
 if(client){if(esp_http_client_set_url(client,url)!=ESP_OK){meeting_https_close(c);return false;}}
 else {client=esp_http_client_init(&cfg);if(!client)return false;c->client=client;}
 bool ok=false;const char *stage="method",*reason="operation";size_t received=0;int read_result=0;esp_http_client_method_t verb;
 /* TLS setup and several bounded I/O operations share this budget.  A
  * single 3 s socket timeout remains in force so recording can preempt at
  * operation boundaries; allow cumulative handshake and response time. */
 if(!strcmp(method,"POST"))verb=HTTP_METHOD_POST;else if(!strcmp(method,"PUT"))verb=HTTP_METHOD_PUT;
 else if(!strcmp(method,"GET"))verb=HTTP_METHOD_GET;else goto done;
 stage="headers";
 if(esp_http_client_set_method(client,verb)!=ESP_OK||esp_http_client_set_header(client,"Host",c->origin+8)!=ESP_OK||esp_http_client_set_header(client,"Authorization",authorization)!=ESP_OK||esp_http_client_set_header(client,"X-Meeting-Protocol-Version","1")!=ESP_OK|| (type?esp_http_client_set_header(client,"Content-Type",type)!=ESP_OK:!clear_header(client,"Content-Type")) || (sha?esp_http_client_set_header(client,"X-Content-SHA256",sha)!=ESP_OK:!clear_header(client,"X-Content-SHA256")))goto done;
 if(upload&&!file_headers(client,upload->manifest))goto done;
 stage="open";
 if(upload) {
  if(cancelled(c)||esp_timer_get_time()>=deadline)goto done;
  upload->open_started=esp_timer_get_time();
 }
 if(esp_http_client_open(client,(int)body_len)!=ESP_OK)goto done;
 stage="write";
 int64_t progress_deadline=deadline;
 if(upload) {
  upload->open_done=esp_timer_get_time();
  progress_deadline=progress_limit(upload->open_done,deadline);
 }
 size_t written=0;
 while(written<body_len) {
  int64_t now=esp_timer_get_time();
  if(cancelled(c)||now>=deadline||now>=progress_deadline)goto done;
  size_t length=body_len-written;
  const char *data;
  if(upload) {
   if(length>cap)length=cap;
   if(!meeting_scr1_read_projected(upload->source,&upload->scan,written,(uint8_t *)response,length))goto done;
   now=esp_timer_get_time();
   if(cancelled(c)||now>=deadline||now>=progress_deadline)goto done;
   data=response;
  } else data=(const char *)body+written;
  int w=esp_http_client_write(client,data,(int)length);
  if(w<=0||(size_t)w>length)goto done;
  written+=(size_t)w;
  if(upload) {
   upload->written=written;
   now=esp_timer_get_time();
   if(now>=progress_deadline)goto done;
   progress_deadline=progress_limit(now,deadline);
  }
 }
 if(upload) upload->write_done=esp_timer_get_time();
#if CONFIG_MEETING_SUPERVISED_TEST_PREEMPT_CHUNK
 if(meeting_commission_allowed()&&!atomic_load(&s_test_preempt_ready)&&
    !strcmp(method,"PUT")&&strstr(path,"/chunks/1")&&path[strlen(path)-1]=='1'){
  ESP_LOGI(TAG,"TEST chunk sent path=%s; waiting before response",path);
  atomic_store(&s_test_preempt_ready,true);
  int64_t probe_deadline=esp_timer_get_time()+5000000LL;
  while(!cancelled(c)&&esp_timer_get_time()<probe_deadline)vTaskDelay(pdMS_TO_TICKS(10));
  if(cancelled(c)){ESP_LOGI(TAG,"TEST chunk request cancelled for capture");goto done;}
  ESP_LOGW(TAG,"TEST capture did not preempt chunk request");
 }
#endif
 stage="response_headers";
 for(;;){
  if(cancelled(c))goto done;
  int64_t now=esp_timer_get_time();
  if(now>=deadline||now>=progress_deadline){reason="deadline";goto done;}
  int64_t headers=esp_http_client_fetch_headers(client);
  if(headers>=0)break;
  if(headers!=-ESP_ERR_HTTP_EAGAIN){reason="fetch_headers";goto done;}
 }
 if(upload) {
  int64_t now=esp_timer_get_time();
  if(now>=progress_deadline)goto done;
  progress_deadline=progress_limit(now,deadline);
 }
 *status=esp_http_client_get_status_code(client);size_t total=0;
 stage="response_body";
 /* The parser may have consumed the full response while fetch_headers
  * cached its body. Always drain the cache before checking completion. */
 for(;;){
  if(cancelled(c))goto done;
  int64_t now=esp_timer_get_time();
  if(now>=deadline||now>=progress_deadline){reason="deadline";goto done;}
  if(total>=cap){reason="response_limit";goto done;}
  int r=esp_http_client_read(client,response+total,(int)(cap-total));read_result=r;
  if(r==-ESP_ERR_HTTP_EAGAIN)continue;
  if(r<0){reason="read";goto done;}
  if(r==0){if(!esp_http_client_is_complete_data_received(client)){reason="incomplete";goto done;}break;}
  total+=(size_t)r;received=total;
  if(upload) {
   now=esp_timer_get_time();
   if(now>=progress_deadline)goto done;
   progress_deadline=progress_limit(now,deadline);
  }
 }
 if(cancelled(c))goto done;
 if(esp_timer_get_time()>=deadline){reason="deadline";goto done;}
 *response_len=total;ok=true;
#if CONFIG_MEETING_SUPERVISED_TEST_PREEMPT_CHUNK
 if(meeting_commission_allowed()&&!strcmp(method,"PUT")&&strstr(path,"/chunks/0")&&
    path[strlen(path)-1]=='0'&&(*status==200||*status==201))
  ESP_LOGI(TAG,"TEST accepted chunk path=%s sha256=%s",path,sha?sha:"");
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_UPLOAD_INTERRUPTS
 /* Controlled fault injection, only in the designated device test build.
  * The server has accepted the request before its reply is withheld. */
 static bool chunk_interrupted,finalize_lost;
 if(meeting_commission_allowed()&&(*status==200||*status==201)){
  if(!chunk_interrupted&&!strcmp(method,"PUT")&&strstr(path,"/chunks/")){
   chunk_interrupted=true;
   esp_err_t disconnected=esp_wifi_disconnect();
   ESP_LOGI(TAG,"TEST upload chunk response withheld; disconnect=%s",esp_err_to_name(disconnected));
   ok=false;
  }else if(chunk_interrupted&&!finalize_lost&&!strcmp(method,"POST")&&strstr(path,"/finalize")){
   finalize_lost=true;ESP_LOGI(TAG,"TEST finalize response withheld");ok=false;
  }
 }
#endif
done:;
 bool fallback=false;
 if(!ok&&!cancelled(c)){
  int tls_code=0,tls_flags=0;
  int detail=esp_http_client_get_and_clear_last_tls_error(client,&tls_code,&tls_flags);
  /* Retry only a failed TLS handshake, before any HTTP body was sent. */
  fallback=c->fallback_ca_pem&&c->fallback_ca_pem[0]&&!strcmp(stage,"open")&&tls_code==0x2700;
  int socket_errno=esp_http_client_get_errno(client);wifi_ap_record_t ap={0};bool have_ap=esp_wifi_sta_get_ap_info(&ap)==ESP_OK;
  ESP_LOGW(TAG,"Request failure: stage=%s reason=%s elapsed_ms=%u received=%u read=%d transport=%d detail=%d verify_flags=0x%x errno=%d heap_before=%u largest_before=%u heap_after=%u largest_after=%u wifi_connected=%d rssi=%d channel=%u",stage,reason,(unsigned)((esp_timer_get_time()-started)/1000),(unsigned)received,read_result,(int)detail,tls_code,(unsigned)tls_flags,socket_errno,(unsigned)heap_before,(unsigned)largest_before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),have_ap,(int)ap.rssi,(unsigned)ap.primary);
  ESP_LOGW(TAG,"TLS target=%s wall_time=%lld",ip,(long long)time(NULL));
  (void)stage;(void)reason;(void)received;(void)read_result;(void)detail;(void)tls_code;(void)tls_flags;(void)socket_errno;(void)have_ap;
 }
 (void)started;(void)heap_before;(void)largest_before;
 memset(authorization,0,sizeof(authorization));if(!ok)meeting_https_close(c);
 if(fallback&&!cancelled(c)){
  c->ca_pem=c->fallback_ca_pem;c->fallback_ca_pem=NULL;
  if(upload) upload->retry_original_ca=true;
  else return request_internal(ctx,method,path,type,sha,body,body_len,status,response,cap,response_len,NULL);
 }
 return ok;
}

static bool request(void *ctx,const char *method,const char *path,const char *type,const char *sha,
    const uint8_t *body,size_t body_len,int *status,char *response,size_t cap,size_t *response_len)
{
 return request_internal(ctx,method,path,type,sha,body,body_len,status,response,cap,response_len,NULL);
}

static bool manifest_id_matches(const meeting_snapshot *snapshot,const meeting_manifest *manifest)
{
 static const char hex[]="0123456789abcdef";
 size_t pos=0;
 for(size_t i=0;i<sizeof(snapshot->meeting_id);i++) {
  if((i==4||i==6||i==8||i==10)&&manifest->id[pos++]!='-')return false;
  uint8_t byte=snapshot->meeting_id[i];
  if(manifest->id[pos++]!=hex[byte>>4]||manifest->id[pos++]!=hex[byte&15])return false;
 }
 return manifest->id[pos]=='\0';
}

static int64_t phase_ms(int64_t start,int64_t end,int64_t finished)
{
 return start<0?-1:((end<0?finished:end)-start)/1000;
}

static void log_file_upload(const file_upload *upload,int status)
{
 const int64_t finished=esp_timer_get_time();
 ESP_LOGI(TAG,"Whole upload http=%d bytes=%u/%u elapsed_ms=%" PRId64 " dns_ms=%" PRId64
     " open_ms=%" PRId64 " stream_ms=%" PRId64 " response_ms=%" PRId64,
     status,(unsigned)upload->written,(unsigned)upload->scan.valid_end,
     phase_ms(upload->started,finished,finished),phase_ms(upload->started,upload->dns_done,finished),
     phase_ms(upload->open_started,upload->open_done,finished),
     phase_ms(upload->open_done,upload->write_done,finished),phase_ms(upload->write_done,finished,finished));
 (void)upload;(void)status;(void)finished;(void)phase_ms;
}

meeting_upload_result meeting_https_upload_file(meeting_https_config *c,FILE *source,
    const meeting_snapshot *snapshot,const meeting_manifest *manifest)
{
 if(!c)return MEETING_UPLOAD_FAILED;
 c->file_route_unavailable=false;
 if(!source||!snapshot||!manifest||!c->is_cancelled||!c->time_valid||
     !origin_hostname(c->origin,c->tls_hostname)||!token_valid(c->device_token)||!c->ca_pem||!c->ca_pem[0]||
     !manifest_id_matches(snapshot,manifest)||manifest->reason>3||manifest->reason!=snapshot->reason||
     manifest->time_trust>2||(!manifest->recorded_at_present&&manifest->time_trust==2)||
     (manifest->recorded_at_present&&(manifest->recorded_at<946684800||manifest->recorded_at>4102444800LL))||
     !manifest->bytes||manifest->bytes>INT_MAX||manifest->bytes!=snapshot->valid_end||
     manifest->count!=(manifest->bytes+MEETING_UPLOAD_CHUNK_SIZE-1)/MEETING_UPLOAD_CHUNK_SIZE)
  return MEETING_UPLOAD_FAILED;
 if(cancelled(c))return MEETING_UPLOAD_PAUSED;

 const int64_t start=esp_timer_get_time();
 if(start<0||start>INT64_MAX-file_timeout_us)return MEETING_UPLOAD_FAILED;
 char path[72],digest[65],buffer[FILE_UPLOAD_BUFFER_SIZE];
 int length=snprintf(path,sizeof(path),"/meeting-service/meetings/%s/audio",manifest->id);
 if(length<0||(size_t)length>=sizeof(path))return MEETING_UPLOAD_FAILED;
 static const char hex[]="0123456789abcdef";
 for(size_t i=0;i<sizeof(manifest->digest);i++) {
  digest[2*i]=hex[manifest->digest[i]>>4];
  digest[2*i+1]=hex[manifest->digest[i]&15];
 }
 digest[sizeof(digest)-1]='\0';

 /* This request owns a fresh HTTP client and closes it after every outcome,
  * so neither its event context nor the metadata headers escape the call. */
 meeting_https_close(c);
 for(unsigned attempt=0;attempt<2;attempt++) {
  file_upload upload={
   .source=source,.manifest=manifest,.scan={.valid_end=snapshot->valid_end,.frames=snapshot->frames},
   .deadline=start+file_timeout_us,.started=-1,.dns_done=-1,.open_started=-1,.open_done=-1,.write_done=-1
  };
  int status=0;
  size_t received=0;
  bool ok=request_internal(c,"PUT",path,"application/octet-stream",digest,NULL,(size_t)manifest->bytes,
      &status,buffer,sizeof(buffer),&received,&upload);
  log_file_upload(&upload,status);
  meeting_https_close(c);
  if(cancelled(c))return MEETING_UPLOAD_PAUSED;
  if(ok) {
   c->file_route_unavailable=(status==404||status==405)&&!upload.response_mode_present;
   return meeting_receipt_json_matches(buffer,received,status,manifest)?MEETING_UPLOAD_CONFIRMED:MEETING_UPLOAD_FAILED;
  }
  if(!upload.retry_original_ca)break;
 }
 return MEETING_UPLOAD_FAILED;
}

void meeting_https_close(meeting_https_config *c)
{
 if(c&&c->client){esp_http_client_cleanup(c->client);c->client=NULL;}
}
bool meeting_https_bind(meeting_https_config *c,meeting_upload_transport *out)
{
 if(!c||!out||!origin_hostname(c->origin,c->tls_hostname)||!token_valid(c->device_token)||!c->ca_pem||!c->ca_pem[0]||!c->is_cancelled||!c->time_valid)return false;
 c->client=NULL;
 *out=(meeting_upload_transport){.ctx=c,.cancelled=cancelled,.request=request};return true;
}
