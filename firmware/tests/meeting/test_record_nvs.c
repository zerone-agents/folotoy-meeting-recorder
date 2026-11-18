#include "meeting_record_nvs.h"
#include <assert.h>
#include <string.h>
static unsigned char blob[MEETING_RECORD_SIZE];static size_t size;static int fail,fail_set,commits;
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){(void)h;assert(!strcmp(key,"r0000004d"));if(!size)return ESP_ERR_NVS_NOT_FOUND;if(*n<size)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,blob,size);*n=size;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t n){(void)h;assert(!strcmp(key,"r0000004d")&&n==sizeof blob);if(fail_set)return 99;memcpy(blob,data,n);size=n;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;commits++;return fail?99:ESP_OK;}
int main(void){
 meeting_record a={.file_id=77,.recorded_at_present=true,.recorded_at=1800000000,.time_trust=2},b;memset(a.meeting_id,5,16);
 fail=1;assert(!meeting_record_nvs_create(1,&a));assert(!meeting_record_nvs_create(1,&a));fail=0;
 assert(meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_OK);assert(meeting_record_nvs_create(1,&a));
 a.recorded_at++;assert(!meeting_record_nvs_create(1,&a));a.recorded_at--;
 a.meeting_id[0]=8;assert(!meeting_record_nvs_create(1,&a));
 /* A failed write must leave the existing record unchanged, without committing. */
 int before=commits;fail_set=1;assert(!meeting_record_nvs_end_intent(1,77,2));
 assert(commits==before&&meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_OK&&b.end_intent==0);fail_set=0;
 fail=1;assert(!meeting_record_nvs_end_intent(1,77,2));assert(!meeting_record_nvs_end_intent(1,77,2));fail=0;
 assert(meeting_record_nvs_end_intent(1,77,2));assert(meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_OK&&b.end_intent==2&&b.recorded_at_present&&b.recorded_at==1800000000&&b.time_trust==2);
 assert(!meeting_record_nvs_end_intent(1,77,1));blob[0]^=1;assert(meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_ERROR);assert(!meeting_record_nvs_create(1,&a));
 memset(blob,0,sizeof blob);memcpy(blob,"MR01",4);blob[4]=77;memset(blob+8,5,16);uint32_t c=crc(blob,28);for(unsigned i=0;i<4;i++)blob[28+i]=(uint8_t)(c>>(8*i));size=MEETING_RECORD_LEGACY_SIZE;
 assert(meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_OK&&!b.recorded_at_present);
 assert(meeting_record_nvs_end_intent(1,77,1));assert(size==MEETING_RECORD_SIZE&&meeting_record_nvs_read(1,77,&b)==MEETING_RECORD_OK&&b.end_intent==1&&!b.recorded_at_present);
 return 0;
}
