#include "meeting_management_credential_nvs.h"
#include "esp_log.h"
static int read_record(void *ctx,uint8_t out[MEETING_MGMT_RECORD_SIZE]){
    if(!ctx||!out)return -1;
    size_t length=MEETING_MGMT_RECORD_SIZE;
    esp_err_t e=nvs_get_blob(*(nvs_handle_t *)ctx,"identity",out,&length);
    if(e==ESP_ERR_NVS_NOT_FOUND)return 1;
    if(e!=ESP_OK||length!=MEETING_MGMT_RECORD_SIZE){ESP_LOGE("meeting_mgmt","Identity read failed: %s",esp_err_to_name(e));return -1;}
    return 0;
}
static bool write_record(void *ctx,const uint8_t data[MEETING_MGMT_RECORD_SIZE]){
    if(!ctx||!data)return false;
    esp_err_t err=nvs_set_blob(*(nvs_handle_t *)ctx,"identity",data,MEETING_MGMT_RECORD_SIZE);
    if(err==ESP_OK)err=nvs_commit(*(nvs_handle_t *)ctx);
    if(err!=ESP_OK)ESP_LOGE("meeting_mgmt","Identity write failed: %s",esp_err_to_name(err));
    return err==ESP_OK;
}
bool meeting_mgmt_nvs_bind(nvs_handle_t *handle,meeting_mgmt_io *out){
    if(!handle||!out)return false;
    *out=(meeting_mgmt_io){.ctx=handle,.read=read_record,.write_sync=write_record};return true;
}
