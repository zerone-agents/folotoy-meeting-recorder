#define _POSIX_C_SOURCE 200809L
#include "sc_store.h"
#include "meeting_record_nvs.h"
#include "meeting_seal.h"
#include "meeting_retirement.h"
#include "meeting_cancel_request.h"
#include "meeting_upload.h"
#include "meeting_store_upload.h"
#include "esp_littlefs.h"
#include "freertos/semphr.h"
#include <mbedtls/sha256.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {int handle;char key[16];uint8_t value[256];size_t length;} entry;
static uint32_t test_crc32(const uint8_t *p,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=p[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static unsigned online_calls;
meeting_upload_result meeting_online_session(FILE *f,const meeting_snapshot *s,bool ht,int64_t rt,uint8_t trust,const meeting_online_config *c,meeting_manifest *m)
{(void)f;(void)s;(void)ht;(void)rt;(void)trust;(void)c;(void)m;online_calls++;return MEETING_UPLOAD_FAILED;}
static entry entries[64];static int fail_index_unlink,fail_erase_handle,fail_seal_write,fail_delete_progress,fail_record_write,fail_retired_open,fail_retired_read,retired_namespace_missing;
struct fake_nvs_iterator {int position,handle;};static struct fake_nvs_iterator iterator;
static int namespace_id(const char *s){if(!strcmp(s,"meeting_rec"))return 1;if(!strcmp(s,"meeting_snap"))return 2;if(!strcmp(s,"meeting_ack"))return 3;if(!strcmp(s,"meeting_clean"))return 4;if(!strcmp(s,"meeting_done"))return 5;if(!strcmp(s,"meeting_dproof"))return 7;return 6;}
static entry *find_entry(int h,const char *key){for(size_t i=0;i<64;i++)if(entries[i].handle==h&&!strcmp(entries[i].key,key))return &entries[i];return NULL;}
esp_err_t nvs_open(const char *s,int mode,nvs_handle_t *h){(void)mode;if(!strcmp(s,"meeting_done")){if(fail_retired_open)return ESP_FAIL;if(retired_namespace_missing)return ESP_ERR_NVS_NOT_FOUND;}*h=namespace_id(s);return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *len){if(fail_retired_read&&h==5)return ESP_FAIL;entry *e=find_entry(h,key);if(!e)return ESP_ERR_NVS_NOT_FOUND;if(!out){*len=e->length;return ESP_OK;}if(*len<e->length)return ESP_ERR_NVS_INVALID_LENGTH;memcpy(out,e->value,e->length);*len=e->length;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t len){if(fail_record_write&&h==1&&key[0]=='r')return ESP_FAIL;if(fail_seal_write&&h==1&&key[0]=='s')return ESP_FAIL;if(fail_delete_progress&&h==6&&!strcmp(key,"x0000000e1"))return ESP_FAIL;entry *e=find_entry(h,key);if(!e){for(size_t i=0;i<64;i++)if(!entries[i].handle){e=&entries[i];e->handle=h;strcpy(e->key,key);break;}}assert(e&&len<=sizeof(e->value));memcpy(e->value,data,len);e->length=len;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){if(h==fail_erase_handle)return ESP_FAIL;entry *e=find_entry(h,key);if(!e)return ESP_ERR_NVS_NOT_FOUND;memset(e,0,sizeof(*e));return ESP_OK;}
static esp_err_t next_entry(nvs_iterator_t *it,int start){for(int p=start;p<64;p++)if(entries[p].handle==iterator.handle){iterator.position=p;*it=&iterator;return ESP_OK;}*it=NULL;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_entry_find(const char *partition,const char *ns,int type,nvs_iterator_t *it){assert(!strcmp(partition,"nvs")&&type==NVS_TYPE_ANY);iterator.handle=namespace_id(ns);return next_entry(it,0);}
esp_err_t nvs_entry_next(nvs_iterator_t *it){return next_entry(it,(*it)->position+1);}
esp_err_t nvs_entry_info(nvs_iterator_t it,nvs_entry_info_t *info){memset(info,0,sizeof(*info));strcpy(info->key,entries[it->position].key);info->type=NVS_TYPE_BLOB;return ESP_OK;}
void nvs_release_iterator(nvs_iterator_t it){(void)it;}
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){return (void *)1;}
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t h,unsigned t){(void)h;(void)t;return pdTRUE;}
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t h){(void)h;return pdTRUE;}
esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c){assert(!c->format_if_mount_failed);return ESP_OK;}
static int fail_space_info;
esp_err_t esp_littlefs_info(const char *partition,size_t *total,size_t *used){(void)partition;if(fail_space_info)return ESP_FAIL;*total=5000000;*used=1000;return ESP_OK;}
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->value=1;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;c->value=1;return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)c->value=c->value*33+p[i];return 0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char out[32]){for(int i=0;i<32;i++)out[i]=(unsigned char)(c->value>>((i%4)*8));return 0;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
void esp_fill_random(void *buffer,size_t length){memset(buffer,1,length);}
void sc_crypto_random(uint8_t *buffer,size_t length){memset(buffer,2,length);}
int meeting_test_unlink(const char *path){size_t n=strlen(path);if(fail_index_unlink&&n>=4&&!strcmp(path+n-4,".idx")){errno=EIO;return -1;}return unlink(path);}
static bool not_cancelled(void *ctx){(void)ctx;return false;}
static bool unused_request(void *ctx,const char *method,const char *path,const char *type,const char *hash,const uint8_t *body,size_t length,int *status,char *response,size_t cap,size_t *response_len){(void)ctx;(void)method;(void)path;(void)type;(void)hash;(void)body;(void)length;(void)status;(void)response;(void)cap;(void)response_len;return false;}
static int remote_result,remote_calls;
int meeting_cancel_request(const uint8_t id[16],const meeting_manifest *expected,const meeting_upload_transport *transport){assert(id&&expected&&transport&&expected->bytes==100);assert(expected->recorded_at_present&&expected->recorded_at==1800000000&&expected->time_trust==2);remote_calls++;assert(sc_store_begin(15,0)==ESP_ERR_INVALID_STATE);return remote_result;}
void sc_store_test_simulate_reboot(void);
static void write32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
int main(void)
{
    char recdir[256],audio[256],index[256];assert(mkdir(SC_STORE_BASE_PATH,0700)==0);
    snprintf(recdir,sizeof(recdir),"%s/rec",SC_STORE_BASE_PATH);assert(mkdir(recdir,0700)==0);
    snprintf(audio,sizeof(audio),"%s/rec/7.dat",SC_STORE_BASE_PATH);snprintf(index,sizeof(index),"%s/rec/7.idx",SC_STORE_BASE_PATH);
    uint8_t data[100]={0};memcpy(data,"SCR1",4);data[4]=1;write32(data+8,7);write32(data+20,1);data[96]=3;data[97]=0xf8;data[98]=0xff;data[99]=0xfe;
    FILE *f=fopen(audio,"wb");assert(f&&fwrite(data,1,sizeof(data),f)==sizeof(data)&&fclose(f)==0);
    f=fopen(index,"wb");assert(f&&fputc(1,f)!=EOF&&fclose(f)==0);
    meeting_record record={.file_id=7,.meeting_id={0x12,0x34,0x56,0x78,0x12,0x34,0x41,0x23,0x81,0x23,0x12,0x34,0x56,0x78,0x9a,0xbc}};
    assert(meeting_record_nvs_create(1,&record)&&meeting_record_nvs_end_intent(1,7,1));
    assert(!sc_store_ready());
    assert(sc_store_init()==ESP_OK&&sc_store_ready()&&sc_store_count()==1);
    FILE *source=sc_store_upload_acquire(7);assert(source);meeting_snapshot snapshot;
    assert(sc_store_prepare_snapshot(7,&snapshot,not_cancelled,NULL)==ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    assert(sc_store_cleanup_confirmed(7,not_cancelled,NULL)==MEETING_CLEANUP_ERROR&&access(audio,F_OK)==0);
    source=sc_store_upload_acquire(7);assert(source);meeting_manifest manifest;
    assert(meeting_upload_manifest(source,&snapshot,false,0,0,&manifest));assert(sc_store_confirm_upload(source,&snapshot,&manifest)==ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    /* An unknown, CRC-valid snapshot alongside a known slot must not authorize cleanup. */
    entry *old_snapshot=find_entry(2,"m000000070");assert(old_snapshot && old_snapshot->length==MEETING_SNAPSHOT_SIZE);
    uint8_t future[MEETING_SNAPSHOT_SIZE];memcpy(future,old_snapshot->value,sizeof future);future[77]=1;
    uint32_t crc=test_crc32(future,84);for(unsigned i=0;i<4;i++)future[84+i]=(uint8_t)(crc>>(8*i));
    assert(nvs_set_blob(2,"m000000071",future,sizeof future)==ESP_OK);
    source=sc_store_upload_acquire(7);assert(source);
    assert(sc_store_prepare_snapshot(7,&snapshot,not_cancelled,NULL)!=ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    meeting_online_config upload_cfg={.https={.is_cancelled=not_cancelled}};
    assert(meeting_store_upload(7,false,0,0,&upload_cfg)==MEETING_UPLOAD_FAILED && online_calls==0);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(meeting_store_upload(7,false,0,0,&upload_cfg)==MEETING_UPLOAD_FAILED && online_calls==0);
    source=sc_store_upload_acquire(7);assert(source);
    assert(sc_store_prepare_snapshot(7,&snapshot,not_cancelled,NULL)!=ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    assert(sc_store_cleanup_confirmed(7,not_cancelled,NULL)==MEETING_CLEANUP_ERROR);
    assert(access(audio,F_OK)==0 && access(index,F_OK)==0 && find_entry(3,"c00000007") && !find_entry(4,"c00000007"));
    /* Unknown format remains unknown to deletion; even an explicit unknown-state
     * intent cannot create the required proof or remove the recording. */
    assert(sc_store_delete_visibility(7,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN);
    assert(sc_store_begin_delete_checked(7,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_ERR_INVALID_STATE);
    assert(sc_store_begin_delete_checked(7,MEETING_DELETE_UNKNOWN,not_cancelled,NULL)==ESP_OK);
    assert(sc_store_prepare_delete_proof(7,not_cancelled,NULL)!=ESP_OK);
    assert(sc_store_finish_delete_local(7,not_cancelled,NULL)!=ESP_OK);
    assert(access(audio,F_OK)==0 && access(index,F_OK)==0 && find_entry(6,"x000000070"));
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(find_entry(6,"x000000070") && access(audio,F_OK)==0 && access(index,F_OK)==0);
    assert(!sc_store_upload_acquire(7));
    assert(sc_store_begin_delete_checked(7,MEETING_DELETE_UNKNOWN,not_cancelled,NULL)==ESP_OK);
    assert(sc_store_prepare_delete_proof(7,not_cancelled,NULL)!=ESP_OK);
    assert(sc_store_finish_delete_local(7,not_cancelled,NULL)!=ESP_OK);
    assert(find_entry(6,"x000000070") && access(audio,F_OK)==0 && access(index,F_OK)==0);
    assert(nvs_erase_key(6,"x000000070")==ESP_OK);
    /* A pre-existing cleanup intent must also fail closed on reboot. */
    entry *ack=find_entry(3,"c00000007");assert(ack);
    assert(nvs_set_blob(4,"c00000007",ack->value,ack->length)==ESP_OK);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(sc_store_cleanup_recovery_failed());
    assert(access(audio,F_OK)==0 && access(index,F_OK)==0 && find_entry(3,"c00000007") && find_entry(4,"c00000007"));
    assert(sc_store_resume_cleanups(not_cancelled,NULL)==MEETING_CLEANUP_ERROR);
    assert(access(audio,F_OK)==0 && access(index,F_OK)==0 && find_entry(4,"c00000007"));
    assert(nvs_erase_key(4,"c00000007")==ESP_OK);
    assert(nvs_erase_key(2,"m000000071")==ESP_OK);
    f=fopen(audio,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfd,f)!=EOF&&fclose(f)==0);
    assert(sc_store_cleanup_confirmed(7,not_cancelled,NULL)==MEETING_CLEANUP_ERROR);
    assert(access(audio,F_OK)==0&&access(index,F_OK)==0&&!find_entry(4,"c00000007"));
    f=fopen(audio,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfe,f)!=EOF&&fclose(f)==0);
    fail_index_unlink=1;assert(sc_store_cleanup_confirmed(7,not_cancelled,NULL)==MEETING_CLEANUP_ERROR);
    assert(access(audio,F_OK)!=0&&access(index,F_OK)==0&&find_entry(4,"c00000007"));
    fail_index_unlink=0;sc_store_test_simulate_reboot();assert(!sc_store_ready());assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(!sc_store_cleanup_recovery_failed());
    assert(access(index,F_OK)!=0&&sc_store_count()==0);
    assert(sc_store_resume_cleanups(not_cancelled,NULL)==MEETING_CLEANUP_DONE);
    /* Next fresh mount confirms file deletion before writing the compact marker. */
    fail_erase_handle=3;sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(sc_store_cleanup_recovery_failed()&&find_entry(5,"d00000007")&&find_entry(4,"c00000007"));
    fail_erase_handle=0;sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(!sc_store_cleanup_recovery_failed()&&find_entry(5,"d00000007"));
    assert(!find_entry(1,"r00000007")&&!find_entry(2,"m000000070")&&!find_entry(3,"c00000007")&&!find_entry(4,"c00000007"));
    assert(sc_store_begin(7,0)!=ESP_OK); /* the compact marker forbids reuse */
    /* A verified header-only interrupted recording is retained and visible,
     * but must not disable unrelated recordings or be treated as audio. */
    snprintf(audio,sizeof(audio),"%s/rec/8.dat",SC_STORE_BASE_PATH);
    memset(data,0,sizeof(data));memcpy(data,"SCR1",4);data[4]=1;write32(data+8,8);write32(data+20,UINT32_MAX);
    f=fopen(audio,"wb");assert(f&&fwrite(data,1,96,f)==96&&fclose(f)==0);
    sc_store_test_simulate_reboot();assert(sc_store_init()!=ESP_OK&&!sc_store_ready());
    assert(access(audio,F_OK)==0); /* missing identity remains unsafe */
    record.file_id=8;record.end_intent=0;assert(meeting_record_nvs_create(1,&record));
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_has_empty_interrupted()&&sc_store_count()==1&&access(audio,F_OK)==0);
    sc_file_entry_t empty;assert(sc_store_list(&empty,1)==1&&empty.file_id==8&&empty.size==0);
    /* Identity persistence failure prevents file creation and recording start. */
    char rejected_audio[128];snprintf(rejected_audio,sizeof(rejected_audio),"%s/rec/10.dat",SC_STORE_BASE_PATH);
    fail_record_write=1;assert(sc_store_begin_timed(10,1800000000,1800000000)!=ESP_OK);
    assert(access(rejected_audio,F_OK)!=0&&sc_store_count()==1&&sc_store_ready());fail_record_write=0;
    assert(sc_store_begin_timed(9,1800000000,1800000000)==ESP_OK);
    bool time_present=false;int64_t capture_time=0;uint8_t time_trust=0;
    assert(sc_store_record_time(9,&time_present,&capture_time,&time_trust)&&time_present&&capture_time==1800000000&&time_trust==2);
    const uint8_t frame[]={0xf8,0xff,0xfe};
    assert(sc_store_append(frame,sizeof(frame))==ESP_OK&&sc_store_end(0,4)==ESP_OK);
    snprintf(index,sizeof(index),"%s/rec/9.dat",SC_STORE_BASE_PATH);
    f=fopen(index,"rb");assert(f&&fread(data,1,96,f)==96&&fclose(f)==0);
    assert(data[20]==0xff&&data[21]==0xff&&data[22]==0xff&&data[23]==0xff);
    entry *seal=find_entry(1,"s00000009");assert(seal);
    uint8_t saved_seal[MEETING_SEAL_SIZE];memcpy(saved_seal,seal->value,sizeof(saved_seal));meeting_seal altered;
    assert(meeting_seal_decode(saved_seal,&altered));altered.meeting_id[0]^=1;assert(meeting_seal_encode(&altered,seal->value));
    source=sc_store_upload_acquire(9);assert(source);
    assert(sc_store_prepare_snapshot(9,&snapshot,not_cancelled,NULL)!=ESP_OK&&sc_store_upload_release(source)==ESP_OK);
    memcpy(seal->value,saved_seal,sizeof(saved_seal));seal->value[8]^=1;
    source=sc_store_upload_acquire(9);assert(source);
    assert(sc_store_prepare_snapshot(9,&snapshot,not_cancelled,NULL)!=ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    sc_store_test_simulate_reboot();assert(sc_store_init()!=ESP_OK&&!sc_store_ready());
    assert(access(index,F_OK)==0); /* damaged proof must not delete the audio */
    seal->value[8]^=1;
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    /* A valid retired marker alongside a retained audio file is inconsistent.
     * Recovery must not make this retired file eligible for upload. */
    meeting_record rec9;assert(meeting_record_nvs_read(1,9,&rec9)==MEETING_RECORD_OK);
    meeting_retired retired9={.file_id=9,.complete=true};memcpy(retired9.meeting_id,rec9.meeting_id,16);
    uint8_t retired_bytes[MEETING_RETIRED_SIZE];assert(meeting_retired_encode(&retired9,retired_bytes));
    assert(nvs_set_blob(5,"d00000009",retired_bytes,sizeof retired_bytes)==ESP_OK);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_cleanup_recovery_failed());
    assert(!sc_store_upload_acquire(9));assert(access(index,F_OK)==0);
    source=sc_store_upload_acquire(8);assert(source&&sc_store_upload_release(source)==ESP_OK);
    entry *retired_entry=find_entry(5,"d00000009");assert(retired_entry);
    retired9.complete=false;assert(meeting_retired_encode(&retired9,retired_entry->value));
    assert(!sc_store_upload_acquire(9));retired9.complete=true;assert(meeting_retired_encode(&retired9,retired_entry->value));
    retired_entry->value[0]^=1;assert(!sc_store_upload_acquire(9));retired_entry->value[0]^=1;
    fail_retired_read=1;assert(!sc_store_upload_acquire(9));fail_retired_read=0;
    fail_retired_open=1;assert(!sc_store_upload_acquire(9));fail_retired_open=0;
    assert(nvs_erase_key(5,"d00000009")==ESP_OK);sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&!sc_store_cleanup_recovery_failed());
    retired_namespace_missing=1;source=sc_store_upload_acquire(9);assert(source&&sc_store_upload_release(source)==ESP_OK);retired_namespace_missing=0;
    source=sc_store_upload_acquire(9);assert(source);
    assert(sc_store_begin_delete_checked(9,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_ERR_INVALID_STATE); /* wait for upload ownership */
    assert(sc_store_upload_release(source)==ESP_OK);
    assert(sc_store_begin_delete_checked(9,MEETING_DELETE_CONFIRMED,not_cancelled,NULL)==ESP_ERR_INVALID_STATE);
    assert(!find_entry(6,"x000000090"));
    assert(sc_store_begin_delete_checked(9,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_OK&&sc_store_begin_delete_checked(9,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_OK);
    assert(!sc_store_upload_acquire(9)); /* persisted intent blocks re-upload */
    assert(sc_store_prepare_delete_proof(9,not_cancelled,NULL)==ESP_OK&&find_entry(7,"p00000009"));
    meeting_confirmation expected_proof;entry *proof_entry=find_entry(7,"p00000009");
    assert(meeting_confirmation_decode(proof_entry->value,&expected_proof));
    assert(expected_proof.manifest.recorded_at_present&&expected_proof.manifest.recorded_at==1800000000&&expected_proof.manifest.time_trust==2);
    assert(!find_entry(3,"c00000009")); /* expected manifest is not an ack */
    assert(nvs_erase_key(7,"p00000009")==ESP_OK);
    assert(nvs_erase_key(6,"x000000090")==ESP_OK);
    source=sc_store_upload_acquire(9);assert(source);
    assert(sc_store_prepare_snapshot(9,&snapshot,not_cancelled,NULL)==ESP_OK&&snapshot.reason==3);
    assert(meeting_upload_manifest(source,&snapshot,true,1800000000,2,&manifest)&&manifest.reason==3);
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN); /* upload owns it */
    assert(sc_store_upload_release(source)==ESP_OK);
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_UNCONFIRMED);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_cleanup_confirmed(9,not_cancelled,NULL)==MEETING_CLEANUP_ERROR&&access(index,F_OK)==0);
    source=sc_store_upload_acquire(9);assert(source);
    assert(sc_store_prepare_snapshot(9,&snapshot,not_cancelled,NULL)==ESP_OK);
    assert(sc_store_confirm_upload(source,&snapshot,&manifest)==ESP_OK);
    assert(sc_store_upload_release(source)==ESP_OK);
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_CONFIRMED);
    entry *timed_record=find_entry(1,"r00000009");assert(timed_record);
    timed_record->value[28]^=1;
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN);
    timed_record->value[28]^=1;
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_CONFIRMED);
    assert(sc_store_begin_delete_checked(9,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_ERR_INVALID_STATE); /* confirmation changed after review */
    assert(!find_entry(6,"x000000090"));
    f=fopen(index,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfd,f)!=EOF&&fclose(f)==0);
    assert(sc_store_delete_visibility(9,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN);
    assert(sc_store_cleanup_confirmed(9,not_cancelled,NULL)==MEETING_CLEANUP_ERROR&&access(index,F_OK)==0);
    f=fopen(index,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfe,f)!=EOF&&fclose(f)==0);
    assert(sc_store_cleanup_confirmed(9,not_cancelled,NULL)==MEETING_CLEANUP_DONE);
    assert(find_entry(1,"s00000009")); /* metadata remains until fresh mount */
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(!find_entry(1,"s00000009")); /* retirement also removes the seal */
    /* Losing the final seal cannot turn the end intent into a complete
     * meeting. Preserve the source and recover it as interrupted. */
    assert(sc_store_begin(10,0)==ESP_OK&&sc_store_append(frame,sizeof(frame))==ESP_OK);
    fail_seal_write=1;assert(sc_store_end(0,1)!=ESP_OK&&!sc_store_ready());fail_seal_write=0;
    assert(sc_store_begin(11,0)!=ESP_OK); /* no new writes while storage state is uncertain */
    snprintf(index,sizeof(index),"%s/rec/10.dat",SC_STORE_BASE_PATH);assert(access(index,F_OK)==0);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    source=sc_store_upload_acquire(10);assert(source);
    assert(sc_store_prepare_snapshot(10,&snapshot,not_cancelled,NULL)==ESP_OK&&snapshot.reason==3);
    assert(sc_store_upload_release(source)==ESP_OK&&access(index,F_OK)==0);
    /* A successfully sealed user stop must stay distinct from interruption. */
    assert(sc_store_begin(11,0)==ESP_OK&&sc_store_append(frame,sizeof(frame))==ESP_OK&&sc_store_end(0,1)==ESP_OK);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    source=sc_store_upload_acquire(11);assert(source);
    assert(sc_store_prepare_snapshot(11,&snapshot,not_cancelled,NULL)==ESP_OK&&snapshot.reason==0);
    assert(meeting_upload_manifest(source,&snapshot,false,0,0,&manifest)&&manifest.reason==0);
    assert(sc_store_upload_release(source)==ESP_OK);
    for(uint32_t id=12;id<=13;id++){
        uint8_t end_intent=(uint8_t)(id-10); /* limit, then storage full */
        assert(sc_store_begin(id,0)==ESP_OK&&sc_store_append(frame,sizeof(frame))==ESP_OK&&sc_store_end(0,end_intent)==ESP_OK);
        source=sc_store_upload_acquire(id);assert(source);
        assert(sc_store_prepare_snapshot(id,&snapshot,not_cancelled,NULL)==ESP_OK&&snapshot.reason==end_intent-1);
        assert(meeting_upload_manifest(source,&snapshot,false,0,0,&manifest)&&manifest.reason==end_intent-1);
        assert(sc_store_upload_release(source)==ESP_OK);
    }
    sc_file_entry_t all[8],window[2];size_t count=sc_store_list(all,8);assert(count>=4);
    assert(sc_store_list_range(1,window,2)==2&&window[0].file_id==all[1].file_id&&window[1].file_id==all[2].file_id);
    assert(sc_store_list_range(count,window,2)==0&&sc_store_list_range(SIZE_MAX,window,2)==0);
    assert(sc_store_begin_timed(14,1800000000,1800000000)==ESP_OK&&sc_store_append(frame,sizeof(frame))==ESP_OK&&sc_store_end(0,1)==ESP_OK);
    assert(sc_store_begin_delete_checked(14,MEETING_DELETE_UNCONFIRMED,not_cancelled,NULL)==ESP_OK);
    snprintf(index,sizeof(index),"%s/rec/14.dat",SC_STORE_BASE_PATH);
    assert(sc_store_finish_delete_local(14,not_cancelled,NULL)!=ESP_OK&&access(index,F_OK)==0); /* no proof yet */
    assert(sc_store_prepare_delete_proof(14,not_cancelled,NULL)==ESP_OK);
    f=fopen(index,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfd,f)!=EOF&&fclose(f)==0);
    assert(sc_store_finish_delete_local(14,not_cancelled,NULL)!=ESP_OK&&access(index,F_OK)==0);
    f=fopen(index,"r+b");assert(f&&fseek(f,99,SEEK_SET)==0&&fputc(0xfe,f)!=EOF&&fclose(f)==0);
    sc_reader_t *reader=sc_store_reader_open(14);uint8_t read_buf[1][SC_UNIT_DATA],read_len[1];
    assert(reader&&sc_store_reader_read(reader,0,read_buf,read_len,1)==1);
    fail_space_info=1;assert(sc_store_finish_delete_local(14,not_cancelled,NULL)!=ESP_OK&&access(index,F_OK)!=0);fail_space_info=0;
    fail_delete_progress=1;assert(sc_store_finish_delete_local(14,not_cancelled,NULL)!=ESP_OK);fail_delete_progress=0;
    assert(sc_store_reader_read(reader,0,read_buf,read_len,1)<0);sc_store_reader_close(reader);
    assert(find_entry(6,"x0000000e0")&&find_entry(7,"p0000000e"));
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_finish_delete_local(14,not_cancelled,NULL)==ESP_OK&&!sc_store_upload_acquire(14));
    meeting_upload_transport remote={.request=unused_request,.cancelled=not_cancelled};
    assert(sc_store_cancel_delete_remote(14,&remote)==ESP_ERR_INVALID_STATE&&remote_calls==1);
    remote_result=1;assert(sc_store_cancel_delete_remote(14,&remote)==ESP_OK&&remote_calls==2);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_cancel_delete_remote(14,&remote)==ESP_OK&&remote_calls==2&&!sc_store_upload_acquire(14));
    /* A legacy file has no meeting identity. Persist a local-only intent
     * before removing anything, then recover a crash between the two unlinks. */
    snprintf(audio,sizeof(audio),"%s/rec/20.dat",SC_STORE_BASE_PATH);
    snprintf(index,sizeof(index),"%s/rec/20.idx",SC_STORE_BASE_PATH);
    memset(data,0,sizeof(data));memcpy(data,"SCR1",4);data[4]=1;write32(data+8,20);write32(data+20,1);data[96]=3;data[97]=0xf8;data[98]=0xff;data[99]=0xfe;
    f=fopen(audio,"wb");assert(f&&fwrite(data,1,sizeof(data),f)==sizeof(data)&&fclose(f)==0);
    f=fopen(index,"wb");assert(f&&fputc(1,f)!=EOF&&fclose(f)==0);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_delete_visibility(20,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN);
    assert(sc_store_begin_delete_checked(20,MEETING_DELETE_UNKNOWN,not_cancelled,NULL)==ESP_OK);
    assert(find_entry(6,"x000000140")&&!sc_store_upload_acquire(20));
    assert(unlink(audio)==0); /* interruption after audio removal */
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_begin(20,0)!=ESP_OK); /* intent also prevents ID reuse */
    assert(sc_store_finish_delete_local(20,not_cancelled,NULL)==ESP_OK&&access(index,F_OK)!=0);
    int remote_before=remote_calls;assert(sc_store_cancel_delete_remote(20,&remote)==ESP_OK&&remote_calls==remote_before);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_finish_delete_local(20,not_cancelled,NULL)==ESP_OK);
    assert(!sc_store_upload_acquire(20)&&sc_store_begin(20,0)!=ESP_OK);
    /* Bad identity data is not treated as a missing legacy record. */
    snprintf(index,sizeof(index),"%s/rec/21.dat",SC_STORE_BASE_PATH);write32(data+8,21);
    f=fopen(index,"wb");assert(f&&fwrite(data,1,sizeof(data),f)==sizeof(data)&&fclose(f)==0);
    uint8_t damaged=1;assert(nvs_set_blob(1,"r00000015",&damaged,1)==ESP_OK);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK);
    assert(sc_store_begin_delete_checked(21,MEETING_DELETE_UNKNOWN,not_cancelled,NULL)!=ESP_OK&&access(index,F_OK)==0);
    assert(unlink(index)==0);
    /* Simulate a reset: the retained empty file still has not been deleted. */
    snprintf(audio,sizeof(audio),"%s/rec/8.dat",SC_STORE_BASE_PATH);
    assert(access(audio,F_OK)==0);
    assert(sc_store_delete_visibility(8,not_cancelled,NULL)==MEETING_DELETE_UNKNOWN);
    assert(sc_store_begin_delete_checked(8,MEETING_DELETE_UNKNOWN,not_cancelled,NULL)==ESP_OK);
    assert(unlink(audio)==0); /* reset after deleting the empty data file */
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    assert(sc_store_finish_delete_local(8,not_cancelled,NULL)==ESP_OK&&access(audio,F_OK)!=0);
    assert(!sc_store_has_empty_interrupted()&&!sc_store_upload_acquire(8)&&sc_store_begin(8,0)!=ESP_OK);
    remote_before=remote_calls;assert(sc_store_cancel_delete_remote(8,&remote)==ESP_OK&&remote_calls==remote_before);
    /* A failed end-intent commit retains audio and recovers as interrupted. */
    assert(sc_store_begin(16,0)==ESP_OK&&sc_store_append(frame,sizeof frame)==ESP_OK);
    fail_record_write=1;assert(sc_store_end(0,1)!=ESP_OK&&!sc_store_ready());fail_record_write=0;
    snprintf(index,sizeof(index),"%s/rec/16.dat",SC_STORE_BASE_PATH);assert(access(index,F_OK)==0);
    sc_store_test_simulate_reboot();assert(sc_store_init()==ESP_OK&&sc_store_ready());
    source=sc_store_upload_acquire(16);assert(source&&sc_store_prepare_snapshot(16,&snapshot,not_cancelled,NULL)==ESP_OK&&snapshot.reason==3);
    assert(sc_store_upload_release(source)==ESP_OK&&access(index,F_OK)==0);
    /* The following cleanup is for this host fixture only. */
    snprintf(index,sizeof(index),"%s/rec/16.dat",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/16.idx",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/9.dat",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/9.idx",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/10.dat",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/10.idx",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/11.dat",SC_STORE_BASE_PATH);unlink(index);
    snprintf(index,sizeof(index),"%s/rec/11.idx",SC_STORE_BASE_PATH);unlink(index);
    for(unsigned id=12;id<=13;id++){
        snprintf(index,sizeof(index),"%s/rec/%u.dat",SC_STORE_BASE_PATH,id);unlink(index);
        snprintf(index,sizeof(index),"%s/rec/%u.idx",SC_STORE_BASE_PATH,id);unlink(index);
    }
    snprintf(audio,sizeof(audio),"%s/rec/8.dat",SC_STORE_BASE_PATH);unlink(audio);
    assert(rmdir(recdir)==0&&rmdir(SC_STORE_BASE_PATH)==0);return 0;
}
