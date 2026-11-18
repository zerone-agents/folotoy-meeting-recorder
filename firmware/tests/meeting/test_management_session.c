#include "meeting_management_session.h"
#include "meeting_config_ble.h"
#include "meeting_management_inbox.h"
#include "meeting_management_dispatch.h"
#include "meeting_management_query.h"
#include "meeting_wifi_sha256.h"
#include <assert.h>
#include <string.h>
static int stop_failure, destroyed, command_pending, prepared;
bool meeting_config_ble_init(meeting_config_ble_inbox *i){(void)i;return true;}
bool meeting_config_ble_init_sized(meeting_config_ble_inbox *i,size_t n){(void)i;return n==118;}
void meeting_config_ble_prepare(meeting_config_ble_inbox *i){(void)i;}
void meeting_config_ble_destroy(meeting_config_ble_inbox *i){(void)i;destroyed++;}
esp_err_t meeting_config_ble_handle(uint32_t s,const uint8_t *r,size_t n,uint8_t **o,ssize_t *l,void *c){(void)s;(void)r;(void)n;(void)o;(void)l;(void)c;return ESP_OK;}
void meeting_config_ble_reset(void *p){(void)p;}
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *i,meeting_pairing_token t,uint8_t n){(void)i;(void)t;(void)n;return true;}
bool meeting_config_ble_acknowledged(meeting_config_ble_inbox *i,meeting_pairing_token t){(void)i;(void)t;return true;}
esp_err_t meeting_management_ble_start(const meeting_mgmt_io *i,const uint8_t id[16],meeting_pairing_handler h,meeting_pairing_reset r,void *c){(void)i;(void)id;(void)h;(void)r;(void)c;return ESP_OK;}
esp_err_t meeting_pairing_ble_stop(void){return stop_failure?ESP_FAIL:ESP_OK;}
bool meeting_management_inbox_take(meeting_config_ble_inbox *i,meeting_wifi_command *c,meeting_pairing_token *t){(void)i;(void)t;if(!command_pending)return false;command_pending=0;c->kind=MEETING_WIFI_COMMAND_SWITCH;return true;}
bool meeting_management_query_publish(meeting_config_ble_inbox *i,meeting_pairing_token t,const meeting_wifi_switch *s,const meeting_wifi_command *c){(void)i;(void)t;(void)s;(void)c;return true;}
uint8_t meeting_management_ack(meeting_pairing_token t,meeting_wifi_switch *s,const meeting_wifi_slots_io *i,const uint8_t id[16]){(void)t;(void)s;(void)i;(void)id;return 24;}
bool meeting_management_dispatch_switch(meeting_config_ble_inbox *i,meeting_pairing_token t,meeting_wifi_switch *s,const meeting_wifi_slots_io *slots,meeting_wifi_operation *op,const meeting_wifi_command *c,bool (*hash)(void *,const uint8_t *,size_t,uint8_t[32]),void *ctx){(void)i;(void)t;(void)s;(void)slots;(void)c;(void)hash;(void)ctx;op->state=MEETING_OP_READY;prepared++;return false;}
bool meeting_wifi_sha256(void *c,const uint8_t *d,size_t n,uint8_t out[32]){(void)c;(void)d;(void)n;(void)out;return true;}
void meeting_wifi_command_clear(meeting_wifi_command *c){memset(c,0,sizeof(*c));}
int main(void){
 meeting_wifi_switch txn={0};meeting_wifi_slots_io slots={0};meeting_wifi_operation op={0};meeting_mgmt_io io={0};uint8_t id[16]={1};
 meeting_management_session *s=meeting_management_session_create(&txn,&slots,&op);assert(s);
 assert(meeting_management_session_start(s,&io,id)==ESP_OK);
 command_pending=1;assert(meeting_management_session_poll(s)==MEETING_SESSION_PREPARED&&prepared==1);
 assert(meeting_management_session_acknowledged(s));
 stop_failure=1;assert(meeting_management_session_stop(s)!=ESP_OK&&!meeting_management_session_destroy(s)&&destroyed==0);
 stop_failure=0;assert(meeting_management_session_stop(s)==ESP_OK&&meeting_management_session_destroy(s)&&destroyed==1);
 return 0;
}
