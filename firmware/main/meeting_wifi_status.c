#include "meeting_wifi_status.h"
uint8_t meeting_wifi_status_code(meeting_switch_result result,meeting_switch_state state){
 if(result==MEETING_SWITCH_UNKNOWN)return 22;
 if(result!=MEETING_SWITCH_OK)return 23;
 switch(state){
 case MEETING_SWITCH_WAITING:return 17;
 case MEETING_SWITCH_TESTING:return 18;
 case MEETING_SWITCH_COMPLETE:return 19;
 case MEETING_SWITCH_ROLLED_BACK:return 20;
 case MEETING_SWITCH_COMMITTING:return 21;
 }
 return 23;
}
