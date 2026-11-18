#include "meeting_management_ack.h"
#include <string.h>
uint8_t meeting_management_ack(meeting_pairing_token token,meeting_wifi_switch *txn,
                               const meeting_wifi_slots_io *slots,const uint8_t id[16])
{
    if(!txn||!slots||!id||!txn->recovered||txn->uncertain||!meeting_pairing_ble_session_valid(token))return 23;
    if(!txn->present)return 24;
    if(memcmp(txn->record.request.id,id,16))return 22;
    if(txn->record.state!=MEETING_SWITCH_COMPLETE&&txn->record.state!=MEETING_SWITCH_ROLLED_BACK)return 23;
    meeting_wifi_slot active={0};unsigned index;uint8_t result=23;
    if(meeting_wifi_slots_load(slots,&active,&index)!=MEETING_WIFI_SLOTS_OK)goto done;
    if(txn->record.state==MEETING_SWITCH_COMPLETE){
        if(memcmp(active.request_id,id,16)||memcmp(active.digest,txn->record.request.digest,32))goto done;
    }else{
        if(!memcmp(active.request_id,id,16)||active.generation==UINT64_MAX||!meeting_pairing_ble_session_valid(token)||
           meeting_wifi_slots_cleanup_uncommitted(slots,active.generation+1,id,txn->record.request.digest)!=MEETING_WIFI_SLOTS_OK)goto done;
    }
    if(!meeting_pairing_ble_session_valid(token)||meeting_wifi_switch_ack(txn,id,true)!=MEETING_SWITCH_OK)goto done;
    result=24;
done:
    {volatile unsigned char *p=(volatile unsigned char *)&active;for(size_t i=0;i<sizeof(active);i++)p[i]=0;}
    return result;
}
