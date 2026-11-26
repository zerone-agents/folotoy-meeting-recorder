#include "meeting_https.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include <assert.h>
#include <string.h>
static const char *last_ca;static int fake_tls_code;
static int done,clock_valid=1,cancelled_flag,init_calls,invalidate_on_headers,cleanup_calls,url_calls,dns_failed;
static const char *expected_authority="example.com";
static int header_timeouts,body_timeouts,cancel_on_timeout,check_alias,open_fails,cancel_on_open,content_set,sha_set,complete_at_headers,buffered;
static int64_t now_us,open_advance_us;
static bool file_mode,file_header_seen,file_receipt_matches,file_has_time,file_cancel_on_write;
static unsigned file_metadata,file_status=200;
static size_t file_written,file_bytes,file_fail_at;
static int file_partial_write;
static int64_t file_write_advance,expected_dns_deadline;
static esp_err_t (*file_event)(esp_http_client_event_t *);
static void *file_event_ctx;
static const meeting_manifest *expected_manifest;
bool meeting_dns_resolve(const char *host,bool (*cancel)(void *),void *ctx,int64_t deadline,char ip[16]){assert(!strcmp(host,"example.com")&&deadline>now_us&&!cancel(ctx));if(expected_dns_deadline)assert(deadline==expected_dns_deadline);if(dns_failed)return false;strcpy(ip,"192.0.2.1");return true;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 10000;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 5000;}
int esp_http_client_get_and_clear_last_tls_error(void *h,int *code,int *flags){(void)h;*code=fake_tls_code;fake_tls_code=0;*flags=0;return 0;}
int esp_http_client_get_errno(void *h){(void)h;return 0;}
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){ap->rssi=-50;ap->primary=1;return 0;}
int64_t esp_timer_get_time(void){return now_us;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->cert_pem&&!c->skip_cert_common_name_check&&c->disable_auto_redirect&&c->timeout_ms==3000&&!strcmp(c->common_name,"example.com"));assert(strstr(c->url,"https://192.0.2.1")==c->url);if(file_mode){assert(!strcmp(c->url,"https://192.0.2.1/meeting-service/meetings/01000000-0000-0000-0000-000000000000/audio"));assert(c->event_handler&&c->user_data);file_event=c->event_handler;file_event_ctx=c->user_data;file_metadata=0;}last_ca=c->cert_pem;init_calls++;return (void*)1;}
int esp_http_client_set_url(void *h,const char *url){(void)h;assert(url);url_calls++;return 0;}
int esp_http_client_delete_header(void *h,const char *key){(void)h;int *v=!strcmp(key,"Content-Type")?&content_set:&sha_set;if(!*v)return ESP_ERR_NOT_FOUND;*v=0;return ESP_OK;}
int esp_http_client_set_method(void *h,esp_http_client_method_t m){(void)h;if(file_mode)assert(m==HTTP_METHOD_PUT);return 0;}
int esp_http_client_set_header(void *h,const char *k,const char *v)
{
 (void)h;
 if(!strcmp(k,"Host"))assert(!strcmp(v,expected_authority));
 if(!strcmp(k,"Content-Type"))content_set=1;
 if(!strcmp(k,"X-Content-SHA256"))sha_set=1;
 if(file_mode){
  if(!strcmp(k,"Authorization")){assert(!strcmp(v,"Bearer abcdefghijklmnopqrstuvwxyz0123456789"));file_metadata|=1;}
  if(!strcmp(k,"Content-Type")){assert(!strcmp(v,"application/octet-stream"));file_metadata|=2;}
  if(!strcmp(k,"X-Content-SHA256")){assert(strlen(v)==64&&!strncmp(v,"ab",2));file_metadata|=4;}
  if(!strcmp(k,"X-Meeting-End-Reason")){assert(!strcmp(v,"interrupted"));file_metadata|=8;}
  if(!strcmp(k,"X-Meeting-Time-Trust")){assert(!strcmp(v,file_has_time?"trusted":"uncalibrated"));file_metadata|=16;}
  if(!strcmp(k,"X-Meeting-Recorded-At")){assert(!strcmp(v,"1795000000"));file_metadata|=32;}
  if(!strcmp(k,"X-Meeting-Protocol-Version")){assert(!strcmp(v,"1"));file_metadata|=64;}
 }
 return 0;
}
int esp_http_client_open(void *h,int n){(void)h;done=0;buffered=0;now_us+=open_advance_us;if(file_mode){assert(n==(int)file_bytes);assert(file_metadata==(file_has_time?127u:95u));file_written=0;}if(open_fails){open_fails=0;if(cancel_on_open)cancelled_flag=1;return -1;}return 0;}
int esp_http_client_write(void *h,const char *p,int n)
{
 (void)h;
 if(check_alias){assert(n==3&&!memcmp(p,"abc",3));check_alias=0;}
 if(file_mode){
  assert(n>0&&n<=4096&&file_written+(size_t)n<=file_bytes);
  if(file_fail_at&&file_written>=file_fail_at)return -1;
  for(int i=0;i<n;i++)assert((uint8_t)p[i]==(uint8_t)((file_written+(size_t)i)%251));
  if(file_partial_write&&n>file_partial_write){n=file_partial_write;file_partial_write=0;}
  file_written+=(size_t)n;
  now_us+=file_write_advance;
  if(file_cancel_on_write)cancelled_flag=1;
 }
 return n;
}
int64_t esp_http_client_fetch_headers(void *h){(void)h;if(invalidate_on_headers)clock_valid=0;if(complete_at_headers){done=1;buffered=1;}if(header_timeouts){header_timeouts--;now_us+=3000000;if(cancel_on_timeout)cancelled_flag=1;return -ESP_ERR_HTTP_EAGAIN;}if(file_mode){assert(file_written==file_bytes);if(file_header_seen){esp_http_client_event_t event={.event_id=HTTP_EVENT_ON_HEADER,.user_data=file_event_ctx,.header_key="x-meeting-upload-mode",.header_value="whole-v1"};assert(file_event(&event)==ESP_OK);}}return 2;}
int esp_http_client_get_status_code(void *h){(void)h;return file_mode?(int)file_status:200;}
bool esp_http_client_is_complete_data_received(void *h){(void)h;return done;}
int esp_http_client_read(void *h,char *out,int n){(void)h;if(body_timeouts){body_timeouts--;now_us+=3000000;return -ESP_ERR_HTTP_EAGAIN;}if(done&&!buffered)return 0;if(n<2)return -1;memcpy(out,"{}",2);done=1;buffered=0;return 2;}
int esp_http_client_close(void *h){(void)h;return 0;}
int esp_http_client_cleanup(void *h){(void)h;cleanup_calls++;return 0;}
static bool cancelled(void *c){(void)c;return cancelled_flag;}
static bool time_valid(void *c){(void)c;return clock_valid;}
bool meeting_scr1_read_projected(FILE *file,const meeting_scr1_scan_result *scan,uint64_t offset,
    uint8_t *buffer,size_t length)
{
 assert(file_mode&&file&&scan->valid_end==file_bytes&&offset==file_written&&length<=4096);
 for(size_t i=0;i<length;i++)buffer[i]=(uint8_t)((offset+i)%251);
 return true;
}

bool meeting_receipt_json_matches(const char *json,size_t len,int status,const meeting_manifest *manifest)
{
 assert(file_mode&&manifest==expected_manifest&&len==2&&!memcmp(json,"{}",2));
 return status==200&&file_receipt_matches;
}

static void test_file_requests(void)
{
 meeting_https_config config={.origin="https://example.com",.device_token="abcdefghijklmnopqrstuvwxyz0123456789",
     .ca_pem="CA",.is_cancelled=cancelled,.time_valid=time_valid};
 meeting_snapshot snapshot={.valid_end=9500,.frames=900,.meeting_id={1},.reason=3};
 meeting_manifest manifest={.id="01000000-0000-0000-0000-000000000000",.bytes=9500,.count=3,
     .digest={0xab},.reason=3,.recorded_at_present=true,.recorded_at=1795000000,.time_trust=2};
 FILE *source=tmpfile();
 assert(source);
 expected_authority="example.com";
 expected_manifest=&manifest;
 file_mode=file_has_time=file_header_seen=file_receipt_matches=true;
 file_bytes=manifest.bytes;
 expected_dns_deadline=now_us+15000000;
 int previous_clients=init_calls;
 file_write_advance=6000000;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_CONFIRMED);
 assert(!config.client&&!config.file_route_unavailable&&file_written==file_bytes&&init_calls==previous_clients+1);
 expected_dns_deadline=0;
 file_write_advance=0;

 file_receipt_matches=false;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(!config.file_route_unavailable);
 file_status=404;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(!config.file_route_unavailable);
 file_header_seen=false;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(config.file_route_unavailable);
 file_status=405;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(config.file_route_unavailable);

 file_status=200;
 file_receipt_matches=true;
 file_has_time=manifest.recorded_at_present=false;
 manifest.time_trust=0;
 file_fail_at=4096;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(!config.file_route_unavailable&&!config.client&&file_written==4096);
 file_fail_at=0;
 file_partial_write=1024;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_CONFIRMED);
 assert(file_written==file_bytes&&!file_partial_write);

 config.ca_pem="Preferred CA";
 config.fallback_ca_pem="Original CA";
 open_fails=1;
 fake_tls_code=0x2700;
 previous_clients=init_calls;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_CONFIRMED);
 assert(init_calls==previous_clients+2&&!strcmp(last_ca,"Original CA"));

 file_cancel_on_write=true;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_PAUSED);
 assert(file_written==4096&&!config.file_route_unavailable&&!config.client);
 file_cancel_on_write=false;
 cancelled_flag=0;
 header_timeouts=6;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(header_timeouts==1&&!config.file_route_unavailable);
 header_timeouts=0;

 file_write_advance=2000000;
 file_bytes=snapshot.valid_end=manifest.bytes=4096u*99;
 manifest.count=99;
 assert(meeting_https_upload_file(&config,source,&snapshot,&manifest)==MEETING_UPLOAD_FAILED);
 assert(file_written==4096u*90&&!config.file_route_unavailable&&!config.client);
 fclose(source);
}
int main(void){meeting_https_config c={.origin="https://example.com",.device_token="abcdefghijklmnopqrstuvwxyz0123456789",.ca_pem="CA",.is_cancelled=cancelled,.time_valid=time_valid};meeting_upload_transport t;assert(meeting_https_bind(&c,&t));char response[20];size_t n;int status;clock_valid=0;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(init_calls==0);clock_valid=1;assert(t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(status==200&&n==2);complete_at_headers=1;assert(t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(n==2&&!memcmp(response,"{}",2));complete_at_headers=0;memcpy(response,"abc",3);check_alias=1;assert(t.request(t.ctx,"PUT","/meeting-service/meetings/x",NULL,NULL,(const uint8_t *)response,3,&status,response,sizeof response,&n));assert(!check_alias&&n==2&&!memcmp(response,"{}",2));assert(init_calls==1&&url_calls==2&&cleanup_calls==0);assert(t.request(t.ctx,"PUT","/meeting-service/meetings/x","application/octet-stream","digest",NULL,0,&status,response,sizeof response,&n));assert(content_set&&sha_set);assert(t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(!content_set&&!sha_set);int before=cleanup_calls;open_fails=1;assert(!t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(cleanup_calls==before+1&&c.client==NULL);int initialized=init_calls;assert(t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(init_calls==initialized+1);open_advance_us=5100000;assert(t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));open_advance_us=15000000;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));open_advance_us=0;header_timeouts=1;body_timeouts=1;assert(t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));header_timeouts=5;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));header_timeouts=1;cancel_on_timeout=1;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));cancelled_flag=0;cancel_on_timeout=0;invalidate_on_headers=1;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(t.cancelled(t.ctx));invalidate_on_headers=0;clock_valid=1;cancelled_flag=1;assert(!t.request(t.ctx,"POST","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));meeting_https_close(&c);assert(c.client==NULL);cancelled_flag=0;c.origin="https://example.com:8443";expected_authority="example.com:8443";assert(meeting_https_bind(&c,&t));dns_failed=1;int before_dns=init_calls;assert(!t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n)&&init_calls==before_dns);dns_failed=0;assert(t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));meeting_https_close(&c);c.ca_pem="X2";c.fallback_ca_pem="X1";assert(meeting_https_bind(&c,&t));open_fails=1;fake_tls_code=0x2700;assert(t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(!strcmp(last_ca,"X1")&&!c.fallback_ca_pem);meeting_https_close(&c);c.ca_pem="X2";c.fallback_ca_pem="X1";assert(meeting_https_bind(&c,&t));open_fails=1;fake_tls_code=0;int before_fallback=init_calls;assert(!t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(init_calls==before_fallback+1&&!strcmp(c.ca_pem,"X2")&&c.fallback_ca_pem);meeting_https_close(&c);open_fails=1;fake_tls_code=0x2700;cancel_on_open=1;before_fallback=init_calls;assert(!t.request(t.ctx,"GET","/meeting-service/meetings/x",NULL,NULL,NULL,0,&status,response,sizeof response,&n));assert(init_calls==before_fallback+1&&!strcmp(c.ca_pem,"X2"));cancelled_flag=0;cancel_on_open=0;fake_tls_code=0;meeting_https_close(&c);c.origin="http://example.com";assert(!meeting_https_bind(&c,&t));test_file_requests();return 0;}
