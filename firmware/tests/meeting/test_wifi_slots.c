#include "meeting_wifi_slots.h"
#include "meeting_wifi_slot_proof.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef struct { meeting_wifi_slot slots[2]; int present[2]; meeting_wifi_pointer pointer; int pointer_present; int fail_pointer; } fake;
static int read_slot(void *v,unsigned n,meeting_wifi_slot *s){fake*f=v;if(!f->present[n])return 1;*s=f->slots[n];return 0;}
static bool write_slot(void *v,unsigned n,const meeting_wifi_slot *s){fake*f=v;f->slots[n]=*s;f->present[n]=1;return true;}
static int read_pointer(void *v,meeting_wifi_pointer *p){fake*f=v;if(!f->pointer_present)return 1;*p=f->pointer;return 0;}
static bool write_pointer(void *v,const meeting_wifi_pointer *p){fake*f=v;if(f->fail_pointer)return false;f->pointer=*p;f->pointer_present=1;return true;}
static bool erase_slot(void *v,unsigned n){fake*f=v;f->present[n]=0;return true;}
static bool save_status(void *v,const meeting_switch_record *r){(void)v;(void)r;return true;}
static int load_status(void *v,meeting_switch_record *r){(void)v;(void)r;return 1;}
int main(void){
 fake f={0};meeting_wifi_slots_io io={&f,read_slot,write_slot,read_pointer,write_pointer,erase_slot};
 meeting_wifi_slot old={.generation=1,.ssid="old",.password="old-password"};
 f.slots[1]=old;f.present[1]=1;assert(meeting_wifi_slots_initialize(&io,&old)==MEETING_WIFI_SLOTS_CORRUPT);f.present[1]=0;
 f.fail_pointer=1;assert(meeting_wifi_slots_initialize(&io,&old)==MEETING_WIFI_SLOTS_IO_ERROR);
 assert(f.present[0]&&!f.pointer_present);
 meeting_wifi_slot mismatch=old;strcpy(mismatch.ssid,"different");
 assert(meeting_wifi_slots_initialize(&io,&mismatch)==MEETING_WIFI_SLOTS_CORRUPT);
 f.fail_pointer=0;assert(meeting_wifi_slots_initialize(&io,&old)==MEETING_WIFI_SLOTS_OK);
 assert(meeting_wifi_slots_initialize(&io,&old)==MEETING_WIFI_SLOTS_OK);
 meeting_wifi_slot candidate={.generation=2,.ssid="new",.password="new-password",.request_id={1},.digest={2}},active;unsigned slot;
 assert(meeting_wifi_slots_stage(&io,&candidate)==MEETING_WIFI_SLOTS_OK);
 meeting_wifi_slot conflicting=candidate;strcpy(conflicting.ssid,"other");
 assert(meeting_wifi_slots_stage(&io,&conflicting)==MEETING_WIFI_SLOTS_CORRUPT);
 assert(!strcmp(f.slots[1].ssid,"new"));
 meeting_switch_io status_io={.save=save_status,.load=load_status};meeting_wifi_switch txn;meeting_wifi_switch_init(&txn,&status_io);
 meeting_switch_request req={.id={1},.digest={2}};assert(meeting_wifi_switch_recover(&txn)==MEETING_SWITCH_OK);
 assert(meeting_wifi_switch_begin(&txn,&req)==MEETING_SWITCH_OK);
 assert(meeting_wifi_switch_advance(&txn,req.id,MEETING_SWITCH_TESTING)==MEETING_SWITCH_OK);
 assert(meeting_wifi_switch_advance(&txn,req.id,MEETING_SWITCH_COMMITTING)==MEETING_SWITCH_OK);
 meeting_wifi_slot_proof proof={&io,2};
 assert(meeting_wifi_switch_commit_confirmed(&txn,req.id,meeting_wifi_slot_commit_proof,&proof)==MEETING_SWITCH_INVALID);
 assert(!meeting_wifi_slots_verify_active(&io,2,candidate.request_id,candidate.digest));
 assert(meeting_wifi_slots_load(&io,&active,&slot)==MEETING_WIFI_SLOTS_OK&&slot==0&&!strcmp(active.ssid,"old"));
 uint8_t wrong[16]={3};assert(meeting_wifi_slots_commit(&io,2,wrong,candidate.digest)==MEETING_WIFI_SLOTS_CORRUPT);
 f.fail_pointer=1;assert(meeting_wifi_slots_commit(&io,2,candidate.request_id,candidate.digest)==MEETING_WIFI_SLOTS_IO_ERROR);
 assert(meeting_wifi_slots_load(&io,&active,&slot)==MEETING_WIFI_SLOTS_OK&&slot==0);
 f.fail_pointer=0;assert(meeting_wifi_slots_commit(&io,2,candidate.request_id,candidate.digest)==MEETING_WIFI_SLOTS_OK);
 assert(meeting_wifi_switch_commit_confirmed(&txn,req.id,meeting_wifi_slot_commit_proof,&proof)==MEETING_SWITCH_OK);
 assert(meeting_wifi_slots_verify_active(&io,2,candidate.request_id,candidate.digest));
 assert(!meeting_wifi_slots_verify_active(&io,2,wrong,candidate.digest));
 assert(meeting_wifi_slots_load(&io,&active,&slot)==MEETING_WIFI_SLOTS_OK&&slot==1&&!strcmp(active.ssid,"new"));
 f.pointer.generation=3;assert(meeting_wifi_slots_load(&io,&active,&slot)==MEETING_WIFI_SLOTS_CORRUPT);
 puts("wifi slot commit and reboot recovery: PASS");
}
