#include "meeting_wifi_trial_sta.h"
#include "meeting_sta.h"
#include <assert.h>
#include <stdio.h>
static bool running,ip;static int start_calls,stop_calls;static bool stop_fail,start_fail;
esp_err_t meeting_sta_start(const char *s,const char *p){assert(s&&p&&!running);running=true;start_calls++;ip=false;return start_fail?ESP_FAIL:ESP_OK;}
esp_err_t meeting_sta_stop(void){stop_calls++;if(stop_fail)return ESP_FAIL;running=false;ip=false;return ESP_OK;}
bool meeting_sta_has_ip(void){return ip;}
bool meeting_sta_running(void){return running;}
int main(void){meeting_wifi_trial_io io;meeting_wifi_trial t={0};meeting_wifi_trial_sta owner={0};assert(meeting_wifi_trial_sta_bind(&owner,&io));
 assert(meeting_wifi_trial_start(&t,&io,"new","pass",0,100)==MEETING_TRIAL_RUNNING&&start_calls==1);
 ip=true;assert(meeting_wifi_trial_poll(&t,1,false)==MEETING_TRIAL_CONNECTED);
 assert(meeting_wifi_trial_poll(&t,2,true)==MEETING_TRIAL_INTERRUPTED&&!running&&stop_calls==1);
 t=(meeting_wifi_trial){0};running=true;assert(meeting_wifi_trial_start(&t,&io,"new","pass",3,100)==MEETING_TRIAL_STOP_FAILED);
 assert(running&&start_calls==1);running=false;start_fail=true;stop_fail=true;t=(meeting_wifi_trial){0};
 assert(meeting_wifi_trial_start(&t,&io,"new","pass",4,100)==MEETING_TRIAL_STOP_FAILED);
 assert(owner.owned&&running);puts("wifi trial STA adapter: PASS");}
