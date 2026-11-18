#include "meeting_network.h"
#include "meeting_ca_compat.h"
#include "meeting_wifi_slot_apply.h"
#include "meeting_sta.h"
#include "meeting_worker.h"
#include "meeting_runtime.h"
#include "meeting_time_device.h"
#include "sc_store.h"
#include "sc_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sdkconfig.h"
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
#include "meeting_commission.h"
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_NEARFULL
#include "meeting_commission.h"
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
#include "meeting_commission.h"
#include "nvs.h"
static atomic_int test_no_wan_phase;
static bool test_no_wan_write(uint8_t value)
{
    nvs_handle_t h;if(nvs_open("meeting_test",NVS_READWRITE,&h)!=ESP_OK)return false;
    esp_err_t err=nvs_set_u8(h,"no_wan2",value);if(err==ESP_OK)err=nvs_commit(h);
    uint8_t readback=0;if(err==ESP_OK)err=nvs_get_u8(h,"no_wan2",&readback);
    nvs_close(h);return err==ESP_OK&&readback==value;
}
#endif

#ifndef CONFIG_MEETING_NETWORK_TASK_STACK_SIZE
#define CONFIG_MEETING_NETWORK_TASK_STACK_SIZE 16384
#endif

#define RETRY_MS 10000
#define UPLOAD_LIST_BATCH 8u
#define FLOOR_INTERVAL_US (300LL*1000000LL)
static meeting_config s_config;
static meeting_clock s_clock;
static meeting_clock_io s_clock_io;
static meeting_time_anchor s_anchor;
static meeting_time_challenge s_challenge;
static char s_http_origin[201];
static bool s_time_restore;
static SemaphoreHandle_t s_clock_lock,s_lifecycle_lock;
static atomic_bool s_enabled,s_stop_requested;
static atomic_bool s_record_requested, s_record_ready;
/* Once the supervisor drains STA, the management controller owns the radio
 * until it releases the window, including during recording preemption. */
/* Low bits are idle/pending/drained; upper bits identify each request. */
enum { MANAGEMENT_IDLE=0, MANAGEMENT_PENDING=1, MANAGEMENT_DRAINED=2, MANAGEMENT_MASK=3 };
static atomic_uint s_management_state;
static bool management_requested(void){return (atomic_load(&s_management_state)&MANAGEMENT_MASK)!=MANAGEMENT_IDLE;}
static void management_set_phase(unsigned phase)
{
    unsigned old=atomic_load(&s_management_state), next;
    do { next=((old&~MANAGEMENT_MASK)+4u)|phase; }
    while(!atomic_compare_exchange_weak(&s_management_state,&old,next));
}
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
static atomic_bool s_test_saved_recording;
#endif
static atomic_int s_state;
static TaskHandle_t s_task;
static int64_t s_last_checkpoint;
static const char *TAG="meeting_network";

bool meeting_network_enabled(void){return atomic_load(&s_enabled);}
meeting_network_state meeting_network_get_state(void){return (meeting_network_state)atomic_load(&s_state);}
static void notify_worker(void)
{
    if(!s_lifecycle_lock||xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)!=pdTRUE)return;
    if(s_task)xTaskNotifyGive(s_task);
    xSemaphoreGive(s_lifecycle_lock);
}
bool meeting_network_stop_request(void)
{
    if(!s_lifecycle_lock)return !meeting_network_enabled();
    if(xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)!=pdTRUE)return false;
    if(meeting_network_enabled()){
        atomic_store(&s_stop_requested,true);
        atomic_store(&s_record_ready,false);
        atomic_store(&s_state,MEETING_NETWORK_STOPPING);
        if(s_task)xTaskNotifyGive(s_task);
    }
    xSemaphoreGive(s_lifecycle_lock);return true;
}
bool meeting_network_stop_complete(void){return !meeting_network_enabled();}
void meeting_network_record_saved(void)
{
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
    if(atomic_load(&s_record_requested)&&meeting_commission_allowed()){
        atomic_store(&test_no_wan_phase,1);
        if(!test_no_wan_write(1))ESP_LOGE(TAG,"TEST no-WAN marker persist failed");
    }
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
    if(atomic_load(&s_record_requested)&&meeting_commission_allowed())atomic_store(&s_test_saved_recording,true);
#endif
    if(meeting_network_enabled())notify_worker();
}
void meeting_network_record_request(void)
{
    atomic_store(&s_record_ready,false);
    atomic_store(&s_record_requested,true);
    if(meeting_network_enabled())notify_worker();
}
bool meeting_network_record_ready(void)
{return !management_requested()&&(!meeting_network_enabled()||(!atomic_load(&s_stop_requested)&&atomic_load(&s_record_ready)));}
void meeting_network_record_release(void)
{
    atomic_store(&s_record_requested,false);
    atomic_store(&s_record_ready,false);
    if(meeting_network_enabled())notify_worker();
}
void meeting_network_management_request(void)
{
    atomic_store(&s_record_ready,false);
    management_set_phase(MANAGEMENT_PENDING);
    if(meeting_network_enabled())notify_worker();
}
bool meeting_network_management_ready(void)
{unsigned state=atomic_load(&s_management_state);return (state&MANAGEMENT_MASK)!=MANAGEMENT_IDLE&&!atomic_load(&s_record_requested)&&
    !meeting_sta_running()&&(!meeting_network_enabled()||(!atomic_load(&s_stop_requested)&&(state&MANAGEMENT_MASK)==MANAGEMENT_DRAINED));}
void meeting_network_management_release(void)
{
    management_set_phase(MANAGEMENT_IDLE);
    if(!atomic_load(&s_record_requested))atomic_store(&s_record_ready,false);
    if(meeting_network_enabled())notify_worker();
}
static char display_ssid[33];

void meeting_network_target_ssid(char out[33])
{
    out[0]=0;
    if(!s_lifecycle_lock||xSemaphoreTake(s_lifecycle_lock,0)!=pdTRUE)return;
    if(meeting_network_enabled())memcpy(out,display_ssid,sizeof(display_ssid));
    xSemaphoreGive(s_lifecycle_lock);
}

bool meeting_network_apply_wifi_slot(const meeting_wifi_slots_io *slots,uint64_t generation,
                                     const uint8_t request_id[16],const uint8_t digest[32])
{
    if(!meeting_network_enabled()||!meeting_network_management_ready()||!slots||!request_id||!digest)return false;
    meeting_wifi_slot slot={0};unsigned index;
    bool ok=meeting_wifi_slots_load(slots,&slot,&index)==MEETING_WIFI_SLOTS_OK&&slot.generation==generation&&
        !memcmp(slot.request_id,request_id,16)&&!memcmp(slot.digest,digest,32)&&meeting_wifi_slot_apply(&s_config,&slot);
    if(ok&&xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)==pdTRUE){
        memcpy(display_ssid,slot.ssid,sizeof(display_ssid));
        xSemaphoreGive(s_lifecycle_lock);
    }
    volatile unsigned char *p=(volatile unsigned char *)&slot;for(size_t i=0;i<sizeof(slot);i++)p[i]=0;
    return ok;
}
static bool has_ip(void *ctx){(void)ctx;return meeting_sta_has_ip();}
static bool cancelled(void *ctx){(void)ctx;return !meeting_network_enabled()||atomic_load(&s_stop_requested)||atomic_load(&s_record_requested)||management_requested();}
static bool clock_cancelled(void *ctx){return cancelled(ctx)||atomic_load(&s_record_requested)||!meeting_sta_has_ip();}
static bool time_valid(void *ctx)
{
    (void)ctx;if(!s_clock_lock||xSemaphoreTake(s_clock_lock,portMAX_DELAY)!=pdTRUE)return false;
    int64_t now_us=esp_timer_get_time();time_t wall=time(NULL);
    bool ok=now_us>=0&&meeting_clock_valid(&s_clock,(int64_t)wall,(uint64_t)now_us/1000);
    if(ok&&now_us-s_last_checkpoint>=FLOOR_INTERVAL_US){
        ok=meeting_clock_checkpoint(&s_clock,&s_clock_io,(int64_t)wall,(uint64_t)now_us/1000);
        if(ok)s_last_checkpoint=now_us;
    }
    xSemaphoreGive(s_clock_lock);return ok;
}
bool meeting_network_trusted_time(int64_t *epoch_seconds)
{
    if(!epoch_seconds||!s_lifecycle_lock||xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)!=pdTRUE)return false;
    bool ok=false;
    if(meeting_network_enabled()&&!atomic_load(&s_stop_requested)&&s_clock_lock&&
       xSemaphoreTake(s_clock_lock,portMAX_DELAY)==pdTRUE){
        int64_t mono=esp_timer_get_time(),wall=(int64_t)time(NULL);
        ok=mono>=0&&meeting_clock_valid(&s_clock,wall,(uint64_t)mono/1000);
        if(ok)*epoch_seconds=wall;
        xSemaphoreGive(s_clock_lock);
    }
    xSemaphoreGive(s_lifecycle_lock);return ok;
}
meeting_clock_view meeting_network_clock_view(int16_t *timezone_minutes)
{
    if(!s_lifecycle_lock)return MEETING_CLOCK_OFF;
    if(xSemaphoreTake(s_lifecycle_lock,0)!=pdTRUE)return MEETING_CLOCK_CHECKING;
    meeting_clock_view result=MEETING_CLOCK_OFF;
    if(meeting_network_enabled()&&!atomic_load(&s_stop_requested)&&s_clock_lock){
        if(xSemaphoreTake(s_clock_lock,0)!=pdTRUE)result=MEETING_CLOCK_CHECKING;
        else {
            int64_t mono=esp_timer_get_time(),wall=(int64_t)time(NULL);
            if(mono>=0&&meeting_clock_valid(&s_clock,wall,(uint64_t)mono/1000)){
                if(timezone_minutes)*timezone_minutes=s_config.timezone_minutes;
                result=MEETING_CLOCK_TRUSTED;
            }
            xSemaphoreGive(s_clock_lock);
        }
    }
    xSemaphoreGive(s_lifecycle_lock);return result;
}
static void task(void *arg)
{
    (void)arg;
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
    bool test_gateway_blocked=false,test_completed=false;
    int64_t test_restore_deadline=0;
    if(meeting_commission_allowed()){
        nvs_handle_t h;uint8_t saved=0;
        if(nvs_open("meeting_test",NVS_READONLY,&h)==ESP_OK){
            esp_err_t err=nvs_get_u8(h,"no_wan2",&saved);nvs_close(h);
            if(err==ESP_OK&&(saved==1||saved==2)){
                atomic_store(&test_no_wan_phase,2);
                if(!test_no_wan_write(2))ESP_LOGE(TAG,"TEST no-WAN restart marker failed");
                test_restore_deadline=esp_timer_get_time()+120000000LL;
                ESP_LOGI(TAG,"TEST no-WAN resumed after reboot");
            }
        }
    }
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
    bool test_armed=false,test_started=false,test_finished=false;
    int64_t test_deadline=0;
    char test_ssid[sizeof(s_config.ssid)],test_password[sizeof(s_config.password)];
#endif
    const char *preferred_ca=meeting_ca_compat_preferred(&s_config);
    const meeting_online_config config={.https={.origin=s_config.origin,.device_token=s_config.device_token,
        .ca_pem=preferred_ca,.fallback_ca_pem=preferred_ca==s_config.ca_pem?NULL:s_config.ca_pem,
        .is_cancelled=cancelled,.time_valid=time_valid},.has_ip=has_ip};
    while(meeting_network_enabled()&&!atomic_load(&s_stop_requested)){
        if(atomic_load(&s_record_requested)||management_requested()){
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
            if(!test_finished&&atomic_load(&s_test_saved_recording))test_armed=true;
#endif
            unsigned management=atomic_load(&s_management_state);
            if((management&MANAGEMENT_MASK)==MANAGEMENT_DRAINED){
                /* The controller will stop its own trial before releasing. */
                atomic_store(&s_record_ready,false);
            }else if(meeting_sta_running()&&meeting_sta_stop()!=ESP_OK){
                atomic_store(&s_record_ready,false);
            }else{
                if((management&MANAGEMENT_MASK)==MANAGEMENT_PENDING){
                    unsigned expected=management;
                    (void)atomic_compare_exchange_strong(&s_management_state,&expected,(management&~MANAGEMENT_MASK)|MANAGEMENT_DRAINED);
                }
                atomic_store(&s_record_ready,!management_requested());
            }
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(RETRY_MS));
            continue;
        }
#if CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS > 0
        if(!test_finished&&atomic_exchange(&s_test_saved_recording,false))test_armed=true;
        if(test_armed&&!test_started){
            memcpy(test_ssid,s_config.ssid,sizeof(test_ssid));
            memcpy(test_password,s_config.password,sizeof(test_password));
            strcpy(s_config.ssid,"meeting-test-offline-no-ap");s_config.password[0]=0;
            test_deadline=esp_timer_get_time()+(int64_t)CONFIG_MEETING_SUPERVISED_TEST_OFFLINE_SECONDS*1000000LL;
            test_started=true;ESP_LOGI(TAG,"TEST offline interval started");
        }
        if(test_started&&!test_finished&&esp_timer_get_time()>=test_deadline){
            if(meeting_sta_running()&&meeting_sta_stop()!=ESP_OK){vTaskDelay(pdMS_TO_TICKS(RETRY_MS));continue;}
            memcpy(s_config.ssid,test_ssid,sizeof(test_ssid));memcpy(s_config.password,test_password,sizeof(test_password));
            memset(test_password,0,sizeof(test_password));test_finished=true;
            ESP_LOGI(TAG,"TEST offline interval complete; reconnecting");
        }
        if(test_started&&!test_finished){
            if(!meeting_sta_running())(void)meeting_sta_start(s_config.ssid,s_config.password);
            else if(!meeting_sta_has_ip())(void)meeting_sta_reconnect();
            atomic_store(&s_state,MEETING_NETWORK_CONNECTING);
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(RETRY_MS));continue;
        }
#endif
        atomic_store(&s_record_ready,false);
        (void)meeting_runtime_resume_cleanup();
        meeting_delete_pass_result deleted=meeting_runtime_resume_deletions(&config);
        if(deleted.failed)ESP_LOGW(TAG,"delete recovery needs retry: %u",deleted.failed);
        if(!meeting_sta_running())(void)meeting_sta_start(s_config.ssid,s_config.password);
        else if(!meeting_sta_has_ip()){
            if(meeting_sta_reconnect()==ESP_ERR_INVALID_STATE)(void)meeting_sta_stop();
        }
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
        int test_phase=atomic_load(&test_no_wan_phase);
        if(test_phase&&meeting_sta_has_ip()&&!test_gateway_blocked&&!test_completed){
            esp_err_t result=meeting_sta_test_no_gateway();
            if(result!=ESP_OK){ESP_LOGE(TAG,"TEST no-WAN gateway isolation failed: %s",esp_err_to_name(result));vTaskDelay(pdMS_TO_TICKS(RETRY_MS));continue;}
            test_gateway_blocked=true;ESP_LOGI(TAG,"TEST Wi-Fi connected, default gateway removed, phase=%d",test_phase);
        }
        if(test_phase&&!test_gateway_blocked){(void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(RETRY_MS));continue;}
        if(test_phase==2&&test_gateway_blocked&&esp_timer_get_time()>=test_restore_deadline){
            if(meeting_sta_stop()!=ESP_OK||!test_no_wan_write(0)){vTaskDelay(pdMS_TO_TICKS(RETRY_MS));continue;}
            atomic_store(&test_no_wan_phase,0);test_completed=true;test_gateway_blocked=false;
            ESP_LOGI(TAG,"TEST no-WAN interval complete; reconnecting with DHCP");continue;
        }
#endif
        if(!meeting_sta_has_ip())atomic_store(&s_state,MEETING_NETWORK_CONNECTING);
        else if(!time_valid(NULL)){
            atomic_store(&s_state,MEETING_NETWORK_CLOCK_REQUIRED);
            if(s_time_restore&&!clock_cancelled(NULL)&&xSemaphoreTake(s_clock_lock,portMAX_DELAY)==pdTRUE){
                meeting_clock_reset(&s_clock);
                meeting_time_client_result synced=meeting_time_device_sync(&s_challenge,s_config.origin,s_http_origin,
                    &s_anchor,clock_cancelled,NULL,&s_clock_io,&s_clock);
                if(synced==MEETING_TIME_CLIENT_OK){s_last_checkpoint=esp_timer_get_time();ESP_LOGI(TAG,"Signed time synchronized");}
                else if(synced==MEETING_TIME_CLIENT_FAILED)ESP_LOGW(TAG,"Signed time unavailable; will retry");
                xSemaphoreGive(s_clock_lock);
            }
        }
        else atomic_store(&s_state,MEETING_NETWORK_SYNCING);
        if(atomic_load(&s_record_requested)||management_requested())continue;
        if(meeting_sta_has_ip()&&time_valid(NULL))atomic_store(&s_state,MEETING_NETWORK_SYNCING);
        meeting_worker_result result={0};
        sc_file_entry_t *entries=calloc(UPLOAD_LIST_BATCH,sizeof(*entries));
        if(entries){if(time_valid(NULL))result=meeting_worker_pass(&config,entries,UPLOAD_LIST_BATCH);free(entries);}
        else {result.failed=1;ESP_LOGW(TAG,"Unable to allocate upload file list");}
        if(result.attempted)ESP_LOGI(TAG,"upload pass attempted=%u confirmed=%u failed=%u",
            (unsigned)result.attempted,(unsigned)result.confirmed,(unsigned)result.failed);
#if CONFIG_MEETING_SUPERVISED_TEST_NEARFULL
        if(result.confirmed&&sc_store_count()==0&&meeting_commission_allowed()&&
           !sc_store_test_nearfull_cleanup())ESP_LOGW(TAG,"TEST near-full fixture retained for maintenance");
#endif
        if(atomic_load(&s_stop_requested))break;
        if(!meeting_sta_has_ip())atomic_store(&s_state,MEETING_NETWORK_CONNECTING);
        else if(!time_valid(NULL))atomic_store(&s_state,MEETING_NETWORK_CLOCK_REQUIRED);
        else if(result.failed||deleted.failed)atomic_store(&s_state,MEETING_NETWORK_RETRY);
        else atomic_store(&s_state,MEETING_NETWORK_IDLE);
        (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(result.paused?5000:meeting_sta_has_ip()?60000:RETRY_MS));
    }
    if(atomic_load(&s_stop_requested)){
        /* A management trial may still own STA. Its controller must finish
         * or preempt the trial and release the window before shutdown can
         * touch the same driver or erase the active configuration. */
        while(management_requested()){
            ESP_LOGW(TAG,"Waiting for management radio release before shutdown");
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000));
        }
        while(meeting_sta_running()&&meeting_sta_stop()!=ESP_OK){
            ESP_LOGW(TAG,"Wi-Fi shutdown failed; retaining configuration until retry");
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if(s_lifecycle_lock&&xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)==pdTRUE){
            meeting_config_clear(&s_config);meeting_clock_reset(&s_clock);
            memset(&s_anchor,0,sizeof(s_anchor));memset(s_http_origin,0,sizeof(s_http_origin));s_time_restore=false;
            memset(&s_clock_io,0,sizeof(s_clock_io));
            if(s_clock_lock){vSemaphoreDelete(s_clock_lock);s_clock_lock=NULL;}
            s_task=NULL;atomic_store(&s_state,MEETING_NETWORK_OFF);atomic_store(&s_enabled,false);
            xSemaphoreGive(s_lifecycle_lock);
        }
    }else atomic_store(&s_state,MEETING_NETWORK_OFF);
    vTaskDelete(NULL);
}
static esp_err_t activate(const meeting_config *config,const meeting_clock *clock,const meeting_clock_io *clock_io,
                          const meeting_time_anchor *anchor,const char *http_origin)
{
    bool restore=anchor&&http_origin;
    if(!config||!clock||(!clock->trusted&&!restore)||!clock_io||!clock_io->commit_floor||meeting_network_enabled()||
       (restore&&(!clock_io->read_floor||!clock_io->set_wall_time||!http_origin[0]||strlen(http_origin)>200))||
       !config->ssid[0]||!config->origin[0]||!config->device_token[0]||!config->ca_pem[0]||sc_wifi_running())return ESP_ERR_INVALID_STATE;
    if(!s_lifecycle_lock){s_lifecycle_lock=xSemaphoreCreateMutex();if(!s_lifecycle_lock)return ESP_ERR_NO_MEM;}
    atomic_store(&s_stop_requested,false);
    s_clock_lock=xSemaphoreCreateMutex();if(!s_clock_lock){ESP_LOGE(TAG,"Clock lock allocation failed");return ESP_ERR_NO_MEM;}
    s_config=*config;s_clock=*clock;s_clock_io=*clock_io;s_last_checkpoint=esp_timer_get_time();
    s_time_restore=restore;
    if(restore){s_anchor=*anchor;strcpy(s_http_origin,http_origin);}
    if(!restore&&!time_valid(NULL)){
        meeting_config_clear(&s_config);meeting_clock_reset(&s_clock);vSemaphoreDelete(s_clock_lock);s_clock_lock=NULL;
        return ESP_ERR_INVALID_STATE;
    }
    if(xSemaphoreTake(s_lifecycle_lock,portMAX_DELAY)!=pdTRUE){
        meeting_config_clear(&s_config);meeting_clock_reset(&s_clock);
        vSemaphoreDelete(s_clock_lock);s_clock_lock=NULL;return ESP_FAIL;
    }
    memcpy(display_ssid,config->ssid,sizeof(display_ssid));
    atomic_store(&s_enabled,true);
    atomic_store(&s_state,MEETING_NETWORK_CONNECTING);
    BaseType_t created=xTaskCreate(task,"meeting_network",CONFIG_MEETING_NETWORK_TASK_STACK_SIZE,NULL,4,&s_task);
    if(created!=pdPASS){
        ESP_LOGE(TAG,"Network task allocation failed: free=%u largest=%u",(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        s_task=NULL;
        atomic_store(&s_enabled,false);atomic_store(&s_state,MEETING_NETWORK_OFF);meeting_config_clear(&s_config);meeting_clock_reset(&s_clock);
        vSemaphoreDelete(s_clock_lock);s_clock_lock=NULL;xSemaphoreGive(s_lifecycle_lock);return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_lifecycle_lock);
    return ESP_OK;
}
esp_err_t meeting_network_activate(const meeting_config *config,const meeting_clock *clock,const meeting_clock_io *clock_io)
{return activate(config,clock,clock_io,NULL,NULL);}
esp_err_t meeting_network_activate_restored(const meeting_config *config,const meeting_time_anchor *anchor,
                                           const char *http_origin,const meeting_clock_io *clock_io)
{meeting_clock clock={0};if(!anchor||!http_origin)return ESP_ERR_INVALID_ARG;return activate(config,&clock,clock_io,anchor,http_origin);}
