#include "meeting_restore.h"
#include "meeting_ca.h"
#include "meeting_clock_nvs.h"
#include "meeting_config_store_nvs.h"
#include "meeting_network.h"
#include "meeting_wifi_boot.h"
#include "meeting_time_anchor_nvs.h"
#include "meeting_agent_anchor.h"
#include "meeting_commission.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

/* The worker keeps a pointer to this handle for its lifetime. */
static nvs_handle_t clock_handle;
static bool clock_open;
static meeting_time_anchor_session anchor_session;

esp_err_t meeting_restore_start(void)
{
 if(clock_open||meeting_network_enabled())return ESP_ERR_INVALID_STATE;
 esp_err_t result=ESP_FAIL; nvs_handle_t config_handle=0,anchor_handle=0;
 meeting_config *config=calloc(1,sizeof(*config));uint8_t *scratch=malloc(MEETING_CONFIG_RECORD_MAX);
 if(!config||!scratch){result=ESP_ERR_NO_MEM;goto done;}
 int anchor_mode=NVS_READONLY;
 const char *anchor_namespace="meeting_timekey";
#if CONFIG_MEETING_AGENT_TIME_PROVISION
 anchor_mode=NVS_READWRITE;
#if CONFIG_MEETING_AGENT_TEST_ANCHOR_NAMESPACE
 if(!meeting_commission_allowed())goto done;
 anchor_namespace="meeting_testkey";
#if CONFIG_MEETING_AGENT_TIME_VERIFY_ONLY
 anchor_mode=NVS_READONLY;
#endif
#endif
#endif
 if(nvs_open("meeting_config",NVS_READWRITE,&config_handle)!=ESP_OK||
    nvs_open(anchor_namespace,anchor_mode,&anchor_handle)!=ESP_OK||
    nvs_open("meeting_clock",NVS_READWRITE,&clock_handle)!=ESP_OK)goto done;
 clock_open=true;
 meeting_config_store_io store;meeting_time_anchor_io anchor_io;meeting_clock_io clock_io;
 if(!meeting_config_store_nvs_bind(&config_handle,&store)||!meeting_time_anchor_nvs_bind(&anchor_handle,&anchor_io)||
    !meeting_clock_nvs_bind(&clock_handle,&clock_io)||
    meeting_config_store_load(&store,meeting_ca_validate_pem,NULL,scratch,config)!=MEETING_CONFIG_STORED)goto done;
 if(!meeting_wifi_boot_restore(config))goto done;
 int64_t floor;if(clock_io.read_floor(clock_io.ctx,&floor)!=0)goto done;
 uint8_t origin_hash[32];meeting_time_anchor anchor;
 if(mbedtls_sha256((const uint8_t *)config->origin,strlen(config->origin),origin_hash,0)!=0)goto done;
#if CONFIG_MEETING_AGENT_TIME_PROVISION
#if CONFIG_MEETING_AGENT_TIME_VERIFY_ONLY
 if(meeting_time_anchor_load(&anchor_session,&anchor_io,origin_hash,meeting_time_public_key_valid,NULL,&anchor)!=MEETING_ANCHOR_OK||
    !meeting_agent_anchor_matches(&anchor,origin_hash,CONFIG_MEETING_AGENT_TIME_ORIGIN_SHA256,
       CONFIG_MEETING_AGENT_TIME_PUBLIC_KEY,meeting_commission_allowed()))goto done;
 ESP_LOGI("meeting_restore","Agent test time anchor readback PASS");
#else
 if(meeting_agent_anchor_install(&anchor_session,&anchor_io,origin_hash,CONFIG_MEETING_AGENT_TIME_ORIGIN_SHA256,
    CONFIG_MEETING_AGENT_TIME_PUBLIC_KEY,meeting_commission_allowed(),meeting_time_public_key_valid,NULL)!=MEETING_ANCHOR_OK)goto done;
 /* This maintenance image is not an upload image. The agent checks this
  * marker then restores the normal application without erasing NVS. */
 ESP_LOGI("meeting_restore","Agent time anchor committed; install normal application");
#endif
 result=ESP_ERR_INVALID_STATE;goto done;
#endif
 if(
    meeting_time_anchor_load(&anchor_session,&anchor_io,origin_hash,meeting_time_public_key_valid,NULL,&anchor)!=MEETING_ANCHOR_OK)goto done;
 /* Same host, default HTTP port; the HTTP transport enforces host syntax and
  * origin matching, and never sends the stored device token. */
 if(strncmp(config->origin,"https://",8))goto done;
 const char *host=config->origin+8;size_t length=strcspn(host,":");
 if(!length||length>180)goto done;
 char http_origin[201];memcpy(http_origin,"http://",7);memcpy(http_origin+7,host,length);http_origin[7+length]=0;
 result=meeting_network_activate_restored(config,&anchor,http_origin,&clock_io);
done:
 if(anchor_handle)nvs_close(anchor_handle);
 if(config_handle)nvs_close(config_handle);
 if(result!=ESP_OK&&clock_open){nvs_close(clock_handle);clock_open=false;}
 if(scratch){memset(scratch,0,MEETING_CONFIG_RECORD_MAX);free(scratch);}
 if(config){meeting_config_clear(config);free(config);}
 return result;
}
