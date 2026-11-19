#include "meeting_sta.h"
#include "sc_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <string.h>

static esp_netif_t *netif;
static esp_event_handler_instance_t wifi_handler, ip_handler;
static bool wifi_initialized, started;
static atomic_bool has_ip, accept_events, ap_missing;
static const char *TAG="meeting_sta";
static void event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (!atomic_load(&accept_events)) return;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&has_ip, false);
        const wifi_event_sta_disconnected_t *disconnected=data;
        ESP_LOGW(TAG,"Wi-Fi disconnected: reason=%u",disconnected?(unsigned)disconnected->reason:0u);
        atomic_store(&ap_missing,disconnected&&disconnected->reason==WIFI_REASON_NO_AP_FOUND);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {atomic_store(&ap_missing,false);atomic_store(&has_ip, true);ESP_LOGI(TAG,"Wi-Fi acquired IP");}
}

esp_err_t meeting_sta_start(const char *ssid, const char *password)
{
    if (!ssid || !password || !*ssid || strlen(ssid)>32 || strlen(password)>63 || started || netif || sc_wifi_running()) return ESP_ERR_INVALID_ARG;
    atomic_store(&has_ip, false);
    atomic_store(&ap_missing,false);
    esp_err_t err=esp_netif_init();
    if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) return err;
    err=esp_event_loop_create_default();
    if (err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) return err;
    netif=esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init); if(err!=ESP_OK) goto fail;
    wifi_initialized=true;
    err=esp_event_handler_instance_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,event,NULL,&wifi_handler); if(err!=ESP_OK) goto fail;
    err=esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL,&ip_handler); if(err!=ESP_OK) goto fail;
    wifi_config_t cfg={0}; memcpy(cfg.sta.ssid,ssid,strlen(ssid));memcpy(cfg.sta.password,password,strlen(password));
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM); if(err!=ESP_OK) goto fail;
    err=esp_wifi_set_mode(WIFI_MODE_STA); if(err!=ESP_OK) goto fail;
    err=esp_wifi_set_config(WIFI_IF_STA,&cfg); memset(&cfg,0,sizeof(cfg)); if(err!=ESP_OK) goto fail;
    atomic_store(&accept_events,true);
    err=esp_wifi_start(); if(err!=ESP_OK) goto fail;
    started=true;
    err=esp_wifi_connect(); if(err!=ESP_OK) goto fail;
    ESP_LOGI(TAG,"Wi-Fi connection requested");
    return ESP_OK;
fail:
    ESP_LOGE(TAG,"Wi-Fi startup failed: %s",esp_err_to_name(err));
    (void)meeting_sta_stop(); return err;
}
esp_err_t meeting_sta_stop(void)
{
    atomic_store(&accept_events,false);
    atomic_store(&ap_missing,false);
    atomic_store(&has_ip,false);
    if(ip_handler){
        esp_err_t e=esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,ip_handler);
        if(e!=ESP_OK)return e;
        ip_handler=NULL;
    }
    if(wifi_handler){
        esp_err_t e=esp_event_handler_instance_unregister(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_handler);
        if(e!=ESP_OK)return e;
        wifi_handler=NULL;
    }
    esp_err_t err=ESP_OK;
    if(started){err=esp_wifi_stop();if(err!=ESP_OK)return err;started=false;}
    if(wifi_initialized){err=esp_wifi_deinit();if(err!=ESP_OK)return err;wifi_initialized=false;}
    if(netif){esp_netif_destroy_default_wifi(netif);netif=NULL;}
    return ESP_OK;
}
bool meeting_sta_has_ip(void){return atomic_load(&has_ip);}
#if CONFIG_MEETING_DEVICE_TIME_PROBE
esp_err_t meeting_sta_probe_dns(const char *address)
{
    if(!netif||!address||!*address)return ESP_ERR_INVALID_ARG;
    esp_netif_dns_info_t info={0};info.ip.type=ESP_IPADDR_TYPE_V4;
    if(esp_netif_str_to_ip4(address,&info.ip.u_addr.ip4)!=ESP_OK)return ESP_ERR_INVALID_ARG;
    esp_err_t err=esp_netif_set_dns_info(netif,ESP_NETIF_DNS_MAIN,&info);
    if(err==ESP_OK)err=esp_netif_set_dns_info(netif,ESP_NETIF_DNS_BACKUP,&info);
    return err;
}
#endif
bool meeting_sta_running(void){return started || wifi_initialized || netif != NULL;}
#if CONFIG_MEETING_SUPERVISED_TEST_NO_WAN
esp_err_t meeting_sta_test_no_gateway(void)
{
    if(!netif||!atomic_load(&has_ip))return ESP_ERR_INVALID_STATE;
    esp_netif_ip_info_t info={0};
    esp_err_t err=esp_netif_get_ip_info(netif,&info);if(err!=ESP_OK)return err;
    err=esp_netif_dhcpc_stop(netif);if(err!=ESP_OK)return err;
    info.gw.addr=0;
    err=esp_netif_set_ip_info(netif,&info);if(err!=ESP_OK)return err;
    esp_netif_ip_info_t check={0};err=esp_netif_get_ip_info(netif,&check);
    return err==ESP_OK&&check.gw.addr==0?ESP_OK:ESP_FAIL;
}
#endif
esp_err_t meeting_sta_reconnect(void)
{
    if(!started||!atomic_load(&accept_events))return ESP_ERR_INVALID_STATE;
    return atomic_load(&has_ip)?ESP_OK:esp_wifi_connect();
}

bool meeting_sta_ap_missing(void){return atomic_load(&accept_events)&&atomic_load(&ap_missing);}
