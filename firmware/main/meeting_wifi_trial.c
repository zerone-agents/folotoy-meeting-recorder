#include "meeting_wifi_trial.h"
#include <stddef.h>
meeting_trial_result meeting_wifi_trial_start(meeting_wifi_trial *t,const meeting_wifi_trial_io *io,const char *ssid,const char *password,uint64_t now,uint64_t timeout){
 if(!t||!io||!io->start||!io->stop||!io->has_ip||!ssid||!*ssid||!password||!timeout||t->state!=MEETING_TRIAL_IDLE)return MEETING_TRIAL_START_FAILED;
 t->io=*io;t->since=now;t->timeout=timeout;
 if(!t->io.start(t->io.ctx,ssid,password)){t->state=t->io.stop(t->io.ctx)?MEETING_TRIAL_START_FAILED:MEETING_TRIAL_STOP_FAILED;return t->state;}
 t->state=MEETING_TRIAL_RUNNING;return t->state;
}
meeting_trial_result meeting_wifi_trial_poll(meeting_wifi_trial *t,uint64_t now,bool record){
 if(!t)return MEETING_TRIAL_START_FAILED;
 if(t->state==MEETING_TRIAL_CONNECTED && record){
  t->state=t->io.stop(t->io.ctx)?MEETING_TRIAL_INTERRUPTED:MEETING_TRIAL_STOP_FAILED;return t->state;
 }
 if(t->state!=MEETING_TRIAL_RUNNING)return t->state;
 if(record||now<t->since||now-t->since>=t->timeout){
  if(!t->io.stop(t->io.ctx)){t->state=MEETING_TRIAL_STOP_FAILED;return t->state;}
  t->state=record?MEETING_TRIAL_INTERRUPTED:MEETING_TRIAL_TIMEOUT;return t->state;
 }
 if(t->io.has_ip(t->io.ctx))t->state=MEETING_TRIAL_CONNECTED;
 return t->state;
}
