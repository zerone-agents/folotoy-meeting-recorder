#include "meeting_wifi_command.h"
#include <string.h>
void meeting_wifi_command_clear(meeting_wifi_command *c){if(c){volatile uint8_t *p=(volatile uint8_t*)c;for(size_t i=0;i<sizeof(*c);i++)p[i]=0;}}
bool meeting_wifi_command_parse(const uint8_t *in,size_t n,meeting_wifi_command *out){
 if(!out)return false;
 if(in){uintptr_t a=(uintptr_t)in,b=(uintptr_t)out;if(a>UINTPTR_MAX-n||b>UINTPTR_MAX-sizeof(*out)||(a<b+sizeof(*out)&&b<a+n))return false;
 }
 meeting_wifi_command_clear(out);
 if(!in||n<21||memcmp(in,"MWC1",4))return false;
 unsigned any=0;for(unsigned i=5;i<21;i++)any|=in[i];if(!any)return false;
 if(in[4]==MEETING_WIFI_COMMAND_QUERY||in[4]==MEETING_WIFI_COMMAND_ACK){if(n!=21)return false;
 out->kind=(meeting_wifi_command_kind)in[4];memcpy(out->operation_id,in+5,16);return true;
 }
 if(in[4]!=MEETING_WIFI_COMMAND_SWITCH||n<23)return false;
 size_t ssid=in[21],pass=in[22];
 if(!ssid||ssid>32||pass>63||n!=23+ssid+pass||memchr(in+23,0,ssid+pass))return false;
 out->kind=MEETING_WIFI_COMMAND_SWITCH;memcpy(out->operation_id,in+5,16);
 memcpy(out->ssid,in+23,ssid);memcpy(out->password,in+23+ssid,pass);return true;
}
bool meeting_wifi_command_digest(const meeting_wifi_command *c,bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *ctx,uint8_t out[32]){
 if(!out)return false;
 memset(out,0,32);
 if(!c||!sha256||c->kind!=MEETING_WIFI_COMMAND_SWITCH)return false;
 const char *ssid_end=memchr(c->ssid,0,sizeof(c->ssid)),*pass_end=memchr(c->password,0,sizeof(c->password));
 if(!ssid_end||!pass_end||ssid_end==c->ssid)return false;
 unsigned any=0;for(unsigned i=0;i<16;i++)any|=c->operation_id[i];if(!any)return false;
 size_t ssid=(size_t)(ssid_end-c->ssid),pass=(size_t)(pass_end-c->password),n=0;
 uint8_t data[7+16+2+32+63];memcpy(data,"MWIFI1\0",7);n=7;memcpy(data+n,c->operation_id,16);n+=16;
 data[n++]=(uint8_t)ssid;data[n++]=(uint8_t)pass;memcpy(data+n,c->ssid,ssid);n+=ssid;memcpy(data+n,c->password,pass);n+=pass;
 bool ok=sha256(ctx,data,n,out);volatile uint8_t *p=data;for(size_t i=0;i<sizeof(data);i++)p[i]=0;
 if(!ok)memset(out,0,32);
 return ok;
}
bool meeting_wifi_command_to_slot(const meeting_wifi_command *c,uint64_t generation,bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *ctx,meeting_wifi_slot *out){
 if(!out)return false;
 meeting_wifi_slot slot={0};
 if(!generation||!meeting_wifi_command_digest(c,sha256,ctx,slot.digest))return false;
 slot.generation=generation;memcpy(slot.request_id,c->operation_id,16);
 memcpy(slot.ssid,c->ssid,strlen(c->ssid));memcpy(slot.password,c->password,strlen(c->password));
 *out=slot;volatile uint8_t *p=(volatile uint8_t*)&slot;for(size_t i=0;i<sizeof(slot);i++)p[i]=0;return true;
}
