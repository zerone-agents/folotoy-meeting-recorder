#include "meeting_wifi_slots_nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t bytes[3][192];static size_t sizes[3];static bool fail_commit;
static int index_for(const char *k){if(!strcmp(k,"slot0"))return 0;if(!strcmp(k,"slot1"))return 1;assert(!strcmp(k,"active"));return 2;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char*k,void*out,size_t*n){(void)h;int i=index_for(k);if(!sizes[i])return ESP_ERR_NVS_NOT_FOUND;if(*n<sizes[i])return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,bytes[i],sizes[i]);*n=sizes[i];return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char*k,const void*in,size_t n){(void)h;int i=index_for(k);assert(n<=sizeof(bytes[i]));memcpy(bytes[i],in,n);sizes[i]=n;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return fail_commit?99:ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char*k){(void)h;int i=index_for(k);if(!sizes[i])return ESP_ERR_NVS_NOT_FOUND;sizes[i]=0;return ESP_OK;}
int main(void){nvs_handle_t h=1;meeting_wifi_slots_io io;assert(meeting_wifi_slots_nvs_bind(&h,&io));
 meeting_wifi_slot s={.generation=1,.ssid="old",.password="secret"},out;meeting_wifi_pointer p={0,1};unsigned index;
 assert(io.write_slot(io.ctx,0,&s));assert(io.write_pointer(io.ctx,&p));
 assert(meeting_wifi_slots_load(&io,&out,&index)==MEETING_WIFI_SLOTS_OK&&index==0);
 bytes[0][20]^=1;assert(meeting_wifi_slots_load(&io,&out,&index)==MEETING_WIFI_SLOTS_IO_ERROR);
 bytes[0][20]^=1;bytes[2][5]^=1;assert(meeting_wifi_slots_load(&io,&out,&index)==MEETING_WIFI_SLOTS_IO_ERROR);
 bytes[2][5]^=1;fail_commit=true;assert(!io.write_pointer(io.ctx,&p));
 puts("wifi slot NVS corruption and commit failure: PASS");}
