#include "meeting_wifi_slot_apply.h"
#include <string.h>
bool meeting_wifi_slot_apply(meeting_config *c,const meeting_wifi_slot *s){
 if(!c||!s||!s->generation||!s->ssid[0]||!memchr(s->ssid,0,sizeof(s->ssid))||!memchr(s->password,0,sizeof(s->password)))return false;
 /* Reject aliasing: clearing the destination must never erase the source. */
 uintptr_t a=(uintptr_t)c,b=(uintptr_t)s;
 if(a>UINTPTR_MAX-sizeof(*c)||b>UINTPTR_MAX-sizeof(*s)||(a<b+sizeof(*s)&&b<a+sizeof(*c)))return false;
 memset(c->ssid,0,sizeof(c->ssid));memset(c->password,0,sizeof(c->password));
 memcpy(c->ssid,s->ssid,strlen(s->ssid));memcpy(c->password,s->password,strlen(s->password));return true;
}
bool meeting_wifi_slot_restore(const meeting_wifi_slots_io *io,meeting_config *config){
 if(!io||!config)return false;
 meeting_wifi_slot slot={0};unsigned index;
 bool ok=meeting_wifi_slots_load(io,&slot,&index)==MEETING_WIFI_SLOTS_OK&&meeting_wifi_slot_apply(config,&slot);
 volatile unsigned char *p=(volatile unsigned char *)&slot;for(size_t i=0;i<sizeof(slot);i++)p[i]=0;
 return ok;
}
