#include "meeting_wifi_command.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool hash(void *ctx,const uint8_t *data,size_t n,uint8_t out[32]){(void)ctx;assert(n==32&&!memcmp(data,"MWIFI1\0",7)&&data[7]==1&&data[23]==3&&data[24]==4&&!memcmp(data+25,"net",3));memset(out,0,32);for(size_t i=0;i<n;i++)out[i%32]^=data[i];return true;}
int main(void){
 uint8_t msg[64]={'M','W','C','1',1,1};msg[21]=3;msg[22]=4;memcpy(msg+23,"netpass",7);
 meeting_wifi_command out;assert(meeting_wifi_command_parse(msg,30,&out));assert(out.kind==MEETING_WIFI_COMMAND_SWITCH&&!strcmp(out.ssid,"net")&&!strcmp(out.password,"pass"));
 uint8_t a[32],b[32];assert(meeting_wifi_command_digest(&out,hash,NULL,a));out.password[0]='P';assert(meeting_wifi_command_digest(&out,hash,NULL,b));assert(memcmp(a,b,32));
 meeting_wifi_slot slot;assert(meeting_wifi_command_to_slot(&out,2,hash,NULL,&slot));assert(slot.generation==2&&!memcmp(slot.digest,b,32)&&!strcmp(slot.password,"Pass"));
 assert(!meeting_wifi_command_parse(msg,31,&out));assert(out.password[0]==0);
 msg[24]=0;assert(!meeting_wifi_command_parse(msg,30,&out));msg[24]='e';
 msg[4]=2;assert(meeting_wifi_command_parse(msg,21,&out)&&out.kind==MEETING_WIFI_COMMAND_QUERY);
 msg[4]=3;assert(meeting_wifi_command_parse(msg,21,&out)&&out.kind==MEETING_WIFI_COMMAND_ACK);
 assert(!meeting_wifi_command_parse(msg,22,&out));msg[5]=0;assert(!meeting_wifi_command_parse(msg,21,&out));
 puts("wifi management command parser: PASS");
}
