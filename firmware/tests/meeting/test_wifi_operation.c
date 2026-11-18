#include "meeting_wifi_operation.h"
#include "meeting_management_dispatch.h"
#include "meeting_management_ack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if CONFIG_MEETING_SUPERVISED_TEST_LONG_WIFI_TRIAL
static bool supervised_allowed=true;
bool meeting_commission_allowed(void){return supervised_allowed;}
#endif
typedef struct {meeting_wifi_slot slot[2];int present[2];meeting_wifi_pointer ptr;meeting_switch_record rec;bool have_rec,ip,fail_ptr,applied_error,fail_stop,fail_start,fail_save;int starts,stops;} fake;
static int rs(void*v,unsigned i,meeting_wifi_slot*s){fake*f=v;if(!f->present[i])return 1;*s=f->slot[i];return 0;}
static bool ws(void*v,unsigned i,const meeting_wifi_slot*s){fake*f=v;f->slot[i]=*s;f->present[i]=1;return true;}
static int rp(void*v,meeting_wifi_pointer*p){*p=((fake*)v)->ptr;return 0;}
static bool wp(void*v,const meeting_wifi_pointer*p){fake*f=v;if(f->fail_ptr)return false;f->ptr=*p;return !f->applied_error;}
static bool es(void*v,unsigned i){((fake*)v)->present[i]=0;return true;}
static int lr(void*v,meeting_switch_record*r){fake*f=v;if(!f->have_rec)return 1;*r=f->rec;return 0;}
static bool sr(void*v,const meeting_switch_record*r){fake*f=v;if(f->fail_save)return false;f->rec=*r;f->have_rec=true;return true;}
static bool er(void*v){((fake*)v)->have_rec=false;return true;}
static bool st(void*v,const char*s,const char*p){(void)s;(void)p;((fake*)v)->starts++;return !((fake*)v)->fail_start;}
static bool sp(void*v){fake *f=v;f->stops++;return !f->fail_stop;}
static bool ip(void*v){return ((fake*)v)->ip;}
static bool hash(void *v,const uint8_t *p,size_t n,uint8_t out[32]){(void)v;(void)p;(void)n;memset(out,0,32);out[0]=2;return true;}
static uint8_t reply;
bool meeting_pairing_ble_session_valid(meeting_pairing_token token){return token.generation==1&&token.session==2;}
bool meeting_config_ble_publish_result(meeting_config_ble_inbox *inbox,meeting_pairing_token token,uint8_t code){(void)inbox;if(!meeting_pairing_ble_session_valid(token))return false;reply=code;return true;}
int main(void){
 fake f={.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1}};
 meeting_wifi_slots_io slots={&f,rs,ws,rp,wp,es};meeting_switch_io store={.ctx=&f,.save=sr,.load=lr,.erase=er};meeting_wifi_trial_io radio={&f,st,sp,ip};
 meeting_wifi_switch txn;meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);
 meeting_wifi_slot next={.generation=2,.ssid="new",.password="newpass",.request_id={1},.digest={2}};
 meeting_wifi_operation op={0};meeting_config_ble_inbox inbox={0};meeting_pairing_token token={1,2};
 meeting_wifi_command command={.kind=MEETING_WIFI_COMMAND_SWITCH,.operation_id={1},.ssid="new",.password="newpass"};
 assert(meeting_management_dispatch_switch(&inbox,token,&txn,&slots,&op,&command,hash,NULL));
 assert(op.state==MEETING_OP_READY&&reply==17);
 assert(f.starts==0);assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RUNNING);
#if CONFIG_MEETING_SUPERVISED_TEST_LONG_WIFI_TRIAL
 assert(op.trial.timeout==180000);supervised_allowed=false;
#else
 assert(op.trial.timeout==30000);
#endif
 f.ip=true;f.fail_ptr=true;assert(meeting_wifi_operation_poll(&op,11,false)==MEETING_OP_STORAGE_ERROR);
 meeting_switch_state state;assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_COMMITTING);
 assert(f.ptr.slot==0);
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);
 assert(meeting_wifi_operation_recover_commit(&txn,&slots,2)==MEETING_OP_ROLLED_BACK);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_ROLLED_BACK);
 assert(meeting_wifi_operation_cleanup_rollback(&txn,&slots,2)==MEETING_OP_ROLLED_BACK);
 assert(!f.present[1]&&f.present[0]&&f.ptr.slot==0);
 assert(meeting_wifi_operation_cleanup_rollback(&txn,&slots,2)==MEETING_OP_ROLLED_BACK);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1}};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RUNNING);
 assert(op.trial.timeout==30000);
 assert(meeting_wifi_operation_poll(&op,11,true)==MEETING_OP_WAITING);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_WAITING&&f.ptr.slot==0);
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_restore_waiting(&op,&txn,&slots,3)==MEETING_OP_STORAGE_ERROR);
 assert(meeting_wifi_operation_restore_waiting(&op,&txn,&slots,2)==MEETING_OP_READY);assert(meeting_wifi_operation_start_trial(&op,&radio,20)==MEETING_OP_RUNNING);
 f.ip=true;assert(meeting_wifi_operation_poll(&op,21,false)==MEETING_OP_COMPLETE);
 assert(f.stops==2); /* interrupted trial, then successful trial released */
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_COMPLETE&&f.ptr.slot==1);
 uint8_t wrong[16]={9};assert(meeting_management_ack(token,&txn,&slots,wrong)==22);
 assert(meeting_management_ack(token,&txn,&slots,next.request_id)==24&&f.have_rec&&f.rec.acknowledged);
 assert(meeting_management_ack(token,&txn,&slots,next.request_id)==24);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1},.fail_stop=true};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RUNNING);
 f.ip=true;assert(meeting_wifi_operation_poll(&op,11,false)==MEETING_OP_RADIO_ERROR);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_COMPLETE&&f.ptr.slot==1);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1},.applied_error=true};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RUNNING);
 f.ip=true;assert(meeting_wifi_operation_poll(&op,11,false)==MEETING_OP_STORAGE_ERROR&&f.ptr.slot==1);
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);
 assert(meeting_wifi_operation_recover_commit(&txn,&slots,2)==MEETING_OP_COMPLETE);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_COMPLETE);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1}};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RUNNING);
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);
 assert(meeting_wifi_operation_recover_testing(&txn,&slots,2)==MEETING_OP_ROLLED_BACK);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_ROLLED_BACK);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1},.fail_start=true};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);
 assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_ROLLED_BACK);
 assert(f.stops==1&&f.ptr.slot==0);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_ROLLED_BACK);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1},.fail_start=true,.fail_stop=true};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);
 assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_RADIO_ERROR);
 assert(meeting_wifi_switch_query(&txn,next.request_id,&state)==MEETING_SWITCH_OK&&state==MEETING_SWITCH_TESTING);
 f=(fake){.slot={{.generation=1,.ssid="old",.password="oldpass"}},.present={1,0},.ptr={0,1}};
 meeting_wifi_switch_init(&txn,&store);assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);op=(meeting_wifi_operation){0};
 assert(meeting_wifi_operation_prepare(&op,&txn,&slots,&next)==MEETING_OP_READY);
 f.fail_save=true;
 assert(meeting_wifi_operation_start_trial(&op,&radio,10)==MEETING_OP_STORAGE_ERROR);
 assert(f.starts==0&&f.ptr.slot==0);
 puts("wifi operation commit failure: PASS");
}
