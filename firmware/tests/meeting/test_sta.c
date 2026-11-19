#include "meeting_sta.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <assert.h>
#include <string.h>
esp_event_base_t WIFI_EVENT="wifi",IP_EVENT="ip";static esp_netif_t n;static event_handler_t cb;static int fail_connect,started,mode,ram,ap,fail_unregister,fail_wifi_unregister;
bool sc_wifi_running(void){return ap;}
esp_err_t esp_netif_init(void){return ESP_OK;} esp_err_t esp_event_loop_create_default(void){return ESP_OK;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){return &n;} void esp_netif_destroy_default_wifi(esp_netif_t *x){assert(x==&n);}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t i,event_handler_t f,void *x,esp_event_handler_instance_t *h){(void)b;(void)i;(void)x;cb=f;*h=(void*)1;return ESP_OK;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t b,int32_t i,esp_event_handler_instance_t h){(void)i;(void)h;if(fail_unregister){fail_unregister--;return 7;}if(b==WIFI_EVENT&&fail_wifi_unregister){fail_wifi_unregister--;return 7;}return ESP_OK;}
esp_err_t esp_wifi_init(const wifi_init_config_t *x){(void)x;return ESP_OK;} esp_err_t esp_wifi_deinit(void){return ESP_OK;}
esp_err_t esp_wifi_set_storage(int x){ram=x==WIFI_STORAGE_RAM;return ESP_OK;} esp_err_t esp_wifi_set_mode(int x){mode=x;return ESP_OK;}
esp_err_t esp_wifi_set_config(int x,const wifi_config_t *c){(void)x;assert(!memcmp(c->sta.ssid,"wifi",4));return ESP_OK;}
esp_err_t esp_wifi_start(void){started=1;return ESP_OK;} esp_err_t esp_wifi_stop(void){started=0;return ESP_OK;} esp_err_t esp_wifi_connect(void){return fail_connect?7:ESP_OK;}
int main(void){assert(meeting_sta_reconnect()!=ESP_OK);ap=1;assert(meeting_sta_start("wifi","password")!=ESP_OK);ap=0;assert(meeting_sta_start("wifi","password")==ESP_OK);assert(started&&ram&&mode==WIFI_MODE_STA);cb(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(meeting_sta_has_ip());cb(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(!meeting_sta_has_ip());
wifi_event_sta_disconnected_t missing={.reason=WIFI_REASON_NO_AP_FOUND};
cb(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&missing);assert(meeting_sta_ap_missing());
cb(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(!meeting_sta_ap_missing());
cb(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(!meeting_sta_ap_missing());
fail_connect=1;assert(meeting_sta_reconnect()!=ESP_OK);fail_connect=0;assert(meeting_sta_reconnect()==ESP_OK);
fail_unregister=1;assert(meeting_sta_stop()!=ESP_OK&&meeting_sta_running()&&started);cb(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(!meeting_sta_has_ip());
assert(meeting_sta_stop()==ESP_OK&&!meeting_sta_running());cb(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(!meeting_sta_has_ip());
assert(meeting_sta_start("wifi","password")==ESP_OK);fail_wifi_unregister=1;assert(meeting_sta_stop()!=ESP_OK&&meeting_sta_running()&&started);assert(meeting_sta_stop()==ESP_OK&&!meeting_sta_running());
fail_connect=1;assert(meeting_sta_start("wifi","password")!=ESP_OK);assert(!started);return 0;}
