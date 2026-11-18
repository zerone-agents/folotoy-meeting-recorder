#include "meeting_management_inbox.h"
#include <string.h>
bool meeting_management_inbox_take(meeting_config_ble_inbox *inbox,meeting_wifi_command *command,meeting_pairing_token *token){
 if(!command||!token)return false;
 meeting_wifi_command_clear(command);memset(token,0,sizeof(*token));
 if(!inbox)return false;
 uint8_t raw[23+32+63]={0};size_t len=0;meeting_pairing_token captured={0};
 bool ok=meeting_config_ble_take(inbox,raw,sizeof(raw),&len,&captured)&&
         meeting_wifi_command_parse(raw,len,command)&&meeting_pairing_ble_session_valid(captured);
 volatile uint8_t *wipe=raw;for(size_t i=0;i<sizeof(raw);i++)wipe[i]=0;
 if(!ok){meeting_wifi_command_clear(command);return false;}
 *token=captured;return true;
}
