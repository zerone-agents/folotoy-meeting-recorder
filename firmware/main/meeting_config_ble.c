#include "meeting_config_ble.h"
#include "meeting_config_protocol.h"
#include <stdlib.h>
#include <string.h>

static void clear(meeting_config_ble_inbox *inbox)
{
    meeting_config_fragments_reset(&inbox->fragments);
    memset(&inbox->token,0,sizeof(inbox->token));
    inbox->delivered=false;inbox->committed=false;inbox->acknowledged=false;inbox->result_code=0;
}
bool meeting_config_ble_init(meeting_config_ble_inbox *inbox)
{return meeting_config_ble_init_sized(inbox,MEETING_CONFIG_MAX_BYTES);}
bool meeting_config_ble_init_sized(meeting_config_ble_inbox *inbox,size_t capacity)
{
    if(!inbox||!capacity||capacity>MEETING_CONFIG_MAX_BYTES)return false;
    memset(inbox,0,sizeof(*inbox));
    atomic_init(&inbox->reset_pending,false);
    atomic_init(&inbox->ack_latched,false);
    inbox->lock=xSemaphoreCreateMutex();
    if(!inbox->lock)return false;
    uint8_t *bytes=malloc(capacity);
    if(!bytes){vSemaphoreDelete(inbox->lock);inbox->lock=NULL;return false;}
    return meeting_config_fragments_bind(&inbox->fragments,bytes,capacity);
}
void meeting_config_ble_prepare(meeting_config_ble_inbox *inbox)
{
    if(!inbox||!inbox->lock)return;
    if(xSemaphoreTake(inbox->lock,portMAX_DELAY)!=pdTRUE)return;
    clear(inbox);memset(&inbox->ack_token,0,sizeof(inbox->ack_token));
    atomic_store(&inbox->ack_latched,false);atomic_store(&inbox->reset_pending,false);
    xSemaphoreGive(inbox->lock);
}
void meeting_config_ble_destroy(meeting_config_ble_inbox *inbox)
{
    if(!inbox||!inbox->lock)return;
    clear(inbox);free(inbox->fragments.bytes);inbox->fragments.bytes=NULL;inbox->fragments.capacity=0;
    vSemaphoreDelete(inbox->lock);inbox->lock=NULL;
}
void meeting_config_ble_reset(void *ctx)
{
    meeting_config_ble_inbox *inbox=ctx;
    if(!inbox||!inbox->lock)return;
    atomic_store(&inbox->reset_pending,true);
    if(xSemaphoreTake(inbox->lock,0)==pdTRUE){
        clear(inbox);atomic_store(&inbox->reset_pending,false);xSemaphoreGive(inbox->lock);
    }
}

esp_err_t meeting_config_ble_handle(uint32_t session,const uint8_t *request,size_t length,
                                    uint8_t **response,ssize_t *response_len,void *ctx)
{
    meeting_config_ble_inbox *inbox=ctx;
    if(!inbox||!inbox->lock||!response||!response_len)return ESP_ERR_INVALID_ARG;
    uint8_t *out=malloc(1);if(!out)return ESP_ERR_NO_MEM;
    out[0]=1;
    if(xSemaphoreTake(inbox->lock,0)==pdTRUE){
        if(atomic_exchange(&inbox->reset_pending,false))clear(inbox);
        bool status_request=request&&length==1&&(request[0]==4||request[0]==5);
        bool ok;
        if(status_request){
            /* The client can poll immediately after sealing, before the
             * coordinator has consumed the request. Report pending instead
             * of rejecting an authenticated request in that interval. */
            ok=(inbox->delivered||inbox->fragments.sealed)&&session==inbox->token.session&&meeting_pairing_ble_session_valid(inbox->token);
            if(ok&&request[0]==5){
                ok=inbox->committed;
                if(ok){inbox->acknowledged=true;atomic_store(&inbox->ack_latched,true);}
            }
        }else ok=!inbox->delivered&&meeting_config_protocol_request(&inbox->fragments,session,request,length);
        if(ok&&inbox->fragments.sealed){
            ok=meeting_pairing_ble_capture(session,&inbox->token);
            if(!ok)clear(inbox);
        }
        if(atomic_load(&inbox->reset_pending)){clear(inbox);ok=false;}
        out[0]=ok?(status_request&&request[0]==4?(inbox->committed?inbox->result_code:0):0):1;
        xSemaphoreGive(inbox->lock);
    }
    *response=out;*response_len=1;return ESP_OK;
}
bool meeting_config_ble_take(meeting_config_ble_inbox *inbox,uint8_t *out,size_t capacity,
                             size_t *length,meeting_pairing_token *token)
{
    if(!inbox||!inbox->lock||!out||!length||!token)return false;
    *length=0;
    if(xSemaphoreTake(inbox->lock,portMAX_DELAY)!=pdTRUE)return false;
    if(atomic_exchange(&inbox->reset_pending,false))clear(inbox);
    size_t size=inbox->fragments.received;
    bool ok=inbox->fragments.sealed&&!inbox->delivered&&size<=capacity&&
            meeting_pairing_ble_session_valid(inbox->token);
    if(ok){
        memcpy(out,inbox->fragments.bytes,size);
        if(atomic_load(&inbox->reset_pending)||!meeting_pairing_ble_session_valid(inbox->token)){
            memset(out,0,size);ok=false;
        }else{
            *length=size;*token=inbox->token;inbox->delivered=true;
            /* The consumer now owns the sole plaintext copy. Keep the inbox
             * closed until session reset so approval cannot be substituted. */
            meeting_config_fragments_reset(&inbox->fragments);
        }
    }
    xSemaphoreGive(inbox->lock);return ok;
}
static bool same_token(meeting_pairing_token a,meeting_pairing_token b)
{return a.session==b.session&&a.generation==b.generation;}
static bool publish(meeting_config_ble_inbox *inbox,meeting_pairing_token token,uint8_t code)
{
    if(!inbox||!inbox->lock||xSemaphoreTake(inbox->lock,portMAX_DELAY)!=pdTRUE)return false;
    bool ok=!atomic_load(&inbox->reset_pending)&&inbox->delivered&&(!inbox->committed||inbox->result_code==code)&&same_token(token,inbox->token)&&
            meeting_pairing_ble_session_valid(token);
    if(ok){inbox->ack_token=token;inbox->result_code=code;inbox->committed=true;}
    xSemaphoreGive(inbox->lock);return ok;
}
bool meeting_config_ble_publish(meeting_config_ble_inbox *inbox,meeting_pairing_token token)
{return publish(inbox,token,2);}
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *inbox,meeting_pairing_token token,uint8_t code)
{return code>=16&&code<=31&&publish(inbox,token,code);}
bool meeting_config_ble_acknowledged(meeting_config_ble_inbox *inbox,meeting_pairing_token token)
{
    if(!inbox||!inbox->lock||xSemaphoreTake(inbox->lock,portMAX_DELAY)!=pdTRUE)return false;
    bool ok=atomic_load(&inbox->ack_latched)&&same_token(token,inbox->ack_token);
    xSemaphoreGive(inbox->lock);return ok;
}
