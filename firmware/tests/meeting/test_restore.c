#include "../../main/meeting_restore.c"
#include <assert.h>
#include <string.h>
static int missing_anchor,missing_floor,bad_anchor,starts,closed,wifi_boot_error,wifi_boot_calls;
bool meeting_wifi_boot_restore(meeting_config *config){assert(config);wifi_boot_calls++;return !wifi_boot_error;}
#if CONFIG_MEETING_AGENT_TIME_PROVISION
static int installs,agent_allowed=1;
bool meeting_commission_allowed(void){return agent_allowed;}
meeting_time_anchor_result meeting_agent_anchor_install(meeting_time_anchor_session *session,const meeting_time_anchor_io *io,
 const uint8_t configured[32],const char *expected,const char *key,bool allowed,meeting_time_key_check check,void *ctx){
 (void)session;(void)io;(void)check;(void)ctx;assert(configured[0]==7&&!strcmp(expected,"origin")&&!strcmp(key,"key"));installs++;
 return allowed?MEETING_ANCHOR_OK:MEETING_ANCHOR_DENIED;
}
#if CONFIG_MEETING_AGENT_TIME_VERIFY_ONLY
bool meeting_agent_anchor_matches(const meeting_time_anchor *anchor,const uint8_t origin[32],const char *expected,const char *key,bool allowed){
 (void)anchor;(void)origin;(void)expected;(void)key;installs++;return allowed;
}
#endif
#endif
static int fake_read(void *ctx,int64_t *floor){(void)ctx;*floor=1800000000;return missing_floor?1:0;}
static bool fake_commit(void *ctx,int64_t floor){(void)ctx;(void)floor;return true;}
static bool fake_set(void *ctx,int64_t floor){(void)ctx;(void)floor;return true;}
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *out){(void)mode;if((!strcmp(name,"meeting_timekey")||!strcmp(name,"meeting_testkey"))&&missing_anchor)return ESP_ERR_NVS_NOT_FOUND;*out=!strcmp(name,"meeting_config")?1:!strcmp(name,"meeting_clock")?3:2;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h);closed++;}
bool meeting_network_enabled(void){return false;}
bool meeting_config_store_nvs_bind(nvs_handle_t *h,meeting_config_store_io *out){(void)h;memset(out,0,sizeof(*out));return true;}
bool meeting_time_anchor_nvs_bind(nvs_handle_t *h,meeting_time_anchor_io *out){(void)h;memset(out,0,sizeof(*out));return true;}
bool meeting_clock_nvs_bind(nvs_handle_t *h,meeting_clock_io *out){*out=(meeting_clock_io){h,fake_read,fake_commit,fake_set};return true;}
bool meeting_ca_validate_pem(const char *pem,uint8_t hash[32],void *ctx){(void)pem;(void)hash;(void)ctx;return true;}
bool meeting_time_public_key_valid(void *ctx,const meeting_time_public_key *key){(void)ctx;(void)key;return true;}
meeting_config_store_status meeting_config_store_load(const meeting_config_store_io *io,meeting_ca_validate validate,void *ctx,uint8_t *scratch,meeting_config *out){(void)io;(void)validate;(void)ctx;(void)scratch;strcpy(out->origin,"https://example.com:443");return MEETING_CONFIG_STORED;}
void meeting_config_clear(meeting_config *c){memset(c,0,sizeof(*c));}
int mbedtls_sha256(const unsigned char *data,size_t n,unsigned char out[32],int is224){(void)data;(void)n;(void)is224;memset(out,7,32);return 0;}
meeting_time_anchor_result meeting_time_anchor_load(const meeting_time_anchor_session *s,const meeting_time_anchor_io *io,const uint8_t hash[32],meeting_time_key_check check,void *ctx,meeting_time_anchor *out){(void)s;(void)io;(void)check;(void)ctx;assert(hash[0]==7);memset(out,0,sizeof(*out));return bad_anchor?MEETING_ANCHOR_CONFLICT:MEETING_ANCHOR_OK;}
esp_err_t meeting_network_activate_restored(const meeting_config *c,const meeting_time_anchor *a,const char *http,const meeting_clock_io *clock){assert(c&&a&&clock&&clock->ctx==&clock_handle&&!strcmp(http,"http://example.com"));starts++;return ESP_OK;}
int main(void){missing_anchor=1;assert(meeting_restore_start()!=ESP_OK&&starts==0&&!clock_open);missing_anchor=0;missing_floor=1;assert(meeting_restore_start()!=ESP_OK&&starts==0&&!clock_open);missing_floor=0;
#if CONFIG_MEETING_AGENT_TIME_PROVISION
#if CONFIG_MEETING_AGENT_TIME_VERIFY_ONLY
 agent_allowed=0;assert(meeting_restore_start()!=ESP_OK&&installs==0&&starts==0&&!clock_open);
 agent_allowed=1;assert(meeting_restore_start()!=ESP_OK&&installs==1&&starts==0&&!clock_open);
#else
 agent_allowed=0;assert(meeting_restore_start()!=ESP_OK&&installs==1&&starts==0&&!clock_open);
 agent_allowed=1;assert(meeting_restore_start()!=ESP_OK&&installs==2&&starts==0&&!clock_open);
#endif
#else
 bad_anchor=1;assert(meeting_restore_start()!=ESP_OK&&starts==0&&!clock_open);bad_anchor=0;
 wifi_boot_error=1;assert(meeting_restore_start()!=ESP_OK&&starts==0&&!clock_open&&wifi_boot_calls>0);
 wifi_boot_error=0;assert(meeting_restore_start()==ESP_OK&&starts==1&&clock_open);assert(meeting_restore_start()==ESP_ERR_INVALID_STATE);
#endif
 assert(closed>=7);return 0;}
