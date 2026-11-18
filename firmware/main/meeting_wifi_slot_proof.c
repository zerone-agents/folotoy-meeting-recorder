#include "meeting_wifi_slot_proof.h"
bool meeting_wifi_slot_commit_proof(void *ctx,const meeting_switch_request *request){
 if(!ctx||!request)return false;
 meeting_wifi_slot_proof *proof=ctx;
 return meeting_wifi_slots_verify_active(proof->io,proof->generation,request->id,request->digest);
}
bool meeting_wifi_slot_rollback_proof(void *ctx,const meeting_switch_request *request){
 if(!ctx||!request)return false;
 meeting_wifi_slot_proof *proof=ctx;
 return meeting_wifi_slots_verify_uncommitted(proof->io,proof->generation,request->id,request->digest);
}
