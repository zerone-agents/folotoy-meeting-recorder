#include "meeting_management_controller.h"
#include "meeting_management_store.h"
#include "meeting_management_credential_nvs.h"
#include "meeting_agent_mgmt.h"
#include "meeting_network.h"
#include "meeting_pairing_ble.h"
#include "meeting_wifi_trial_sta.h"
#include "meeting_upload_priority.h"
#include "sdkconfig.h"
#if CONFIG_MEETING_SUPERVISED_TEST_COMMIT_HOLD
#include "meeting_commission.h"
#endif
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

enum {DRAIN_MS=3000,WINDOW_MS=8000,CONNECTED_WINDOW_MS=30000,ACK_MS=2000};
typedef enum {CTRL_OFF,CTRL_ONLINE,CTRL_DRAIN,CTRL_BLE,CTRL_ACK,CTRL_TRIAL,CTRL_FAULT} controller_state;
static controller_state state;
static uint8_t device_mac[6];
static uint64_t since;
static meeting_management_store store;
static meeting_wifi_trial_sta radio_owner;
static meeting_wifi_trial_io radio;
static bool cleanup_retryable;
static meeting_upload_priority priority;
static bool elapsed(uint64_t now,uint64_t start,uint64_t limit){return now>=start&&now-start>=limit;}
static uint64_t unconnected_window_ms(void){
#if CONFIG_MEETING_SUPERVISED_TEST_COMMIT_HOLD
    if(meeting_commission_allowed())return 30000;
#endif
    return WINDOW_MS;
}
static void fail(void){cleanup_retryable=false;state=CTRL_FAULT;ESP_LOGE("meeting_mgmt","Management radio requires recovery");}
static void fail_cleanup(void){cleanup_retryable=true;state=CTRL_FAULT;ESP_LOGE("meeting_mgmt","Management cleanup requires retry");}
bool meeting_management_controller_init(const uint8_t mac[6])
{
    if(!mac||state!=CTRL_OFF)return false;
    nvs_handle_t handle=0;meeting_mgmt_io io;meeting_mgmt_record credential={0};uint8_t id[16];
    bool ok=meeting_agent_mgmt_device_id(mac,id)&&nvs_open("meeting_mgmt",NVS_READONLY,&handle)==ESP_OK&&
        meeting_mgmt_nvs_bind(&handle,&io)&&meeting_mgmt_load(&io,&credential)==MEETING_MGMT_OK&&
        credential.state==MEETING_MGMT_ACTIVE&&!memcmp(credential.device_id,id,16);
    meeting_mgmt_clear(&credential);if(handle)nvs_close(handle);
    if(!ok)return false;
    memcpy(device_mac,mac,6);state=CTRL_ONLINE;since=0;meeting_upload_priority_reset(&priority,0);return true;
}
bool meeting_management_controller_owns_radio(void)
{return state!=CTRL_OFF&&state!=CTRL_ONLINE;}
bool meeting_management_controller_window_active(void)
{return state==CTRL_BLE||state==CTRL_ACK;}
bool meeting_management_controller_window_connected(void)
{return meeting_management_controller_window_active()&&meeting_pairing_ble_connected();}
bool meeting_management_controller_window_authenticated(void)
{return meeting_management_controller_window_active()&&meeting_pairing_ble_authenticated();}
bool meeting_management_controller_trial_active(void)
{return state==CTRL_TRIAL;}
static bool finish(uint64_t now)
{
    bool drain_failed=state==CTRL_DRAIN;
    if(store.session&&meeting_management_session_stop(store.session)!=ESP_OK)return false;
    if(radio_owner.owned)return false;
    if(!meeting_management_store_close(&store))return false;
    meeting_network_management_release();cleanup_retryable=false;state=CTRL_ONLINE;since=now;
    meeting_upload_priority_window_end(&priority,now,drain_failed);return true;
}
bool meeting_management_controller_preempt(uint64_t now)
{
    if(state==CTRL_OFF||state==CTRL_ONLINE)return true;
    if(state==CTRL_FAULT)return cleanup_retryable&&finish(now);
    if(state==CTRL_TRIAL){
        meeting_wifi_operation_result result=meeting_wifi_operation_poll(&store.operation,now,true);
        if(result!=MEETING_OP_WAITING)return false;
    }
    if(!finish(now)){fail_cleanup();return false;}
    return true;
}
void meeting_management_controller_tick(uint64_t now,bool idle)
{
    if(state==CTRL_OFF||state==CTRL_FAULT)return;
    if(state==CTRL_ONLINE){
        if(!idle){since=now;meeting_upload_priority_window_end(&priority,now,false);return;}
        meeting_activity_snapshot activity=meeting_upload_activity_snapshot();
        if(!meeting_upload_priority_tick(&priority,now,&activity).request_window)return;
        meeting_network_management_request();state=CTRL_DRAIN;since=now;return;
    }
    if(!idle){(void)meeting_management_controller_preempt(now);return;}
    if(state==CTRL_DRAIN){
        if(elapsed(now,since,DRAIN_MS)){if(!finish(now))fail_cleanup();return;}
        if(!meeting_network_management_ready())return;
        if(!meeting_management_store_open(&store,device_mac)){
            ESP_LOGE("meeting_mgmt","Management storage initialization failed: free=%u largest=%u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            if(!finish(now))fail_cleanup();
            return;
        }
        if(store.operation.state==MEETING_OP_READY){
            if(!meeting_wifi_trial_sta_bind(&radio_owner,&radio)){fail();return;}
            meeting_wifi_operation_result started=meeting_wifi_operation_start_trial(&store.operation,&radio,now);
            if(started==MEETING_OP_ROLLED_BACK){if(!finish(now))fail_cleanup();return;}
            if(started!=MEETING_OP_RUNNING){fail();return;}
            state=CTRL_TRIAL;return;
        }
        esp_err_t start_result=meeting_management_session_start(store.session,&store.identity,store.device_id);
        if(start_result!=ESP_OK){
            ESP_LOGE("meeting_mgmt","Management BLE start failed: %s",esp_err_to_name(start_result));
            if(!finish(now))fail_cleanup();
            return;
        }
        state=CTRL_BLE;since=now;return;
    }
    if(state==CTRL_BLE||state==CTRL_ACK){
        if(state==CTRL_BLE&&meeting_management_session_poll(store.session)==MEETING_SESSION_PREPARED){state=CTRL_ACK;since=now;}
        if(state==CTRL_ACK&&(meeting_management_session_acknowledged(store.session)||elapsed(now,since,ACK_MS))){
            if(meeting_management_session_stop(store.session)!=ESP_OK){fail_cleanup();return;}
            if(!meeting_wifi_trial_sta_bind(&radio_owner,&radio)){fail();return;}
            meeting_wifi_operation_result started=meeting_wifi_operation_start_trial(&store.operation,&radio,now);
            if(started==MEETING_OP_ROLLED_BACK){if(!finish(now))fail_cleanup();return;}
            if(started!=MEETING_OP_RUNNING){fail();return;}
            state=CTRL_TRIAL;return;
        }
        if(state==CTRL_BLE&&elapsed(now,since,meeting_pairing_ble_connected()?CONNECTED_WINDOW_MS:unconnected_window_ms())){if(!finish(now))fail_cleanup();}
        return;
    }
    if(state==CTRL_TRIAL){
        meeting_wifi_operation_result result=meeting_wifi_operation_poll(&store.operation,now,false);
        if(result==MEETING_OP_RUNNING)return;
        if(result==MEETING_OP_COMPLETE&&!meeting_network_apply_wifi_slot(&store.slots,store.operation.generation,
           store.operation.request.id,store.operation.request.digest)){fail();return;}
        if(result!=MEETING_OP_COMPLETE&&result!=MEETING_OP_ROLLED_BACK){fail();return;}
        if(!finish(now))fail_cleanup();
    }
}
