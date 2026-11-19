#pragma once
#include "esp_err.h"
#include <stdint.h>
typedef struct {int x;} wifi_init_config_t;
typedef struct {uint8_t reason;} wifi_event_sta_disconnected_t;
typedef struct {int8_t rssi;uint8_t primary;} wifi_ap_record_t;
typedef struct {struct {uint8_t ssid[32],password[64];} sta;} wifi_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
esp_err_t esp_wifi_init(const wifi_init_config_t*);esp_err_t esp_wifi_deinit(void);esp_err_t esp_wifi_set_storage(int);esp_err_t esp_wifi_set_mode(int);esp_err_t esp_wifi_set_config(int,const wifi_config_t*);esp_err_t esp_wifi_start(void);esp_err_t esp_wifi_stop(void);esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t*);

#define WIFI_REASON_NO_AP_FOUND 201
