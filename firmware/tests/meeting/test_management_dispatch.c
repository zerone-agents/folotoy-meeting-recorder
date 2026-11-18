#include "meeting_management_dispatch.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool valid=true;static int calls;static uint8_t reply;
bool meeting_pairing_ble_session_valid(meeting_pairing_token t){(void)t;return valid;}
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *i,meeting_pairing_token t,uint8_t code){(void)i;(void)t;if(!valid)return false;reply=code;return true;}
static bool hash(void *ctx,const uint8_t *p,size_t n,uint8_t out[32]){(void)ctx;(void)p;(void)n;memset(out,1,32);return true;}
meeting_wifi_slots_result meeting_wifi_slots_load(const meeting_wifi_slots_io *io,meeting_wifi_slot *out,unsigned *index){(void)io;*out=(meeting_wifi_slot){.generation=1,.ssid="old"};*index=0;return MEETING_WIFI_SLOTS_OK;}
meeting_wifi_operation_result meeting_wifi_operation_prepare(meeting_wifi_operation *op,meeting_wifi_switch *txn,const meeting_wifi_slots_io *io,const meeting_wifi_slot *candidate){
 (void)op;(void)txn;(void)io;assert(candidate->generation==2&&!strcmp(candidate->ssid,"net")&&candidate->digest[0]==1);calls++;return MEETING_OP_READY;
}
int main(void){meeting_config_ble_inbox inbox={0};meeting_pairing_token token={1,2};meeting_wifi_switch txn={.recovered=true};meeting_wifi_slots_io slots={0};meeting_wifi_operation op={0};
 meeting_wifi_command c={.kind=MEETING_WIFI_COMMAND_SWITCH,.operation_id={1},.ssid="net",.password="pass"};
 valid=false;assert(!meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));assert(!calls);
 valid=true;assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));assert(calls==1&&reply==17);
 /* A duplicate operation returns its durable status without re-staging. */
 txn.recovered=true;txn.present=true;txn.record.request.id[0]=1;memset(txn.record.request.digest,1,32);
 txn.record.state=MEETING_SWITCH_COMPLETE;reply=0;
 assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));
 assert(reply==19&&calls==1);
 txn.record.state=MEETING_SWITCH_WAITING;reply=0;
 assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));
 assert(reply==17&&calls==1);
 txn.record.state=MEETING_SWITCH_ROLLED_BACK;reply=0;
 assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));
 assert(reply==20&&calls==1);
 txn.uncertain=true;assert(!meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));
 txn.uncertain=false;
 txn.record.request.digest[0]=2;reply=0;
 assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&c,hash,NULL));
 assert(reply==23&&calls==1);
 puts("authenticated switch dispatch: PASS");}
