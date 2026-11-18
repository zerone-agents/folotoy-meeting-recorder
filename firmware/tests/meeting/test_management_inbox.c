#include "meeting_management_inbox.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool valid=true,deliver=true;
bool meeting_config_ble_take(meeting_config_ble_inbox *inbox,uint8_t *out,size_t cap,size_t *length,meeting_pairing_token *token){
 (void)inbox;assert(cap>=30);if(!deliver)return false;uint8_t m[30]={'M','W','C','1',1,1};m[21]=3;m[22]=4;memcpy(m+23,"netpass",7);memcpy(out,m,30);*length=30;*token=(meeting_pairing_token){1,2};return true;
}
bool meeting_pairing_ble_session_valid(meeting_pairing_token token){(void)token;return valid;}
int main(void){meeting_config_ble_inbox inbox={0};meeting_wifi_command c;meeting_pairing_token token;
 assert(meeting_management_inbox_take(&inbox,&c,&token));assert(!strcmp(c.ssid,"net"));
 valid=false;assert(!meeting_management_inbox_take(&inbox,&c,&token));assert(c.password[0]==0&&token.session==0);
 valid=true;deliver=false;assert(!meeting_management_inbox_take(&inbox,&c,&token));assert(c.password[0]==0);
 puts("management inbox authenticated handoff: PASS");}
