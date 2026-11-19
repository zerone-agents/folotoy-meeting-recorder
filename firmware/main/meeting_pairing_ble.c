#include "meeting_pairing_ble.h"
#include "meeting_commission.h"
#include "meeting_management_password.h"
#include "sc_ble.h"
#include "sc_wifi.h"
#include "meeting_sta.h"
#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_srp.h"
#include "sdkconfig.h"
#include "protocomm.h"
#include "protocomm_ble.h"
#include "protocomm_security2.h"
#include "freertos/FreeRTOS.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAIRING_TTL_US (120LL*1000000LL)
#define MAX_REQUEST 460
static protocomm_t *pc;
static char *salt,*verifier;
static size_t verifier_size;
static bool transport_started,security_added,handler_added;
static atomic_bool accepting;
static atomic_bool connected;
static portMUX_TYPE peer_lock=portMUX_INITIALIZER_UNLOCKED;
static char peer_name[MEETING_PAIRING_PEER_NAME_SIZE];
static int64_t deadline;
static bool manual_session;
static meeting_pairing_handler app_handler;
static meeting_pairing_reset app_reset;
static void *app_context;
static protocomm_security_t guarded_security;
static atomic_uint handshake_requests;
static meeting_pairing_session session_guard;
static protocomm_ble_name_uuid_t endpoints[]={ {"prov-session",0xff51}, {"meeting-config",0xff52} };

static void reset_session(void)
{
    meeting_pairing_session_reset(&session_guard);
    portENTER_CRITICAL(&peer_lock);
    memset(peer_name,0,sizeof(peer_name));
    portEXIT_CRITICAL(&peer_lock);
    if(app_reset)app_reset(app_context);
}

void meeting_pairing_ble_peer_name(char out[MEETING_PAIRING_PEER_NAME_SIZE])
{
    portENTER_CRITICAL(&peer_lock);
    if(meeting_pairing_ble_authenticated())memcpy(out,peer_name,sizeof(peer_name));
    else out[0]=0;
    portEXIT_CRITICAL(&peer_lock);
}
bool meeting_pairing_ble_capture(uint32_t session,meeting_pairing_token *token)
{return atomic_load(&accepting)&&esp_timer_get_time()<deadline&&meeting_pairing_session_capture(&session_guard,session,token);}
bool meeting_pairing_ble_session_valid(meeting_pairing_token token)
{return atomic_load(&accepting)&&esp_timer_get_time()<deadline&&meeting_pairing_session_valid(&session_guard,token);}
static esp_err_t new_session(protocomm_security_handle_t handle,uint32_t session)
{
    if(manual_session)atomic_store(&handshake_requests,0);
    reset_session();esp_err_t err=protocomm_security2.new_transport_session(handle,session);
    if(err==ESP_OK)atomic_store(&connected,true);
    return err;
}
static esp_err_t close_session(protocomm_security_handle_t handle,uint32_t session)
{
    if(manual_session)atomic_store(&handshake_requests,0);
    atomic_store(&connected,false);reset_session();return protocomm_security2.close_transport_session(handle,session);
}
bool meeting_pairing_ble_connected(void){return atomic_load(&accepting)&&atomic_load(&connected);}
bool meeting_pairing_ble_authenticated(void)
{return atomic_load(&accepting)&&esp_timer_get_time()<deadline&&atomic_load(&session_guard.active);}
static esp_err_t security_request(protocomm_security_handle_t handle,const void *params,uint32_t session,
                                  const uint8_t *input,ssize_t input_len,uint8_t **output,ssize_t *output_len,void *priv)
{
    reset_session();
    /* Two packets per attempt. Bound all handshake packets, including
     * malformed ones; existing authenticated sessions may finish normally. */
    if(!atomic_load(&accepting)||esp_timer_get_time()>=deadline||atomic_fetch_add(&handshake_requests,1)>=6)
        return ESP_ERR_INVALID_STATE;
    /* Some protocomm transports do not deliver new_transport_session before
     * the first handshake packet. A live request is evidence of a connection,
     * not of successful authentication. */
    atomic_store(&connected,true);
    return protocomm_security2.security_req_handler(handle,params,session,input,input_len,output,output_len,priv);
}

static esp_err_t request(uint32_t session,const uint8_t *in,ssize_t len,uint8_t **out,ssize_t *out_len,void *ctx)
{
    (void)ctx;
    if(!atomic_load(&accepting)||esp_timer_get_time()>=deadline||!in||len<=0||len>MAX_REQUEST||
       !out||!out_len||!app_handler)return ESP_ERR_INVALID_STATE;
    meeting_pairing_session_accept(&session_guard,session);
    if(in[0]==6){
        if(len<2||len>MEETING_PAIRING_PEER_NAME_SIZE)return ESP_ERR_INVALID_ARG;
        for(ssize_t i=1;i<len;i++){
            if(in[i]<32||in[i]>126)return ESP_ERR_INVALID_ARG;
        }
        uint8_t *response=calloc(1,1);
        if(!response)return ESP_ERR_NO_MEM;
        portENTER_CRITICAL(&peer_lock);
        memcpy(peer_name,in+1,(size_t)len-1);
        peer_name[len-1]=0;
        portEXIT_CRITICAL(&peer_lock);
        *out=response;
        *out_len=1;
        return ESP_OK;
    }
    return app_handler(session,in,(size_t)len,out,out_len,app_context);
}
static void clear_secrets(void)
{
    if(salt){memset(salt,0,16);free(salt);salt=NULL;}
    if(verifier){memset(verifier,0,verifier_size);free(verifier);verifier=NULL;}
    verifier_size=0;
}
esp_err_t meeting_pairing_ble_stop(void)
{
    atomic_store(&accepting,false);
    atomic_store(&connected,false);
    /* Invalidate tokens immediately, but do not touch application state here:
     * the BLE host may still be handling a request until transport stops. */
    meeting_pairing_session_reset(&session_guard);
    if(!pc)return ESP_OK;
    if(transport_started){
        esp_err_t err=protocomm_ble_stop(pc);
        if(err!=ESP_OK)return err; /* retain state if shutdown is ambiguous */
        transport_started=false;
    }
    if(handler_added){(void)protocomm_remove_endpoint(pc,"meeting-config");handler_added=false;}
    if(security_added){(void)protocomm_unset_security(pc,"prov-session");security_added=false;}
    protocomm_delete(pc);pc=NULL;clear_secrets();reset_session();app_handler=NULL;app_reset=NULL;app_context=NULL;return ESP_OK;
}
static esp_err_t start_password(const char *password, size_t length, bool manual, meeting_pairing_handler handler, meeting_pairing_reset reset, void *ctx)
{
    if(!password||!length||!handler||!reset||pc||sc_ble_running()||sc_wifi_running()||meeting_sta_running())return ESP_ERR_INVALID_STATE;
    int verifier_len=0;
    bootloader_random_enable();
    esp_err_t err=esp_srp_gen_salt_verifier("meeting",7,password,length,&salt,16,&verifier,&verifier_len);
    bootloader_random_disable();
    if(verifier_len>0 && verifier_len<=UINT16_MAX)verifier_size=(size_t)verifier_len;
    if(err!=ESP_OK||!salt||!verifier||verifier_len<=0||verifier_len>UINT16_MAX){clear_secrets();return ESP_FAIL;}
    pc=protocomm_new();if(!pc){clear_secrets();return ESP_ERR_NO_MEM;}
    protocomm_ble_config_t config={.device_name="PASSPORT-SETUP",.nu_lookup_count=2,.nu_lookup=endpoints};
    const uint8_t uuid[16]={0x07,0xed,0x9b,0x2d,0x0f,0x06,0x7c,0x87,0x9b,0x43,0x43,0x6b,0x4d,0x24,0x75,0x17};
    memcpy(config.service_uuid,uuid,sizeof(uuid));
    app_handler=handler;app_reset=reset;app_context=ctx;manual_session=manual;
    deadline=manual?INT64_MAX:esp_timer_get_time()+PAIRING_TTL_US;
    atomic_store(&handshake_requests,0);
    err=protocomm_ble_start(pc,&config);if(err!=ESP_OK)goto fail;transport_started=true;
    protocomm_security2_params_t params={.salt=salt,.salt_len=16,.verifier=verifier,.verifier_len=(uint16_t)verifier_len};
    guarded_security=protocomm_security2;
    guarded_security.new_transport_session=new_session;guarded_security.close_transport_session=close_session;
    guarded_security.security_req_handler=security_request;
    err=protocomm_set_security(pc,"prov-session",&guarded_security,&params);if(err!=ESP_OK)goto fail;security_added=true;
    err=protocomm_add_endpoint(pc,"meeting-config",request,NULL);if(err!=ESP_OK)goto fail;handler_added=true;
    atomic_store(&accepting,true);return ESP_OK;
fail:
    (void)meeting_pairing_ble_stop();return err;
}

esp_err_t meeting_pairing_ble_start(char code[13],meeting_pairing_handler handler,meeting_pairing_reset reset,void *ctx)
{
    if(!code||!handler||!reset||pc||sc_ble_running()||sc_wifi_running()||meeting_sta_running())return ESP_ERR_INVALID_STATE;
    code[0]='\0';
    const uint64_t range=1000000000000ULL,limit=UINT64_MAX-(UINT64_MAX%range);
    uint64_t random;
    bootloader_random_enable();
    do {esp_fill_random(&random,sizeof(random));} while(random>=limit);
    bootloader_random_disable();
    const char *test_code=CONFIG_MEETING_SUPERVISED_PAIRING_CODE;
    bool valid_test=strlen(test_code)==12;
    for(size_t i=0;valid_test&&i<12;i++)valid_test=test_code[i]>='0'&&test_code[i]<='9';
    if(valid_test&&meeting_commission_allowed())memcpy(code,test_code,13);
    else (void)snprintf(code,13,"%012" PRIu64,random%range);
    random=0;
    esp_err_t err=start_password(code,12,false,handler,reset,ctx);
    if(err!=ESP_OK)memset(code,0,13);
    return err;
}

esp_err_t meeting_pairing_ble_start_management(const uint8_t secret[32],meeting_pairing_handler handler,meeting_pairing_reset reset,void *ctx)
{
    char password[65];
    if(!meeting_management_password(secret,password))return ESP_ERR_INVALID_ARG;
    esp_err_t err=start_password(password,64,true,handler,reset,ctx);
    volatile char *wipe=password;
    for(size_t i=0;i<sizeof(password);i++)wipe[i]=0;
    return err;
}
