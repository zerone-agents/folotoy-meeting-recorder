#include "meeting_management_session.h"
#include "meeting_management_ble.h"
#include "meeting_management_inbox.h"
#include "meeting_management_query.h"
#include "meeting_management_dispatch.h"
#include "meeting_management_ack.h"
#include "meeting_wifi_sha256.h"
#include <stdlib.h>

struct meeting_management_session {
    meeting_config_ble_inbox inbox;
    meeting_wifi_switch *txn;
    const meeting_wifi_slots_io *slots;
    meeting_wifi_operation *operation;
    meeting_pairing_token last_token;
    bool active;
};
meeting_management_session *meeting_management_session_create(meeting_wifi_switch *txn,const meeting_wifi_slots_io *slots,meeting_wifi_operation *op)
{
    if(!txn||!slots||!op)return NULL;
    meeting_management_session *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
    if(!meeting_config_ble_init_sized(&s->inbox,118)){free(s);return NULL;}
    s->txn=txn;s->slots=slots;s->operation=op;return s;
}
esp_err_t meeting_management_session_start(meeting_management_session *s,const meeting_mgmt_io *io,const uint8_t id[16])
{
    if(!s||!io||!id||s->active)return ESP_ERR_INVALID_STATE;
    meeting_config_ble_prepare(&s->inbox);
    /* A partial BLE start can retain callbacks; mark active before calling so
     * the coordinator must stop it even if start returns an error. */
    s->active=true;
    return meeting_management_ble_start(io,id,meeting_config_ble_handle,meeting_config_ble_reset,&s->inbox);
}
meeting_session_result meeting_management_session_poll(meeting_management_session *s)
{
    if(!s||!s->active)return MEETING_SESSION_ERROR;
    meeting_wifi_command command={0};meeting_pairing_token token={0};
    if(!meeting_management_inbox_take(&s->inbox,&command,&token))return MEETING_SESSION_IDLE;
    s->last_token=token;
    meeting_session_result result=MEETING_SESSION_ERROR;
    if(command.kind==MEETING_WIFI_COMMAND_QUERY){
        if(meeting_management_query_publish(&s->inbox,token,s->txn,&command))result=MEETING_SESSION_QUERY;
    }else if(command.kind==MEETING_WIFI_COMMAND_ACK){
        uint8_t code=meeting_management_ack(token,s->txn,s->slots,command.operation_id);
        if(meeting_config_ble_publish_result(&s->inbox,token,code))result=MEETING_SESSION_QUERY;
    }else if(command.kind==MEETING_WIFI_COMMAND_SWITCH){
        bool published=meeting_management_dispatch_switch(&s->inbox,token,s->txn,s->slots,s->operation,&command,meeting_wifi_sha256,NULL);
        if(s->operation->state==MEETING_OP_READY)result=MEETING_SESSION_PREPARED;
        else if(!published)(void)meeting_config_ble_publish_result(&s->inbox,token,23);
    }
    meeting_wifi_command_clear(&command);return result;
}
bool meeting_management_session_acknowledged(meeting_management_session *s)
{return s&&s->active&&meeting_config_ble_acknowledged(&s->inbox,s->last_token);}
esp_err_t meeting_management_session_stop(meeting_management_session *s)
{
    if(!s)return ESP_ERR_INVALID_ARG;
    if(!s->active)return ESP_OK;
    esp_err_t result=meeting_pairing_ble_stop();
    if(result==ESP_OK){s->active=false;meeting_config_ble_prepare(&s->inbox);}
    return result;
}
bool meeting_management_session_destroy(meeting_management_session *s)
{
    if(!s||s->active)return false;
    meeting_config_ble_destroy(&s->inbox);free(s);return true;
}
