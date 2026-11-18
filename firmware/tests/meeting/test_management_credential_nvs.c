#include "meeting_management_credential_nvs.h"
#include <assert.h>
#include <string.h>
static uint8_t blob[MEETING_MGMT_RECORD_SIZE];static size_t len;static int fail_set,fail_commit;
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){(void)h;assert(!strcmp(key,"identity"));if(!len)return ESP_ERR_NVS_NOT_FOUND;if(*size<len)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,blob,len);*size=len;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){(void)h;assert(!strcmp(key,"identity")&&size==sizeof blob);if(fail_set)return 99;memcpy(blob,data,size);len=size;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return fail_commit?99:ESP_OK;}
int main(void){
 nvs_handle_t h=1;meeting_mgmt_io io;assert(meeting_mgmt_nvs_bind(&h,&io));meeting_mgmt_record a={.generation=1,.device_id={1},.secret={2},.state=MEETING_MGMT_ACTIVE},out;
 fail_set=1;assert(meeting_mgmt_replace(&io,0,&a)==MEETING_MGMT_ERROR&&len==0);fail_set=0;
 fail_commit=1;assert(meeting_mgmt_replace(&io,0,&a)==MEETING_MGMT_ERROR);fail_commit=0;
 assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_OK&&out.generation==1);len=1;assert(meeting_mgmt_load(&io,&out)==MEETING_MGMT_ERROR);
}
