#include "meeting_store_upload.h"
#include "sc_store.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_MEETING_SUPERVISED_TEST_REPLAY_UPLOAD
#include "meeting_commission.h"
#include <inttypes.h>
#include <string.h>
static bool replay_complete;
static bool replay_snapshot(uint32_t file_id,const meeting_snapshot *source,meeting_snapshot *replay)
{
    if(!source||!replay||file_id!=CONFIG_MEETING_SUPERVISED_TEST_REPLAY_FILE||
       !meeting_commission_allowed()||source->frames!=90000)return false;
    *replay=*source;
    /* A deterministic identity preserves retry semantics across reboots.
     * Flip one bit so it cannot equal the original, then encode the run in
     * the last four bytes; the audio projection and source remain unchanged. */
    replay->meeting_id[0]^=0x80;
    uint32_t run=CONFIG_MEETING_SUPERVISED_TEST_REPLAY_RUN;
    for(unsigned i=0;i<4;i++)replay->meeting_id[12+i]^=(uint8_t)(run>>(8*i));
    return true;
}
#endif

static const char *TAG="meeting_upload";

meeting_upload_result meeting_store_upload(uint32_t file_id, bool has_time,
                                           int64_t recorded_at, uint8_t time_trust,
                                           const meeting_online_config *config)
{
    if (!config) return MEETING_UPLOAD_FAILED;
    FILE *source = sc_store_upload_acquire(file_id);
    if (!source) {ESP_LOGW(TAG,"file %lu: acquire failed",(unsigned long)file_id);return MEETING_UPLOAD_FAILED;}
    meeting_snapshot snapshot;
    meeting_upload_result result = MEETING_UPLOAD_FAILED;
    meeting_manifest manifest;
    ESP_LOGI(TAG,"file %lu: preparing snapshot",(unsigned long)file_id);
    esp_err_t prepared=sc_store_prepare_snapshot(file_id, &snapshot,config->https.is_cancelled,config->https.cancel_ctx);
    if(prepared==ESP_OK)ESP_LOGI(TAG,"file %lu: snapshot ready",(unsigned long)file_id);
    if (prepared == ESP_OK) {
#if CONFIG_MEETING_SUPERVISED_TEST_REPLAY_UPLOAD
        meeting_snapshot replay;
        if(file_id==CONFIG_MEETING_SUPERVISED_TEST_REPLAY_FILE){
            if(!replay_snapshot(file_id,&snapshot,&replay))result=MEETING_UPLOAD_FAILED;
            else if(replay_complete)result=MEETING_UPLOAD_PAUSED;
            else {
                result=meeting_online_session(source,&replay,has_time,recorded_at,time_trust,config,&manifest);
                if(result==MEETING_UPLOAD_CONFIRMED){
                    static const char hex[]="0123456789abcdef";char digest[65];
                    for(unsigned i=0;i<32;i++){digest[2*i]=hex[manifest.digest[i]>>4];digest[2*i+1]=hex[manifest.digest[i]&15];}digest[64]='\0';
                    (void)digest;
                    ESP_LOGI(TAG,"TEST replay confirmed run=%u id=%s bytes=%" PRIu64 " sha256=%s",
                             (unsigned)CONFIG_MEETING_SUPERVISED_TEST_REPLAY_RUN,manifest.id,manifest.bytes,digest);
                    replay_complete=true;result=MEETING_UPLOAD_PAUSED;
                }
            }
        }else
#endif
        {
        sc_confirmation_status status=sc_store_check_confirmation(source,&snapshot,has_time,recorded_at,time_trust,
                                                                  config->https.is_cancelled,config->https.cancel_ctx);
        if (status==SC_CONFIRMATION_PRESENT) result=MEETING_UPLOAD_CONFIRMED;
        else if (status==SC_CONFIRMATION_ABSENT) {
            ESP_LOGI(TAG,"file %lu: preparing upload session",(unsigned long)file_id);
            result = meeting_online_session(source, &snapshot, has_time, recorded_at, time_trust, config, &manifest);
            if(result==MEETING_UPLOAD_FAILED)ESP_LOGW(TAG,"file %lu: remote session failed",(unsigned long)file_id);
            if (result == MEETING_UPLOAD_CONFIRMED && sc_store_confirm_upload(source, &snapshot, &manifest) != ESP_OK)
                result = MEETING_UPLOAD_FAILED;
        } else if(config->https.is_cancelled && config->https.is_cancelled(config->https.cancel_ctx)) result=MEETING_UPLOAD_PAUSED;
        else ESP_LOGW(TAG,"file %lu: confirmation metadata invalid",(unsigned long)file_id);
        }
    } else if(config->https.is_cancelled && config->https.is_cancelled(config->https.cancel_ctx)) result=MEETING_UPLOAD_PAUSED;
    else if(prepared==ESP_ERR_NOT_FOUND)ESP_LOGW(TAG,"file %lu: meeting identity missing; retained for maintenance",(unsigned long)file_id);
    else ESP_LOGW(TAG,"file %lu: snapshot failed: %s",(unsigned long)file_id,esp_err_to_name(prepared));
    if (sc_store_upload_release(source) != ESP_OK) return MEETING_UPLOAD_FAILED;
    return result;
}
