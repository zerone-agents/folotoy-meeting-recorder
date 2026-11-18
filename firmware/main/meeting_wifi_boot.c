#include "meeting_wifi_boot.h"
#include "meeting_wifi_slot_apply.h"
#include "meeting_wifi_slots_nvs.h"
#include "meeting_wifi_switch_nvs.h"
#include "nvs.h"
#include <string.h>

static bool active_matches(void *ctx,const meeting_switch_request *request)
{
    const meeting_wifi_slot *slot=ctx;
    return slot&&request&&!memcmp(slot->request_id,request->id,16)&&!memcmp(slot->digest,request->digest,32);
}
typedef struct {const meeting_wifi_slots_io *slots;uint64_t generation;} candidate_proof;
static bool candidate_matches(void *ctx,const meeting_switch_request *request)
{
    const candidate_proof *proof=ctx;
    return proof&&request&&meeting_wifi_slots_verify_uncommitted(proof->slots,proof->generation,request->id,request->digest);
}

bool meeting_wifi_boot_restore(meeting_config *config)
{
    if (!config) return false;
    nvs_handle_t handle;
    esp_err_t err=nvs_open("meeting_wifi",NVS_READONLY,&handle);
    if (err==ESP_ERR_NVS_NOT_FOUND) return true;
    if (err!=ESP_OK) return false;
    meeting_wifi_slots_io slots={0};meeting_switch_io store={0};
    meeting_wifi_switch txn;meeting_wifi_pointer pointer;
    meeting_wifi_slot active={0},other={0};unsigned index=0;
    bool ok=false;
    if (!meeting_wifi_slots_nvs_bind(&handle,&slots)||!meeting_wifi_switch_nvs_bind(&handle,&store)) goto done;
    meeting_wifi_switch_init(&txn,&store);
    if (meeting_wifi_switch_recover(&txn)!=MEETING_SWITCH_OK) goto done;
    int pointer_status=slots.read_pointer(slots.ctx,&pointer);
    if (pointer_status==1) {
        /* A partially initialized namespace is not evidence of legacy state. */
        ok=!txn.present&&slots.read_slot(slots.ctx,0,&active)==1&&slots.read_slot(slots.ctx,1,&other)==1;
        goto done;
    }
    if (pointer_status!=0||meeting_wifi_slots_load(&slots,&active,&index)!=MEETING_WIFI_SLOTS_OK) goto done;
    if (txn.present&&(txn.record.state==MEETING_SWITCH_COMMITTING||txn.record.state==MEETING_SWITCH_TESTING)) {
        meeting_switch_state previous=txn.record.state;
        nvs_close(handle);handle=0;
        if(nvs_open("meeting_wifi",NVS_READWRITE,&handle)!=ESP_OK) goto done;
        if(!meeting_wifi_slots_nvs_bind(&handle,&slots)||!meeting_wifi_switch_nvs_bind(&handle,&store))goto done;
        meeting_wifi_switch_init(&txn,&store);
        if(meeting_wifi_switch_recover(&txn)!=MEETING_SWITCH_OK||!txn.present||txn.record.state!=previous||
           meeting_wifi_slots_load(&slots,&active,&index)!=MEETING_WIFI_SLOTS_OK)goto done;
        if(previous==MEETING_SWITCH_TESTING) {
            if(active.generation==UINT64_MAX)goto done;
            candidate_proof proof={&slots,active.generation+1};
            if(!candidate_matches(&proof,&txn.record.request)||
               meeting_wifi_switch_advance(&txn,txn.record.request.id,MEETING_SWITCH_WAITING)!=MEETING_SWITCH_OK)goto done;
        } else if(active_matches(&active,&txn.record.request)) {
            if(meeting_wifi_switch_commit_confirmed(&txn,txn.record.request.id,active_matches,&active)!=MEETING_SWITCH_OK)goto done;
        } else {
            if(active.generation==UINT64_MAX)goto done;
            candidate_proof proof={&slots,active.generation+1};
            if(meeting_wifi_switch_rollback_confirmed(&txn,txn.record.request.id,candidate_matches,&proof)!=MEETING_SWITCH_OK)goto done;
        }
    }
    if(txn.present&&txn.record.state==MEETING_SWITCH_WAITING&&slots.read_slot(slots.ctx,1-index,&other)==1) {
        /* Power loss between writing WAITING and staging the candidate. */
        nvs_close(handle);handle=0;
        if(nvs_open("meeting_wifi",NVS_READWRITE,&handle)!=ESP_OK)goto done;
        if(!meeting_wifi_slots_nvs_bind(&handle,&slots)||!meeting_wifi_switch_nvs_bind(&handle,&store))goto done;
        meeting_wifi_switch_init(&txn,&store);
        if(meeting_wifi_switch_recover(&txn)!=MEETING_SWITCH_OK||!txn.present||txn.record.state!=MEETING_SWITCH_WAITING||
           meeting_wifi_slots_load(&slots,&active,&index)!=MEETING_WIFI_SLOTS_OK||
           slots.read_slot(slots.ctx,1-index,&other)!=1||!memcmp(active.request_id,txn.record.request.id,16)||
           meeting_wifi_switch_advance(&txn,txn.record.request.id,MEETING_SWITCH_ROLLED_BACK)!=MEETING_SWITCH_OK)goto done;
    }
    if (txn.present) {
        if (txn.record.state==MEETING_SWITCH_COMPLETE) {
            if (memcmp(active.request_id,txn.record.request.id,16)||memcmp(active.digest,txn.record.request.digest,32)) goto done;
        } else if (txn.record.state==MEETING_SWITCH_ROLLED_BACK) {
            if (!memcmp(active.request_id,txn.record.request.id,16)) goto done;
        } else if (txn.record.state==MEETING_SWITCH_WAITING) {
            if(active.generation==UINT64_MAX)goto done;
            candidate_proof proof={&slots,active.generation+1};
            if(!candidate_matches(&proof,&txn.record.request))goto done;
        } else goto done;
    }
    ok=meeting_wifi_slot_apply(config,&active);
done:
    {volatile unsigned char *p=(volatile unsigned char *)&active;for(size_t i=0;i<sizeof(active);i++)p[i]=0;
     p=(volatile unsigned char *)&other;for(size_t i=0;i<sizeof(other);i++)p[i]=0;}
    if(handle)nvs_close(handle);
    return ok;
}
