#include "meeting_management_query.h"
#include "meeting_wifi_status.h"
bool meeting_management_query_publish(meeting_config_ble_inbox *inbox,meeting_pairing_token token,const meeting_wifi_switch *txn,const meeting_wifi_command *command){
 if(!inbox||!txn||!command||command->kind!=MEETING_WIFI_COMMAND_QUERY||!meeting_pairing_ble_session_valid(token))return false;
 meeting_switch_state state=0;meeting_switch_result result=meeting_wifi_switch_query(txn,command->operation_id,&state);
 return meeting_config_ble_publish_result(inbox,token,meeting_wifi_status_code(result,state));
}
