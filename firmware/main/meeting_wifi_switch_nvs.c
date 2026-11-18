#include "meeting_wifi_switch_nvs.h"
#include <string.h>
enum { RECORD_SIZE=57 };
static uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;
}
static bool encode(const meeting_switch_record *r,uint8_t out[RECORD_SIZE]) {
    if(!r||r->state<MEETING_SWITCH_WAITING||r->state>MEETING_SWITCH_COMMITTING||
       (r->acknowledged&&r->state!=MEETING_SWITCH_COMPLETE&&r->state!=MEETING_SWITCH_ROLLED_BACK))return false;
    memcpy(out,"MSW1",4);memcpy(out+4,r->request.id,16);memcpy(out+20,r->request.digest,32);
    out[52]=(uint8_t)r->state|(r->acknowledged?0x80:0);uint32_t c=crc32(out,53);
    for(int i=0;i<4;i++)out[53+i]=(uint8_t)(c>>(8*i));
    return true;
}
static int load(void *ctx,meeting_switch_record *r) {
    if(!ctx||!r)return -1;
 uint8_t b[RECORD_SIZE];size_t n=sizeof(b);
    esp_err_t e=nvs_get_blob(*(nvs_handle_t*)ctx,"txn",b,&n);
    if(e==ESP_ERR_NVS_NOT_FOUND)return 1;
    if(e!=ESP_OK||n!=sizeof(b)||memcmp(b,"MSW1",4))return -1;
    uint32_t got=0;for(int i=0;i<4;i++)got|=(uint32_t)b[53+i]<<(8*i);
    uint8_t state=b[52]&0x7f;bool acknowledged=(b[52]&0x80)!=0;
    if(got!=crc32(b,53)||state<MEETING_SWITCH_WAITING||state>MEETING_SWITCH_COMMITTING||
       (acknowledged&&state!=MEETING_SWITCH_COMPLETE&&state!=MEETING_SWITCH_ROLLED_BACK))return -1;
    memcpy(r->request.id,b+4,16);memcpy(r->request.digest,b+20,32);r->state=(meeting_switch_state)state;r->acknowledged=acknowledged;return 0;
}
static bool save(void *ctx,const meeting_switch_record *r) {
    uint8_t b[RECORD_SIZE];meeting_switch_record check;
    if(!ctx||!encode(r,b))return false;
 nvs_handle_t h=*(nvs_handle_t*)ctx;
    if(nvs_set_blob(h,"txn",b,sizeof(b))!=ESP_OK||nvs_commit(h)!=ESP_OK)return false;
    return load(ctx,&check)==0&&check.state==r->state&&check.acknowledged==r->acknowledged&&
           !memcmp(&check.request,&r->request,sizeof(r->request));
}
static bool erase(void *ctx) {
    if(!ctx)return false;
 nvs_handle_t h=*(nvs_handle_t*)ctx;esp_err_t e=nvs_erase_key(h,"txn");
    return (e==ESP_OK||e==ESP_ERR_NVS_NOT_FOUND)&&nvs_commit(h)==ESP_OK;
}
bool meeting_wifi_switch_nvs_bind(nvs_handle_t *handle,meeting_switch_io *out) {
    if(!handle||!out)return false;
 *out=(meeting_switch_io){handle,save,load,erase};return true;
}
