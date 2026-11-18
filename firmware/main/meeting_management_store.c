#include "meeting_management_store.h"
#include "meeting_agent_mgmt.h"
#include "meeting_ca.h"
#include "meeting_config_store_nvs.h"
#include "meeting_management_credential_nvs.h"
#include "meeting_wifi_slots_nvs.h"
#include "meeting_wifi_switch_nvs.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static void wipe(void *ptr,size_t n){volatile unsigned char *p=ptr;while(n--)*p++=0;}
static bool bootstrap_slots(meeting_management_store *s)
{
    meeting_wifi_pointer pointer;int result=s->slots.read_pointer(s->slots.ctx,&pointer);
    if(result==0){meeting_wifi_slot active={0};unsigned index;
        bool ok=meeting_wifi_slots_load(&s->slots,&active,&index)==MEETING_WIFI_SLOTS_OK;
        wipe(&active,sizeof(active));return ok;}
    if(result!=1||s->transaction.present)return false;
    nvs_handle_t config_handle=0;meeting_config_store_io config_io;
    meeting_config *config=calloc(1,sizeof(*config));uint8_t *scratch=malloc(MEETING_CONFIG_RECORD_MAX);
    meeting_wifi_slot initial={.generation=1};bool ok=false;
    if(!config||!scratch){ESP_LOGE("meeting_mgmt","Config bootstrap allocation failed");goto done;}
    /* The loader recommits and rereads to resolve ambiguous NVS writes. */
    if(nvs_open("meeting_config",NVS_READWRITE,&config_handle)!=ESP_OK){ESP_LOGE("meeting_mgmt","Config namespace unavailable");goto done;}
    if(!meeting_config_store_nvs_bind(&config_handle,&config_io)||
       meeting_config_store_load(&config_io,meeting_ca_validate_pem,NULL,scratch,config)!=MEETING_CONFIG_STORED){ESP_LOGE("meeting_mgmt","Config bootstrap read failed");goto done;}
    memcpy(initial.ssid,config->ssid,sizeof(initial.ssid));memcpy(initial.password,config->password,sizeof(initial.password));
    ok=meeting_wifi_slots_initialize(&s->slots,&initial)==MEETING_WIFI_SLOTS_OK;
    if(!ok)ESP_LOGE("meeting_mgmt","Wi-Fi slot initialization failed");
done:
    if(config_handle)nvs_close(config_handle);
    if(config){meeting_config_clear(config);free(config);}
    if(scratch){wipe(scratch,MEETING_CONFIG_RECORD_MAX);free(scratch);}
    wipe(&initial,sizeof(initial));return ok;
}
bool meeting_management_store_open(meeting_management_store *s,const uint8_t mac[6])
{
    if(!s||!mac||s->identity_handle||s->wifi_handle||s->session)return false;
    meeting_mgmt_record credential={0};bool ok=false;
    if(!meeting_agent_mgmt_device_id(mac,s->device_id)||
       nvs_open("meeting_mgmt",NVS_READONLY,&s->identity_handle)!=ESP_OK||
       !meeting_mgmt_nvs_bind(&s->identity_handle,&s->identity)||
       meeting_mgmt_load(&s->identity,&credential)!=MEETING_MGMT_OK||credential.state!=MEETING_MGMT_ACTIVE||
       memcmp(credential.device_id,s->device_id,16)){ESP_LOGE("meeting_mgmt","Management identity unavailable");goto done;}
    if(nvs_open("meeting_wifi",NVS_READWRITE,&s->wifi_handle)!=ESP_OK||
       !meeting_wifi_slots_nvs_bind(&s->wifi_handle,&s->slots)||
       !meeting_wifi_switch_nvs_bind(&s->wifi_handle,&s->transaction_io)){ESP_LOGE("meeting_mgmt","Wi-Fi state namespace unavailable");goto done;}
    meeting_wifi_switch_init(&s->transaction,&s->transaction_io);
    if(meeting_wifi_switch_recover(&s->transaction)!=MEETING_SWITCH_OK){ESP_LOGE("meeting_mgmt","Transaction recovery failed");goto done;}
    if(!bootstrap_slots(s))goto done;
    if(s->transaction.present&&s->transaction.record.state==MEETING_SWITCH_WAITING){
        meeting_wifi_slot active={0};unsigned index;
        bool loaded=meeting_wifi_slots_load(&s->slots,&active,&index)==MEETING_WIFI_SLOTS_OK&&active.generation!=UINT64_MAX&&
            meeting_wifi_operation_restore_waiting(&s->operation,&s->transaction,&s->slots,active.generation+1)==MEETING_OP_READY;
        wipe(&active,sizeof(active));if(!loaded)goto done;
    }
    s->session=meeting_management_session_create(&s->transaction,&s->slots,&s->operation);
    ok=s->session!=NULL;
    if(!ok)ESP_LOGE("meeting_mgmt","Management session allocation failed");
done:
    meeting_mgmt_clear(&credential);
    if(!ok)(void)meeting_management_store_close(s);
    return ok;
}
bool meeting_management_store_close(meeting_management_store *s)
{
    if(!s)return false;
    if(s->session&&!meeting_management_session_destroy(s->session))return false;
    if(s->wifi_handle)nvs_close(s->wifi_handle);
    if(s->identity_handle)nvs_close(s->identity_handle);
    memset(s,0,sizeof(*s));return true;
}
