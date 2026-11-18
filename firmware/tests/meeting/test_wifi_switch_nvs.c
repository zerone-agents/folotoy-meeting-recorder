#include "meeting_wifi_switch_nvs.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char blob[64]; static size_t len; static int commit_fail;
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size) {
    (void)h; assert(!strcmp(key,"txn")); if(!len)return ESP_ERR_NVS_NOT_FOUND;
    if(*size<len)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,blob,len);*size=len;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *in,size_t size) {
    (void)h; assert(!strcmp(key,"txn")&&size<=sizeof blob);memcpy(blob,in,size);len=size;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {(void)h;return commit_fail?99:ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){(void)h;assert(!strcmp(key,"txn"));if(!len)return ESP_ERR_NVS_NOT_FOUND;len=0;return ESP_OK;}
int main(void) {
    nvs_handle_t h=1; meeting_switch_io io;meeting_wifi_switch s;
    assert(meeting_wifi_switch_nvs_bind(&h,&io));meeting_wifi_switch_init(&s,&io);
    assert(meeting_wifi_switch_recover(&s)==MEETING_SWITCH_OK);
    meeting_switch_request r={.id={1},.digest={2}};
    commit_fail=1;assert(meeting_wifi_switch_begin(&s,&r)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_begin(&s,&r)==MEETING_SWITCH_STORAGE_ERROR);
    /* Stub models reopening the namespace; it does not model NVS staging. */
    h=2;
    meeting_switch_record persisted={0};
    assert(io.load(io.ctx,&persisted)==0);
    meeting_switch_record unrelated=persisted;unrelated.request.id[0]^=2;
    assert(meeting_wifi_switch_reconcile(&s,&unrelated)==MEETING_SWITCH_CONFLICT);
    assert(s.uncertain);
    assert(meeting_wifi_switch_reconcile(&s,&persisted)==MEETING_SWITCH_OK);
    assert(meeting_wifi_switch_begin(&s,&r)==MEETING_SWITCH_OK);
    commit_fail=0;meeting_wifi_switch restart;meeting_wifi_switch_init(&restart,&io);
    assert(meeting_wifi_switch_recover(&restart)==MEETING_SWITCH_OK);
    assert(restart.present && restart.record.state==MEETING_SWITCH_WAITING);
    assert(!restart.record.acknowledged);
    assert(meeting_wifi_switch_advance(&restart,r.id,MEETING_SWITCH_ROLLED_BACK)==MEETING_SWITCH_OK);
    /* An ACK commit may report failure whether the new value persisted or
     * not. Both recovery branches must preserve the terminal result. */
    unsigned char before_ack[sizeof blob];memcpy(before_ack,blob,len);size_t before_len=len;
    commit_fail=1;
    assert(meeting_wifi_switch_ack(&restart,r.id,true)==MEETING_SWITCH_STORAGE_ERROR);
    meeting_switch_request different={.id={7},.digest={8}};
    assert(meeting_wifi_switch_begin(&restart,&different)==MEETING_SWITCH_STORAGE_ERROR);
    commit_fail=0;meeting_wifi_switch_init(&restart,&io);
    assert(meeting_wifi_switch_recover(&restart)==MEETING_SWITCH_OK && restart.record.acknowledged);
    memcpy(blob,before_ack,before_len);len=before_len;meeting_wifi_switch_init(&restart,&io);
    assert(meeting_wifi_switch_recover(&restart)==MEETING_SWITCH_OK && !restart.record.acknowledged);
    assert(meeting_wifi_switch_begin(&restart,&different)==MEETING_SWITCH_CONFLICT);
    assert(meeting_wifi_switch_ack(&restart,r.id,true)==MEETING_SWITCH_OK);
    meeting_wifi_switch_init(&restart,&io);
    assert(meeting_wifi_switch_recover(&restart)==MEETING_SWITCH_OK);
    meeting_switch_state state=0;
    assert(restart.record.acknowledged && meeting_wifi_switch_query(&restart,r.id,&state)==MEETING_SWITCH_OK && state==MEETING_SWITCH_ROLLED_BACK);
    blob[10]^=1;meeting_wifi_switch_init(&restart,&io);
    assert(meeting_wifi_switch_recover(&restart)==MEETING_SWITCH_STORAGE_ERROR);
    assert(meeting_wifi_switch_begin(&restart,&r)==MEETING_SWITCH_STORAGE_ERROR);
    puts("wifi switch NVS corruption and ambiguous commit: PASS");
}
