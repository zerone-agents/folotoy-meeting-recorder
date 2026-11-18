#include "meeting_management_dispatch.h"
#include "meeting_wifi_status.h"
#include <stdint.h>
#include <string.h>
bool meeting_management_dispatch_switch(meeting_config_ble_inbox *inbox,meeting_pairing_token token,meeting_wifi_switch *txn,const meeting_wifi_slots_io *slots,meeting_wifi_operation *op,const meeting_wifi_command *command,bool (*sha256)(void *,const uint8_t *,size_t,uint8_t[32]),void *hash_ctx){
 if(!inbox||!txn||!slots||!op||!command||!sha256||command->kind!=MEETING_WIFI_COMMAND_SWITCH||!meeting_pairing_ble_session_valid(token))return false;
 meeting_wifi_slot active={0},candidate={0};uint8_t digest[32]={0};unsigned index;bool ok=false;
 if(!txn->recovered||txn->uncertain)goto done;
 if(txn->present&&!memcmp(txn->record.request.id,command->operation_id,16)){
  if(!meeting_wifi_command_digest(command,sha256,hash_ctx,digest))goto done;
  uint8_t code=memcmp(txn->record.request.digest,digest,sizeof digest)?23:
      meeting_wifi_status_code(MEETING_SWITCH_OK,txn->record.state);
  ok=meeting_config_ble_publish_result(inbox,token,code);goto done;
 }
 if(meeting_wifi_slots_load(slots,&active,&index)!=MEETING_WIFI_SLOTS_OK||active.generation==UINT64_MAX)goto done;
 if(!meeting_wifi_command_to_slot(command,active.generation+1,sha256,hash_ctx,&candidate))goto done;
 if(!meeting_pairing_ble_session_valid(token))goto done;
 if(meeting_wifi_operation_prepare(op,txn,slots,&candidate)!=MEETING_OP_READY)goto done;
 ok=meeting_config_ble_publish_result(inbox,token,17);
done:
 {volatile uint8_t *p=(volatile uint8_t *)&active;for(size_t i=0;i<sizeof(active);i++)p[i]=0;
  p=(volatile uint8_t *)&candidate;for(size_t i=0;i<sizeof(candidate);i++)p[i]=0;
  p=digest;for(size_t i=0;i<sizeof digest;i++)p[i]=0;}
 return ok;
}
