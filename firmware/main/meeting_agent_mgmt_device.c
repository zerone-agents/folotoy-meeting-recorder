#include "meeting_agent_mgmt.h"
#include "meeting_commission.h"
#include "meeting_management_credential_nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <string.h>

#if CONFIG_MEETING_AGENT_MGMT_PROVISION
bool meeting_agent_mgmt_provision_device(void)
{
    if(!meeting_commission_allowed()){ESP_LOGE("meeting_mgmt","Target not authorized");return false;}
    uint8_t mac[6],id[16];
    if(esp_efuse_mac_get_default(mac)!=ESP_OK||!meeting_agent_mgmt_device_id(mac,id)){
        ESP_LOGE("meeting_mgmt","Device identity unavailable");return false;
    }
    nvs_handle_t handle;
    esp_err_t err=nvs_open("meeting_mgmt",NVS_READWRITE,&handle);
    if(err!=ESP_OK){ESP_LOGE("meeting_mgmt","Identity namespace open failed: %s",esp_err_to_name(err));return false;}
    meeting_mgmt_io io;
    bool bound=meeting_mgmt_nvs_bind(&handle,&io);
    meeting_mgmt_result result=MEETING_MGMT_ERROR;
    if(bound){
        const char *action=CONFIG_MEETING_AGENT_MGMT_ACTION;
        if(!strcmp(action,"install"))
            result=meeting_agent_mgmt_install(&io,id,CONFIG_MEETING_AGENT_MGMT_SECRET,true);
        else if(!strcmp(action,"rotate")||!strcmp(action,"revoke"))
            result=meeting_agent_mgmt_maintain(&io,id,CONFIG_MEETING_AGENT_MGMT_SECRET,
                CONFIG_MEETING_AGENT_MGMT_EXPECTED_GENERATION,!strcmp(action,"revoke"),true);
    }
    bool ok=result==MEETING_MGMT_OK;
    if(!ok)ESP_LOGE("meeting_mgmt","Identity installation failed: result=%d",(int)result);
    nvs_close(handle);
    if(ok)ESP_LOGI("meeting_mgmt","Management credential committed; restore normal application");
    return ok;
}
#endif
