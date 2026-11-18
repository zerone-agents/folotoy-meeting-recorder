#include "meeting_wifi_operation.h"
#include "meeting_wifi_slot_proof.h"
#include "sdkconfig.h"
#include <string.h>
#if CONFIG_MEETING_SUPERVISED_TEST_LONG_WIFI_TRIAL || CONFIG_MEETING_SUPERVISED_TEST_COMMIT_HOLD
#include "meeting_commission.h"
#include "esp_log.h"
#endif
#if CONFIG_MEETING_SUPERVISED_TEST_COMMIT_HOLD
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
static void test_commit_hold(unsigned stage){
 if(CONFIG_MEETING_SUPERVISED_TEST_COMMIT_HOLD==stage&&meeting_commission_allowed()){
  for(unsigned i=0;i<180;i++){
   if(i%5==0){ESP_LOGW("meeting_wifi_test","TEST holding commit checkpoint %u second %u",stage,i);fflush(stdout);}
   vTaskDelay(pdMS_TO_TICKS(1000));
  }
 }
}
#else
static void test_commit_hold(unsigned stage){(void)stage;}
#endif
meeting_wifi_operation_result meeting_wifi_operation_prepare(meeting_wifi_operation *o,meeting_wifi_switch *t,const meeting_wifi_slots_io *slots,const meeting_wifi_slot *candidate){
 if(!o||!t||!slots||!candidate||o->state!=MEETING_OP_IDLE)return MEETING_OP_STORAGE_ERROR;
 o->txn=t;o->slots=slots;o->generation=candidate->generation;
 memcpy(o->request.id,candidate->request_id,16);memcpy(o->request.digest,candidate->digest,32);
 if(meeting_wifi_switch_begin(t,&o->request)!=MEETING_SWITCH_OK||t->record.state!=MEETING_SWITCH_WAITING||
    meeting_wifi_slots_stage(slots,candidate)!=MEETING_WIFI_SLOTS_OK){o->state=MEETING_OP_STORAGE_ERROR;return o->state;}
 o->state=MEETING_OP_READY;return o->state;
}
meeting_wifi_operation_result meeting_wifi_operation_restore_waiting(meeting_wifi_operation *o,meeting_wifi_switch *t,const meeting_wifi_slots_io *slots,uint64_t generation){
 if(!o||!t||!slots||o->state!=MEETING_OP_IDLE||!t->recovered||t->uncertain||!t->present||t->record.state!=MEETING_SWITCH_WAITING||
    !meeting_wifi_slots_verify_uncommitted(slots,generation,t->record.request.id,t->record.request.digest))return MEETING_OP_STORAGE_ERROR;
 o->txn=t;o->slots=slots;o->generation=generation;o->request=t->record.request;o->state=MEETING_OP_READY;return o->state;
}
meeting_wifi_operation_result meeting_wifi_operation_start_trial(meeting_wifi_operation *o,const meeting_wifi_trial_io *radio,uint64_t now){
 if(!o||!radio||o->state!=MEETING_OP_READY||!o->slots||!o->slots->read_slot)return MEETING_OP_STORAGE_ERROR;
 meeting_wifi_slot active={0},candidate={0};unsigned index;meeting_wifi_operation_result result=MEETING_OP_STORAGE_ERROR;
 if(meeting_wifi_slots_load(o->slots,&active,&index)!=MEETING_WIFI_SLOTS_OK||
    !meeting_wifi_slots_verify_uncommitted(o->slots,o->generation,o->request.id,o->request.digest)||
    o->slots->read_slot(o->slots->ctx,1-index,&candidate)!=0||
    meeting_wifi_switch_advance(o->txn,o->request.id,MEETING_SWITCH_TESTING)!=MEETING_SWITCH_OK)goto done;
 uint64_t timeout_ms=30000;
#if CONFIG_MEETING_SUPERVISED_TEST_LONG_WIFI_TRIAL
 if(meeting_commission_allowed()){timeout_ms=180000;ESP_LOGW("meeting_wifi_test","TEST extended candidate trial to 180 seconds");}
#endif
 meeting_trial_result trial=meeting_wifi_trial_start(&o->trial,radio,candidate.ssid,candidate.password,now,timeout_ms);
 if(trial==MEETING_TRIAL_RUNNING)result=MEETING_OP_RUNNING;
 else if(trial==MEETING_TRIAL_START_FAILED)
  result=meeting_wifi_switch_advance(o->txn,o->request.id,MEETING_SWITCH_ROLLED_BACK)==MEETING_SWITCH_OK?MEETING_OP_ROLLED_BACK:MEETING_OP_STORAGE_ERROR;
 else result=MEETING_OP_RADIO_ERROR;
done:
 {volatile unsigned char *p=(volatile unsigned char *)&active;for(size_t i=0;i<sizeof(active);i++)p[i]=0;
  p=(volatile unsigned char *)&candidate;for(size_t i=0;i<sizeof(candidate);i++)p[i]=0;}
 o->state=result;return result;
}
meeting_wifi_operation_result meeting_wifi_operation_poll(meeting_wifi_operation *o,uint64_t now,bool record){
 if(!o)return MEETING_OP_STORAGE_ERROR;
 if(o->state!=MEETING_OP_RUNNING)return o->state;
 meeting_trial_result result=meeting_wifi_trial_poll(&o->trial,now,record);
 if(result==MEETING_TRIAL_RUNNING)return o->state;
 if(result==MEETING_TRIAL_INTERRUPTED||result==MEETING_TRIAL_TIMEOUT){
  meeting_switch_state next=result==MEETING_TRIAL_INTERRUPTED?MEETING_SWITCH_WAITING:MEETING_SWITCH_ROLLED_BACK;
  o->state=meeting_wifi_switch_advance(o->txn,o->request.id,next)==MEETING_SWITCH_OK?
      (result==MEETING_TRIAL_INTERRUPTED?MEETING_OP_WAITING:MEETING_OP_ROLLED_BACK):MEETING_OP_STORAGE_ERROR;return o->state;
 }
 if(result!=MEETING_TRIAL_CONNECTED){o->state=MEETING_OP_RADIO_ERROR;return o->state;}
 if(meeting_wifi_switch_advance(o->txn,o->request.id,MEETING_SWITCH_COMMITTING)!=MEETING_SWITCH_OK){
  o->state=meeting_wifi_trial_poll(&o->trial,now,true)==MEETING_TRIAL_INTERRUPTED?MEETING_OP_STORAGE_ERROR:MEETING_OP_RADIO_ERROR;return o->state;
 }
 test_commit_hold(1);
 if(meeting_wifi_slots_commit(o->slots,o->generation,o->request.id,o->request.digest)!=MEETING_WIFI_SLOTS_OK){
  o->state=meeting_wifi_trial_poll(&o->trial,now,true)==MEETING_TRIAL_INTERRUPTED?MEETING_OP_STORAGE_ERROR:MEETING_OP_RADIO_ERROR;return o->state;
 }
 test_commit_hold(2);
 meeting_wifi_slot_proof proof={o->slots,o->generation};
 if(meeting_wifi_switch_commit_confirmed(o->txn,o->request.id,meeting_wifi_slot_commit_proof,&proof)!=MEETING_SWITCH_OK){
  o->state=meeting_wifi_trial_poll(&o->trial,now,true)==MEETING_TRIAL_INTERRUPTED?MEETING_OP_STORAGE_ERROR:MEETING_OP_RADIO_ERROR;return o->state;
 }
 /* The trial owns STA even after DHCP succeeds. Release it before the
  * supervisor can apply the committed slot or start capture. The durable
  * result remains COMPLETE if radio teardown fails. */
 o->state=meeting_wifi_trial_poll(&o->trial,now,true)==MEETING_TRIAL_INTERRUPTED?
     MEETING_OP_COMPLETE:MEETING_OP_RADIO_ERROR;return o->state;
}
meeting_wifi_operation_result meeting_wifi_operation_recover_commit(meeting_wifi_switch *txn,const meeting_wifi_slots_io *slots,uint64_t generation){
 if(!txn||!slots||!txn->recovered||txn->uncertain||!txn->present||txn->record.state!=MEETING_SWITCH_COMMITTING)
  return MEETING_OP_STORAGE_ERROR;
 meeting_wifi_slot_proof proof={slots,generation};
 if(meeting_wifi_switch_commit_confirmed(txn,txn->record.request.id,meeting_wifi_slot_commit_proof,&proof)==MEETING_SWITCH_OK)
  return MEETING_OP_COMPLETE;
 if(txn->uncertain)return MEETING_OP_STORAGE_ERROR;
 return meeting_wifi_switch_rollback_confirmed(txn,txn->record.request.id,meeting_wifi_slot_rollback_proof,&proof)==MEETING_SWITCH_OK?
        MEETING_OP_ROLLED_BACK:MEETING_OP_STORAGE_ERROR;
}
meeting_wifi_operation_result meeting_wifi_operation_recover_testing(meeting_wifi_switch *txn,const meeting_wifi_slots_io *slots,uint64_t generation){
 if(!txn||!slots||!txn->recovered||txn->uncertain||!txn->present||txn->record.state!=MEETING_SWITCH_TESTING||
    !meeting_wifi_slots_verify_uncommitted(slots,generation,txn->record.request.id,txn->record.request.digest))return MEETING_OP_STORAGE_ERROR;
 return meeting_wifi_switch_advance(txn,txn->record.request.id,MEETING_SWITCH_ROLLED_BACK)==MEETING_SWITCH_OK?
        MEETING_OP_ROLLED_BACK:MEETING_OP_STORAGE_ERROR;
}
meeting_wifi_operation_result meeting_wifi_operation_cleanup_rollback(const meeting_wifi_switch *txn,const meeting_wifi_slots_io *slots,uint64_t generation){
 if(!txn||!slots||!txn->recovered||txn->uncertain||!txn->present||txn->record.state!=MEETING_SWITCH_ROLLED_BACK)return MEETING_OP_STORAGE_ERROR;
 return meeting_wifi_slots_cleanup_uncommitted(slots,generation,txn->record.request.id,txn->record.request.digest)==MEETING_WIFI_SLOTS_OK?
        MEETING_OP_ROLLED_BACK:MEETING_OP_STORAGE_ERROR;
}
