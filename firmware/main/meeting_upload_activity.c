#include "meeting_upload_activity.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#else
#include <stdatomic.h>
#endif

/* A bounded, non-evicting cache prevents old acknowledgements from becoming
 * new progress after retries. When full, unknown identities fail closed: they
 * upload normally but cannot extend the BLE scheduling opportunity. */
typedef struct { char id[37]; uint8_t digest[32]; uint32_t next; bool confirmed; } watermark;
static watermark entries[32];
static meeting_activity_snapshot current;
static watermark *bound;
#ifdef ESP_PLATFORM
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static void lock(void){portENTER_CRITICAL(&guard);}
static void unlock(void){portEXIT_CRITICAL(&guard);}
#else
static atomic_flag guard=ATOMIC_FLAG_INIT;
static void lock(void){while(atomic_flag_test_and_set_explicit(&guard,memory_order_acquire)) {}}
static void unlock(void){atomic_flag_clear_explicit(&guard,memory_order_release);}
#endif
uint64_t meeting_upload_activity_begin(void){
    lock(); current.generation++; if(!current.generation) current.generation++;
    current.phase=MEETING_ACTIVITY_PREPARING; current.meeting_id[0]='\0';
    memset(current.digest,0,sizeof(current.digest)); current.confirmed_next=0; bound=NULL;
    uint64_t generation=current.generation; unlock(); return generation;
}
bool meeting_upload_activity_bind(uint64_t generation,const char *id,const uint8_t digest[32]){
    if(!id||!digest||!id[0]||strlen(id)>=sizeof(current.meeting_id)) return false;
    lock();
    if(generation!=current.generation||current.phase!=MEETING_ACTIVITY_PREPARING){unlock();return false;}
    watermark *free_entry=NULL;
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++){
        if(!entries[i].id[0]) {if(!free_entry)free_entry=&entries[i];continue;}
        if(!strcmp(entries[i].id,id)&&!memcmp(entries[i].digest,digest,32)){bound=&entries[i];break;}
    }
    if(!bound&&free_entry){bound=free_entry;strcpy(bound->id,id);memcpy(bound->digest,digest,32);}
    strcpy(current.meeting_id,id);memcpy(current.digest,digest,32);
    current.confirmed_next=bound?bound->next:0;current.phase=MEETING_ACTIVITY_UPLOADING;
    unlock();return true;
}
bool meeting_upload_activity_progress(uint64_t generation,uint32_t next){
    lock();
    if(generation!=current.generation||current.phase!=MEETING_ACTIVITY_UPLOADING){unlock();return false;}
    if(bound&&next>bound->next){bound->next=next;current.confirmed_next=next;current.progress_serial++;}
    unlock();return true;
}
bool meeting_upload_activity_finish(uint64_t generation,meeting_activity_phase phase){
    if(phase!=MEETING_ACTIVITY_PAUSED&&phase!=MEETING_ACTIVITY_FAILED&&phase!=MEETING_ACTIVITY_CONFIRMED)return false;
    lock();
    if(generation!=current.generation||(current.phase!=MEETING_ACTIVITY_PREPARING&&current.phase!=MEETING_ACTIVITY_UPLOADING)){unlock();return false;}
    if(phase==MEETING_ACTIVITY_CONFIRMED&&bound&&!bound->confirmed){bound->confirmed=true;current.progress_serial++;}
    if(phase==MEETING_ACTIVITY_PAUSED||phase==MEETING_ACTIVITY_FAILED){current.failure_serial++;current.failure_progress_serial=current.progress_serial;}
    current.phase=phase;unlock();return true;
}
bool meeting_upload_activity_retire(uint64_t generation){
    lock();
    if(generation!=current.generation||current.phase!=MEETING_ACTIVITY_CONFIRMED||!bound){unlock();return false;}
    memset(bound,0,sizeof(*bound));bound=NULL;unlock();return true;
}
meeting_activity_snapshot meeting_upload_activity_snapshot(void){lock();meeting_activity_snapshot result=current;unlock();return result;}
