#include "meeting_wifi_status.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,MEETING_SWITCH_WAITING)==17);
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,MEETING_SWITCH_TESTING)==18);
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,MEETING_SWITCH_COMPLETE)==19);
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,MEETING_SWITCH_ROLLED_BACK)==20);
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,MEETING_SWITCH_COMMITTING)==21);
 assert(meeting_wifi_status_code(MEETING_SWITCH_UNKNOWN,0)==22);
 assert(meeting_wifi_status_code(MEETING_SWITCH_STORAGE_ERROR,0)==23);
 assert(meeting_wifi_status_code(MEETING_SWITCH_OK,99)==23);
 puts("wifi status mapping: PASS");
}
