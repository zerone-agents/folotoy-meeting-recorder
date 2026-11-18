#include "meeting_store_upload.h"
#include "sc_store.h"
#include <assert.h>
#include <string.h>
static FILE *source;static bool allowed=true;static unsigned frames=90000,sessions,confirmations,releases;static meeting_upload_result answer=MEETING_UPLOAD_FAILED;static meeting_snapshot original,observed;
bool meeting_commission_allowed(void){return allowed;}
FILE *sc_store_upload_acquire(uint32_t id){assert(id==7);return source;}
esp_err_t sc_store_upload_release(FILE *f){assert(f==source);releases++;return ESP_OK;}
esp_err_t sc_store_prepare_snapshot(uint32_t id,meeting_snapshot *out,meeting_cancel_fn cancelled,void *ctx){(void)cancelled;(void)ctx;assert(id==7);*out=original;out->frames=frames;return ESP_OK;}
sc_confirmation_status sc_store_check_confirmation(FILE *f,const meeting_snapshot *s,bool h,int64_t t,uint8_t tr,meeting_cancel_fn c,void *ctx){(void)f;(void)s;(void)h;(void)t;(void)tr;(void)c;(void)ctx;return SC_CONFIRMATION_ABSENT;}
esp_err_t sc_store_confirm_upload(FILE *f,const meeting_snapshot *s,const meeting_manifest *m){(void)f;(void)s;(void)m;confirmations++;return ESP_OK;}
meeting_upload_result meeting_online_session(FILE *f,const meeting_snapshot *s,bool h,int64_t t,uint8_t tr,const meeting_online_config *c,meeting_manifest *m){(void)h;(void)t;(void)tr;(void)c;assert(f==source);observed=*s;sessions++;memset(m,0,sizeof(*m));return answer;}
int main(void){
 source=tmpfile();assert(source);original.meeting_id[0]=0x12;original.meeting_id[15]=0x34;original.valid_end=3000000;original.source_sha256[0]=42;
 meeting_online_config config={0};frames=89999;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_FAILED);assert(!sessions&&!confirmations);
 frames=90000;allowed=false;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_FAILED);assert(!sessions&&!confirmations);
 allowed=true;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_FAILED);assert(sessions==1&&!confirmations);
 assert(observed.meeting_id[0]==(original.meeting_id[0]^0x80));assert(observed.meeting_id[12]!=(original.meeting_id[12]));
 assert(observed.valid_end==original.valid_end&&!memcmp(observed.source_sha256,original.source_sha256,32));
 meeting_snapshot first=observed;answer=MEETING_UPLOAD_PAUSED;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_PAUSED);assert(!memcmp(first.meeting_id,observed.meeting_id,16));
 answer=MEETING_UPLOAD_CONFIRMED;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_PAUSED);assert(!confirmations);
 unsigned before=sessions;assert(meeting_store_upload(7,false,0,0,&config)==MEETING_UPLOAD_PAUSED);assert(sessions==before&&!confirmations);assert(releases==6);fclose(source);
}
