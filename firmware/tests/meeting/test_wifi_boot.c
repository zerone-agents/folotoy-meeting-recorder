#include "meeting_wifi_boot.h"
#include "meeting_wifi_slots_nvs.h"
#include "meeting_wifi_switch_nvs.h"
#include <assert.h>
#include <string.h>
static int namespace_missing, pointer_missing, corrupt, txn_present,fail_save;
static meeting_wifi_slot active,staged;static int staged_present;
static meeting_switch_record record;
static int read_pointer(void *ctx,meeting_wifi_pointer *out){(void)ctx;if(corrupt)return -1;if(pointer_missing)return 1;*out=(meeting_wifi_pointer){0,active.generation};return 0;}
static int read_slot(void *ctx,unsigned i,meeting_wifi_slot *out){(void)ctx;if(i==1){if(!staged_present)return 1;*out=staged;return 0;}*out=active;return 0;}
static int load(void *ctx,meeting_switch_record *out){(void)ctx;if(!txn_present)return 1;*out=record;return 0;}
static bool save(void *ctx,const meeting_switch_record *in){(void)ctx;if(fail_save)return false;record=*in;return true;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){(void)name;(void)mode;*out=1;return namespace_missing?ESP_ERR_NVS_NOT_FOUND:ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
bool meeting_wifi_slots_nvs_bind(nvs_handle_t *h,meeting_wifi_slots_io *out){(void)h;*out=(meeting_wifi_slots_io){.read_pointer=read_pointer,.read_slot=read_slot};return true;}
bool meeting_wifi_switch_nvs_bind(nvs_handle_t *h,meeting_switch_io *out){(void)h;*out=(meeting_switch_io){.load=load,.save=save};return true;}
int main(void){
 meeting_config c={.ssid="old",.password="oldpass",.origin="https://example.com",.device_token="token"},before=c;
 namespace_missing=1;assert(meeting_wifi_boot_restore(&c)&&!memcmp(&c,&before,sizeof(c)));namespace_missing=0;
 active=(meeting_wifi_slot){.generation=2,.ssid="new",.password="newpass",.request_id={1},.digest={2}};
 assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"new")&&!strcmp(c.origin,before.origin)&&!strcmp(c.device_token,before.device_token));
 c=before;record=(meeting_switch_record){.request={.id={1},.digest={2}},.state=MEETING_SWITCH_COMMITTING};txn_present=1;
 fail_save=1;assert(!meeting_wifi_boot_restore(&c)&&!memcmp(&c,&before,sizeof(c)));fail_save=0;
 assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"new")&&record.state==MEETING_SWITCH_COMPLETE);
 record.state=MEETING_SWITCH_COMPLETE;assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"new"));
 active=(meeting_wifi_slot){.generation=1,.ssid="old",.password="oldpass",.request_id={9},.digest={8}};
 staged=(meeting_wifi_slot){.generation=2,.ssid="new",.password="newpass",.request_id={1},.digest={2}};staged_present=1;
 record.state=MEETING_SWITCH_COMMITTING;c=before;
 assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"old")&&record.state==MEETING_SWITCH_ROLLED_BACK);
 record.state=MEETING_SWITCH_TESTING;c=before;
 /* A failed recovery write must not apply either candidate or stale state;
  * retrying with the same durable record remains possible. */
 fail_save=1;assert(!meeting_wifi_boot_restore(&c)&&!memcmp(&c,&before,sizeof(c)));
 assert(record.state==MEETING_SWITCH_TESTING);fail_save=0;
 assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"old")&&record.state==MEETING_SWITCH_WAITING);
 assert(meeting_wifi_boot_restore(&c)&&record.state==MEETING_SWITCH_WAITING);
 staged.digest[0]=7;assert(!meeting_wifi_boot_restore(&c));staged.digest[0]=2;
 staged_present=0;c=before;
 assert(meeting_wifi_boot_restore(&c)&&!strcmp(c.ssid,"old")&&record.state==MEETING_SWITCH_ROLLED_BACK);
 active=staged;staged_present=0;
 c=before;record.state=MEETING_SWITCH_COMPLETE;record.request.digest[0]=3;assert(!meeting_wifi_boot_restore(&c)&&!memcmp(&c,&before,sizeof(c)));
 txn_present=0;pointer_missing=1;assert(!meeting_wifi_boot_restore(&c));pointer_missing=0;
 corrupt=1;assert(!meeting_wifi_boot_restore(&c));return 0;
}
