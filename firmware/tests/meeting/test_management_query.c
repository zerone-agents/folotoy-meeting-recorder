#include "meeting_management_query.h"
#include <assert.h>
#include <stdio.h>
static bool valid=true;static uint8_t published;
bool meeting_pairing_ble_session_valid(meeting_pairing_token token){(void)token;return valid;}
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *i,meeting_pairing_token t,uint8_t code){(void)i;(void)t;published=code;return valid;}
int main(void){meeting_wifi_switch txn={.recovered=true,.present=true,.record={.request={.id={1},.digest={2}},.state=MEETING_SWITCH_COMPLETE}};
 meeting_config_ble_inbox inbox={0};meeting_wifi_command cmd={.kind=MEETING_WIFI_COMMAND_QUERY,.operation_id={1}};meeting_pairing_token t={1,2};
 assert(meeting_management_query_publish(&inbox,t,&txn,&cmd)&&published==19);
 cmd.operation_id[0]=3;assert(meeting_management_query_publish(&inbox,t,&txn,&cmd)&&published==22);
 valid=false;published=0;assert(!meeting_management_query_publish(&inbox,t,&txn,&cmd)&&published==0);
 puts("authenticated management query: PASS");}
