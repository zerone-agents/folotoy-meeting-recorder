#include "meeting_wifi_trial.h"
#include <assert.h>
#include <stdio.h>
typedef struct {int starts,stops;bool ip,stop_ok;} fake;
static bool start(void *v,const char*s,const char*p){fake*f=v;assert(s&&p);f->starts++;return true;}
static bool stop(void *v){fake*f=v;f->stops++;return f->stop_ok;}
static bool has_ip(void *v){return ((fake*)v)->ip;}
int main(void){
 fake f={.stop_ok=true};meeting_wifi_trial_io io={&f,start,stop,has_ip};meeting_wifi_trial trial={0};
 assert(meeting_wifi_trial_start(&trial,&io,"candidate","password",100,30000)==MEETING_TRIAL_RUNNING);
 f.ip=true;assert(meeting_wifi_trial_poll(&trial,101,false)==MEETING_TRIAL_CONNECTED);
 assert(f.stops==0);
 assert(meeting_wifi_trial_poll(&trial,102,true)==MEETING_TRIAL_INTERRUPTED && f.stops==1);
 f.stops=0;
 trial=(meeting_wifi_trial){0};f.ip=false;
 assert(meeting_wifi_trial_start(&trial,&io,"candidate","password",100,30000)==MEETING_TRIAL_RUNNING);
 assert(meeting_wifi_trial_poll(&trial,110,true)==MEETING_TRIAL_INTERRUPTED && f.stops==1);
 trial=(meeting_wifi_trial){0};assert(meeting_wifi_trial_start(&trial,&io,"candidate","password",100,30000)==MEETING_TRIAL_RUNNING);
 assert(meeting_wifi_trial_poll(&trial,30100,false)==MEETING_TRIAL_TIMEOUT && f.stops==2);
 trial=(meeting_wifi_trial){0};assert(meeting_wifi_trial_start(&trial,&io,"candidate","password",100,30000)==MEETING_TRIAL_RUNNING);
 f.stop_ok=false;assert(meeting_wifi_trial_poll(&trial,101,true)==MEETING_TRIAL_STOP_FAILED);
 assert(meeting_wifi_trial_poll(&trial,102,false)==MEETING_TRIAL_STOP_FAILED);
 puts("candidate wifi trial: PASS");
}
