#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef void *esp_http_client_handle_t;
typedef enum {HTTP_METHOD_POST,HTTP_METHOD_PUT,HTTP_METHOD_GET} esp_http_client_method_t;
typedef int esp_err_t;
typedef enum {HTTP_EVENT_ON_HEADER} esp_http_client_event_id_t;
typedef struct {
 esp_http_client_event_id_t event_id;
 void *user_data;
 char *header_key,*header_value;
} esp_http_client_event_t;
typedef struct {
 const char *url,*cert_pem,*common_name;
 int timeout_ms;
 bool disable_auto_redirect,skip_cert_common_name_check;
 esp_err_t (*event_handler)(esp_http_client_event_t *);
 void *user_data;
} esp_http_client_config_t;
#define ESP_OK 0
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_HTTP_EAGAIN 0x7007
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_set_url(esp_http_client_handle_t,const char*);
int esp_http_client_delete_header(esp_http_client_handle_t,const char*);
int esp_http_client_set_method(esp_http_client_handle_t,esp_http_client_method_t);
int esp_http_client_set_header(esp_http_client_handle_t,const char*,const char*);
int esp_http_client_open(esp_http_client_handle_t,int);
int esp_http_client_write(esp_http_client_handle_t,const char*,int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t,char*,int);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t,int*,int*);
int esp_http_client_get_errno(esp_http_client_handle_t);
