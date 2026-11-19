#include "meeting_management_controller.h"
#include "meeting_management_store.h"
#include "meeting_management_credential_nvs.h"
#include "meeting_agent_mgmt.h"
#include "meeting_network.h"
#include "meeting_wifi_trial_sta.h"
#include "meeting_upload_activity.h"
#include <assert.h>
#include <string.h>
unsigned heap_caps_get_free_size(unsigned caps){(void)caps;return 0;}
unsigned heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 0;}
static bool ready,requested,stopped,ack,connected,authenticated;static int start_trial_result=MEETING_OP_RUNNING;static int session_open,closed,starts,trial_result,event,applied;
bool meeting_pairing_ble_connected(void){return connected;}
bool meeting_pairing_ble_authenticated(void){return authenticated;}
esp_err_t nvs_open(const char *n,int m,nvs_handle_t *out){(void)n;(void)m;*out=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
bool meeting_agent_mgmt_device_id(const uint8_t m[6],uint8_t id[16]){memset(id,0,16);memcpy(id,m,6);return true;}
bool meeting_mgmt_nvs_bind(nvs_handle_t *h,meeting_mgmt_io *io){(void)h;(void)io;return true;}
meeting_mgmt_result meeting_mgmt_load(const meeting_mgmt_io *io,meeting_mgmt_record *r){(void)io;*r=(meeting_mgmt_record){.state=MEETING_MGMT_ACTIVE,.device_id={1}};return MEETING_MGMT_OK;}
void meeting_mgmt_clear(meeting_mgmt_record *r){memset(r,0,sizeof(*r));}
void meeting_network_management_request(void){requested=true;}
void meeting_network_management_release(void){requested=false;}
bool meeting_network_management_ready(void){return ready&&requested;}
bool meeting_management_store_open(meeting_management_store *s,const uint8_t m[6]){(void)m;s->session=(void *)1;return true;}
bool meeting_management_store_close(meeting_management_store *s){memset(s,0,sizeof(*s));closed++;return true;}
esp_err_t meeting_management_session_start(meeting_management_session *s,const meeting_mgmt_io *i,const uint8_t id[16]){(void)s;(void)i;(void)id;session_open=1;starts++;return ESP_OK;}
esp_err_t meeting_management_session_stop(meeting_management_session *s){(void)s;if(!stopped)return ESP_FAIL;session_open=0;return ESP_OK;}
meeting_session_result meeting_management_session_poll(meeting_management_session *s){(void)s;int e=event;event=0;return e?MEETING_SESSION_PREPARED:MEETING_SESSION_IDLE;}
bool meeting_management_session_acknowledged(meeting_management_session *s){(void)s;return ack;}
bool meeting_wifi_trial_sta_bind(meeting_wifi_trial_sta *o,meeting_wifi_trial_io *i){(void)o;(void)i;return true;}
meeting_wifi_operation_result meeting_wifi_operation_start_trial(meeting_wifi_operation *o,const meeting_wifi_trial_io *i,uint64_t n){(void)o;(void)i;(void)n;return start_trial_result;}
meeting_wifi_operation_result meeting_wifi_operation_poll(meeting_wifi_operation *o,uint64_t n,bool r){(void)o;(void)n;(void)r;return trial_result;}
bool meeting_network_apply_wifi_slot(const meeting_wifi_slots_io *s,uint64_t g,const uint8_t id[16],const uint8_t digest[32]){(void)s;(void)g;(void)id;(void)digest;applied++;return true;}
int main(void){
 uint8_t mac[6]={1};stopped=true;assert(meeting_management_controller_init(mac));
 meeting_management_controller_tick(19999,true);assert(!requested);
 meeting_management_controller_tick(24999,true);assert(!requested);
 assert(meeting_management_controller_request());
 meeting_management_controller_tick(25000,true);assert(requested&&meeting_management_controller_owns_radio());
 ready=true;meeting_management_controller_tick(25001,true);assert(session_open&&starts==1&&meeting_management_controller_window_active());
 connected=true;assert(meeting_management_controller_window_connected());connected=false;
 authenticated=true;assert(meeting_management_controller_window_authenticated());authenticated=false;
 assert(meeting_management_controller_preempt(25002));assert(!session_open&&!requested&&closed==1);
 assert(!meeting_management_controller_window_active());
 assert(meeting_management_controller_request());
 meeting_management_controller_tick(50002,true);assert(requested);
 meeting_management_controller_tick(50003,true);assert(session_open);
 connected=true;meeting_management_controller_tick(59000,true);assert(session_open);
 event=1;ack=true;meeting_management_controller_tick(59001,true);assert(!session_open&&requested&&meeting_management_controller_trial_active());
 trial_result=MEETING_OP_COMPLETE;meeting_management_controller_tick(59002,true);assert(applied==1&&!requested&&closed==2&&!meeting_management_controller_trial_active());
 meeting_management_controller_tick(84002,true);meeting_management_controller_tick(84003,true);assert(session_open);
 stopped=false;assert(!meeting_management_controller_preempt(84004));assert(session_open&&requested);
 stopped=true;assert(meeting_management_controller_preempt(84005));assert(!session_open&&!requested);
 meeting_management_controller_tick(1000000,true);assert(!requested);
 assert(meeting_management_controller_request());
 meeting_management_controller_tick(1000001,true);assert(requested);
 meeting_management_controller_cancel();
 meeting_management_controller_tick(1000002,true);assert(!requested);
 assert(meeting_management_controller_request());
 meeting_management_controller_tick(1000010,true);
 meeting_management_controller_tick(1000011,true);assert(session_open);
 meeting_management_controller_tick(1120010,true);assert(session_open);
 connected=true;
 meeting_management_controller_tick(1120011,true);assert(session_open&&requested);
 meeting_management_controller_tick(2000000,true);assert(session_open&&requested);
 connected=false;
 meeting_management_controller_tick(2000001,true);assert(session_open&&requested);
 assert(meeting_management_controller_toggle());
 meeting_management_controller_tick(2000002,true);assert(!session_open&&!requested);
 assert(meeting_management_controller_toggle());
 meeting_management_controller_tick(2000010,true);assert(requested);
 meeting_management_controller_tick(2000011,true);assert(session_open);
 assert(meeting_management_controller_toggle());
 meeting_management_controller_tick(2000012,true);assert(!session_open&&!requested);
 meeting_management_controller_tick(2200000,true);assert(!requested);
 return 0;
}
