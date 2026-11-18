#include "meeting_wifi_trial_sta.h"
#include "meeting_sta.h"
static bool start(void *ctx,const char *ssid,const char *password){
 meeting_wifi_trial_sta *owner=ctx;if(!owner||owner->owned||meeting_sta_running())return false;
 owner->owned=true;return meeting_sta_start(ssid,password)==ESP_OK;
}
static bool stop(void *ctx){
 meeting_wifi_trial_sta *owner=ctx;if(!owner)return false;if(!owner->owned)return !meeting_sta_running();
 if(meeting_sta_stop()!=ESP_OK)return false;
 owner->owned=false;return true;
}
static bool ip(void *ctx){meeting_wifi_trial_sta *owner=ctx;return owner&&owner->owned&&meeting_sta_has_ip();}
bool meeting_wifi_trial_sta_bind(meeting_wifi_trial_sta *owner,meeting_wifi_trial_io *out){
 if(!owner||!out||owner->owned)return false;
 *out=(meeting_wifi_trial_io){owner,start,stop,ip};return true;
}
