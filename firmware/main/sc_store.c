// main/sc_store.c —— 录音文件存储实现(LittleFS)。
#include "sc_store.h"

#include "sc_crypto.h"
#include "sc_ring.h"
#include "meeting_scr1_file.h"
#include "meeting_create_file.h"
#include "meeting_recovery.h"
#include "meeting_snapshot_nvs.h"
#include "meeting_record_nvs.h"
#include "meeting_seal_nvs.h"
#include "meeting_deletion_store.h"
#include "meeting_delete_proof.h"
#include "meeting_delete_proof_nvs.h"
#include "meeting_cancel_request.h"
#include "meeting_confirmation_nvs.h"
#include "meeting_upload.h"
#include "meeting_cleanup_files.h"
#include "meeting_retirement_nvs.h"
#include "esp_random.h"
#include "nvs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "sc_store";

#ifndef SC_STORE_BASE_PATH
#define SC_STORE_BASE_PATH "/lfs"
#endif
#define BASE_PATH     SC_STORE_BASE_PATH
#define REC_DIR       BASE_PATH "/rec"
#define PART_LABEL    "storage"
#define HDR_SIZE      96u
#define HDR_MAGIC     0x31524353u   // "SCR1"
#define HDR_VERSION   1u
#define FRAMES_OPEN   0xFFFFFFFFu
#define SYNC_EVERY    50u           // 约 1 秒同步一次文件
#define FRAMES_PER_S  50u

typedef struct {
    uint32_t file_id;
    uint32_t start_time;
    uint32_t end_time;
    uint32_t frames;
} cat_entry_t;

typedef struct {
    FILE *f;
    FILE *idx;
    uint32_t file_id;
    uint32_t start_time;
    uint32_t frames;
    uint32_t synced;            // 已 fsync 的帧数(其他句柄可读到)
    bool failed;                // Any write/sync failure leaves file recoverable
    uint32_t pos;               // 下一条记录的文件偏移
    uint8_t file_key[32];
    uint8_t nonce[16];
} live_t;

struct sc_reader {
    bool used;
    uint32_t file_id;
    FILE *f;
    uint32_t cur_seq;           // f 当前位置对应的帧序号
    uint32_t visible;           // 打开时文件内可读的帧数
};

static SemaphoreHandle_t s_mutex;
static cat_entry_t s_cat[SC_STORE_MAX_FILES];
static size_t s_cat_count;
static live_t s_live;
static sc_ring_t s_ring;
static sc_reader_t s_readers[SC_STORE_READERS];
static FILE *s_upload_file;
static uint32_t s_upload_id;
static uint32_t s_cleanup_id;
static bool s_cleanup_enumerating;
static bool s_cleanup_recovery_failed;
static bool s_empty_interrupted;
static bool s_ready;
bool sc_store_ready(void) { return s_ready; }
bool sc_store_has_empty_interrupted(void) { return s_empty_interrupted; }
static bool s_mounted;

#ifdef SC_STORE_TEST
/* Host fault-injection only: model loss of all volatile state at reboot. */
void sc_store_test_simulate_reboot(void)
{
    s_mutex=NULL;s_cat_count=0;memset(&s_live,0,sizeof(s_live));memset(s_readers,0,sizeof(s_readers));
    s_upload_file=NULL;s_upload_id=0;s_cleanup_id=0;s_cleanup_enumerating=false;
    s_cleanup_recovery_failed=false;s_empty_interrupted=false;s_ready=false;s_mounted=false;
}
#endif

static void lock(void) { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGiveRecursive(s_mutex); }

static void path_dat(char *buf, size_t cap, uint32_t id) { snprintf(buf, cap, REC_DIR "/%lu.dat", (unsigned long)id); }
static void path_idx(char *buf, size_t cap, uint32_t id) { snprintf(buf, cap, REC_DIR "/%lu.idx", (unsigned long)id); }

static void hdr_encode(uint8_t h[HDR_SIZE], uint32_t id, uint32_t start, uint32_t end, uint32_t frames,
                       const uint8_t key[32], const uint8_t nonce[16])
{
    memset(h, 0, HDR_SIZE);
    sc_wr32(h + 0, HDR_MAGIC);
    sc_wr32(h + 4, HDR_VERSION);
    sc_wr32(h + 8, id);
    sc_wr32(h + 12, start);
    sc_wr32(h + 16, end);
    sc_wr32(h + 20, frames);
    memcpy(h + 24, key, 32);
    memcpy(h + 56, nonce, 16);
}

static bool hdr_read_file(FILE *f, uint8_t h[HDR_SIZE])
{
    return fseek(f, 0, SEEK_SET) == 0 && fread(h, 1, HDR_SIZE, f) == HDR_SIZE &&
           sc_rd32(h) == HDR_MAGIC && sc_rd32(h + 4) == HDR_VERSION;
}

static bool hdr_read(const char *path, uint8_t h[HDR_SIZE])
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = hdr_read_file(f, h);
    fclose(f);
    return ok;
}

static void cat_sort(void)
{
    // 插入排序:条目少,且几乎总是有序。新的在前。
    for (size_t i = 1; i < s_cat_count; i++) {
        cat_entry_t e = s_cat[i];
        size_t j = i;
        while (j > 0 && s_cat[j - 1].file_id < e.file_id) {
            s_cat[j] = s_cat[j - 1];
            j--;
        }
        s_cat[j] = e;
    }
}

static cat_entry_t *cat_find(uint32_t id)
{
    for (size_t i = 0; i < s_cat_count; i++) {
        if (s_cat[i].file_id == id) return &s_cat[i];
    }
    return NULL;
}

static void cat_remove(uint32_t id)
{
    for (size_t i = 0; i < s_cat_count; i++) {
        if (s_cat[i].file_id == id) {
            memmove(&s_cat[i], &s_cat[i + 1], (s_cat_count - i - 1) * sizeof(s_cat[0]));
            s_cat_count--;
            return;
        }
    }
}

static void remove_files(uint32_t id)
{
    char p[48];
    path_dat(p, sizeof(p), id);
    unlink(p);
    path_idx(p, sizeof(p), id);
    unlink(p);
}

/* An open header alone is not proof of a recoverable meeting. Only expose it
 * after validating the persistent identity and scanning complete frames. */
static bool inspect_interrupted(const char *path, uint32_t id, uint32_t *frames, bool *sealed)
{
    nvs_handle_t handle;
    meeting_record record;meeting_seal seal;
    if (nvs_open("meeting_rec", NVS_READONLY, &handle) != ESP_OK) return false;
    meeting_record_status status = meeting_record_nvs_read(handle, id, &record);
    meeting_seal_status seal_status=meeting_seal_nvs_read(handle,id,&seal);
    nvs_close(handle);
    if (status != MEETING_RECORD_OK||seal_status==MEETING_SEAL_ERROR||!sealed) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    meeting_scr1_scan_result scan;
    bool ok = meeting_scr1_scan_file(file, &scan);
    if (fclose(file) != 0) ok = false;
    if(ok&&seal_status==MEETING_SEAL_OK){
        struct stat st;
        ok=record.end_intent>=1&&record.end_intent<=4&&seal.reason==record.end_intent-1&&
           !memcmp(seal.meeting_id,record.meeting_id,16)&&stat(path,&st)==0&&st.st_size>=0&&
           (uint64_t)st.st_size==seal.bytes&&!scan.incomplete_tail&&scan.valid_end==seal.bytes&&scan.frames==seal.frames;
    }
    if (ok) {*frames = scan.frames;*sealed=seal_status==MEETING_SEAL_OK;}
    return ok;
}

/* Keep a verified header-only interrupted recording visible, without ever
 * projecting, uploading, or deleting it. Its durable identity and exact file
 * length distinguish a zero-frame interruption from a damaged audio tail. */
static bool inspect_empty_interrupted(const char *path,uint32_t id)
{
    struct stat st;
    if(stat(path,&st)!=0||st.st_size!=HDR_SIZE)return false;
    nvs_handle_t handle;meeting_record record;
    if(nvs_open("meeting_rec",NVS_READONLY,&handle)!=ESP_OK)return false;
    meeting_record_status status=meeting_record_nvs_read(handle,id,&record);
    nvs_close(handle);
    return status==MEETING_RECORD_OK&&record.end_intent==0;
}

static bool scan_catalog(void)
{
    s_cat_count = 0;
    s_empty_interrupted = false;
    DIR *d = opendir(REC_DIR);
    if (!d) return false;
    bool healthy = true;
    struct dirent *de;
    uint8_t h[HDR_SIZE];
    char p[300];
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (n < 5 || strcmp(de->d_name + n - 4, ".dat") != 0) continue;
        uint32_t id = (uint32_t)strtoul(de->d_name, NULL, 10);
        snprintf(p, sizeof(p), REC_DIR "/%s", de->d_name);
        if (id == 0 || !hdr_read(p, h) || sc_rd32(h + 8) != id) {
            ESP_LOGE(TAG, "录音文件异常，已保留 %s", de->d_name);
            healthy = false;
            continue;
        }
        uint32_t frames = sc_rd32(h + 20);
        bool sealed=false;
        if (frames == FRAMES_OPEN && inspect_interrupted(p, id, &frames, &sealed)) {
            if(sealed)ESP_LOGI(TAG,"已验证保存录音，原文件保持不变 %s",de->d_name);
            else ESP_LOGW(TAG, "发现中断录音，原文件保持不变 %s", de->d_name);
        } else if (frames == FRAMES_OPEN && inspect_empty_interrupted(p,id)) {
            frames=0;s_empty_interrupted=true;
            ESP_LOGW(TAG,"Empty interrupted recording retained for review: %lu",(unsigned long)id);
        } else if (frames == FRAMES_OPEN || frames == 0) {
            ESP_LOGE(TAG, "录音未完成或为空，需人工恢复 %s", de->d_name);
            healthy = false;
            continue;
        }
        if (s_cat_count >= SC_STORE_MAX_FILES) {
            ESP_LOGE(TAG, "录音条数超过上限 %u，已保留 %lu", SC_STORE_MAX_FILES, (unsigned long)id);
            healthy = false;
            continue;
        }
        s_cat[s_cat_count++] = (cat_entry_t){
            .file_id = id,
            .start_time = sc_rd32(h + 12),
            .end_time = sc_rd32(h + 16),
            .frames = frames,
        };
    }
    closedir(d);
    cat_sort();
    return healthy;
}

static bool boot_cleanup_cancelled(void *ctx){(void)ctx;return false;}
static bool retire_completed_after_mount(void);

esp_err_t sc_store_init(void)
{
    if (s_ready) return ESP_OK;
    if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    esp_vfs_littlefs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = PART_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS 挂载失败: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    mkdir(REC_DIR, 0775);
    lock();
    bool healthy = scan_catalog();
    s_ready = healthy;
    unlock();
    if (!healthy) {
        ESP_LOGE(TAG, "录音存储需要恢复，已禁止继续使用");
        return ESP_FAIL;
    }
    s_cleanup_recovery_failed=!retire_completed_after_mount();
    if(!s_cleanup_recovery_failed)
        s_cleanup_recovery_failed=sc_store_resume_cleanups(boot_cleanup_cancelled,NULL)!=MEETING_CLEANUP_DONE;
    if(s_cleanup_recovery_failed)ESP_LOGE(TAG,"已确认录音清理恢复失败，保留剩余文件");
    uint32_t total, free_kb;
    sc_store_usage(&total, &free_kb);
    ESP_LOGI(TAG, "存储就绪:%u 条录音,%lu/%lu KB 可用", (unsigned)s_cat_count,
             (unsigned long)free_kb, (unsigned long)total);
    return ESP_OK;
}

bool sc_store_inspect_recovery(uint32_t file_id, meeting_scr1_scan_result *out)
{
    if (!s_mounted || !s_mutex || !file_id || !out) return false;
    lock();
    if (s_live.f && s_live.file_id == file_id) { unlock(); return false; }
    char path[48];
    path_dat(path, sizeof(path), file_id);
    FILE *file = fopen(path, "rb");
    bool ok = file && meeting_scr1_scan_file(file, out);
    if (file && fclose(file) != 0) ok = false;
    unlock();
    return ok;
}

esp_err_t sc_store_prepare_snapshot(uint32_t file_id, meeting_snapshot *out, meeting_cancel_fn cancelled, void *ctx)
{
    if (!s_mounted || !s_mutex || !file_id || !out) return ESP_ERR_INVALID_STATE;
    lock();
    FILE *file = s_upload_id == file_id && !s_live.f ? s_upload_file : NULL;
    unlock();
    if (!file) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ESP_FAIL;
    nvs_handle_t handle = 0;
    bool opened = false;
    uint8_t header[HDR_SIZE];
    /* LittleFS may count the same blocks twice after closing a second reader. */
    if (!hdr_read_file(file, header) || sc_rd32(header + 8) != file_id) goto done;
    if (nvs_open("meeting_snap", NVS_READWRITE, &handle) != ESP_OK) goto done;
    opened = true;
    meeting_snapshot_nvs storage = {.handle=handle, .file_id=file_id};
    meeting_snapshot_io io;
    if (!meeting_snapshot_nvs_bind(&storage, &io)) goto done;
    nvs_handle_t record_nvs; meeting_record record;meeting_seal seal;
    if (nvs_open("meeting_rec", NVS_READONLY, &record_nvs) != ESP_OK) goto done;
    meeting_record_status record_status=meeting_record_nvs_read(record_nvs,file_id,&record);
    meeting_seal_status seal_status=meeting_seal_nvs_read(record_nvs,file_id,&seal);
    nvs_close(record_nvs);
    /* Older recordings may predate meeting identity metadata. Preserve them
     * for maintenance and report this separately from corruption or I/O. */
    if (record_status == MEETING_RECORD_MISSING) { result=ESP_ERR_NOT_FOUND; goto done; }
    if (record_status != MEETING_RECORD_OK || seal_status==MEETING_SEAL_ERROR) goto done;
    meeting_recovery_status recovery;
    if (sc_rd32(header + 20) == FRAMES_OPEN) {
        if(seal_status==MEETING_SEAL_OK){
            if(memcmp(seal.meeting_id,record.meeting_id,16)||record.end_intent<1||record.end_intent>4||seal.reason!=record.end_intent-1)goto done;
            recovery=meeting_sealed_prepare_cancel(file,&io,&seal,out,cancelled,ctx);
        }else recovery = meeting_recovery_prepare_cancel(file, &io, record.meeting_id, out,cancelled,ctx);
    } else {
        // An absent end intent cannot prove a complete meeting.
        if (record.end_intent < 1 || record.end_intent > 4) goto done;
        recovery = meeting_closed_prepare_cancel(file, &io, record.meeting_id, (uint8_t)(record.end_intent - 1), out,cancelled,ctx);
    }
    if (recovery == MEETING_RECOVERY_OK) result = ESP_OK;
done:
    if (opened) nvs_close(handle);
    return result;
}

/* A recovery error must never make a retired recording uploadable again.
 * Caller holds the store mutex; absence is the only permissive result. */
static bool retirement_allows_upload(uint32_t file_id)
{
    nvs_handle_t handle;
    esp_err_t opened=nvs_open("meeting_done",NVS_READONLY,&handle);
    if(opened==ESP_ERR_NVS_NOT_FOUND)return true;
    if(opened!=ESP_OK)return false;
    meeting_retired marker;
    int status=meeting_retirement_nvs_read(handle,file_id,&marker);
    nvs_close(handle);
    return status==1;
}

FILE *sc_store_upload_acquire(uint32_t file_id)
{
    if (!s_mounted || !s_mutex || !file_id) return NULL;
    lock();
    FILE *file = NULL;
    if (!s_live.f && !s_upload_file && !s_cleanup_id &&
        retirement_allows_upload(file_id) && meeting_deletion_store_upload_permitted(file_id)) {
        char path[48]; uint8_t header[HDR_SIZE];
        path_dat(path, sizeof(path), file_id);
        if (hdr_read(path, header) && sc_rd32(header + 8) == file_id) {
            file = fopen(path, "rb");
            if (file) { s_upload_file = file; s_upload_id = file_id; }
        }
    }
    unlock();
    return file;
}

meeting_delete_visibility sc_store_delete_visibility(uint32_t file_id,meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_ready||!cancelled||cancelled(ctx))return MEETING_DELETE_UNKNOWN;
    FILE *file=sc_store_upload_acquire(file_id);if(!file)return MEETING_DELETE_UNKNOWN;
    meeting_snapshot snapshot;meeting_delete_visibility result=MEETING_DELETE_UNKNOWN;
    bool has_time;int64_t recorded_at;uint8_t time_trust;
    if(sc_store_record_time(file_id,&has_time,&recorded_at,&time_trust)&&
       sc_store_prepare_snapshot(file_id,&snapshot,cancelled,ctx)==ESP_OK){
        sc_confirmation_status status=sc_store_check_confirmation(file,&snapshot,has_time,recorded_at,time_trust,cancelled,ctx);
        if(status==SC_CONFIRMATION_PRESENT)result=MEETING_DELETE_CONFIRMED;
        else if(status==SC_CONFIRMATION_ABSENT)result=MEETING_DELETE_UNCONFIRMED;
    }
    if(sc_store_upload_release(file)!=ESP_OK||cancelled(ctx))return MEETING_DELETE_UNKNOWN;
    return result;
}

static esp_err_t begin_delete_unchecked(uint32_t file_id)
{
    if(!s_ready||!s_mutex||!file_id)return ESP_ERR_INVALID_STATE;
    lock();esp_err_t result=ESP_FAIL;nvs_handle_t records;meeting_record record;
    if(s_live.f||(s_upload_file&&s_upload_id==file_id)||s_cleanup_id==file_id){result=ESP_ERR_INVALID_STATE;goto done;}
    cat_entry_t *catalog=cat_find(file_id);if(!catalog){result=ESP_ERR_NOT_FOUND;goto done;}
    esp_err_t record_open=nvs_open("meeting_rec",NVS_READONLY,&records);
    meeting_record_status record_status=MEETING_RECORD_MISSING;
    if(record_open==ESP_OK){record_status=meeting_record_nvs_read(records,file_id,&record);nvs_close(records);}
    else if(record_open!=ESP_ERR_NVS_NOT_FOUND)goto done;
    if(record_status!=MEETING_RECORD_OK&&record_status!=MEETING_RECORD_MISSING)goto done;
    bool empty=false;
    if(record_status==MEETING_RECORD_OK&&record.end_intent==0&&catalog->frames==0){
        char path[48];path_dat(path,sizeof(path),file_id);empty=inspect_empty_interrupted(path,file_id);
    }
    meeting_deletion_store store;meeting_deletion_io io;meeting_deletion current;unsigned slot;
    if(!meeting_deletion_store_open(&store,file_id,&io))goto done;
    meeting_deletion_status state=meeting_deletion_load(&io,&current,&slot);
    if(state==MEETING_DELETION_OK){
        if(current.file_id==file_id&&((current.legacy_local_only&&record_status==MEETING_RECORD_MISSING)||
           (!current.legacy_local_only&&record_status==MEETING_RECORD_OK&&!memcmp(current.meeting_id,record.meeting_id,16)&&
            (!current.empty_local_only||record.end_intent==0))))result=ESP_OK;
    }else if(state==MEETING_DELETION_EMPTY){
        meeting_deletion intent={.generation=1,.file_id=file_id};
        if(record_status==MEETING_RECORD_MISSING){intent.legacy_local_only=true;esp_fill_random(intent.meeting_id,sizeof(intent.meeting_id));}
        else {memcpy(intent.meeting_id,record.meeting_id,16);intent.empty_local_only=empty;}
        esp_fill_random(intent.operation_id,sizeof(intent.operation_id));
        if(meeting_deletion_commit(&io,&intent))result=ESP_OK;
    }
    meeting_deletion_store_close(&store);
done:
    unlock();return result;
}

esp_err_t sc_store_begin_delete_checked(uint32_t file_id,meeting_delete_visibility reviewed,
                                       meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_ready||!s_mutex||!file_id||!cancelled||cancelled(ctx)||(unsigned)reviewed>MEETING_DELETE_UNKNOWN)
        return ESP_ERR_INVALID_STATE;
    lock();meeting_deletion_store store;meeting_deletion_io io;meeting_deletion existing;unsigned slot;
    if(!meeting_deletion_store_open(&store,file_id,&io)){unlock();return ESP_FAIL;}
    meeting_deletion_status status=meeting_deletion_load(&io,&existing,&slot);meeting_deletion_store_close(&store);
    if(status==MEETING_DELETION_OK){esp_err_t result=begin_delete_unchecked(file_id);unlock();return result;}
    if(status!=MEETING_DELETION_EMPTY){unlock();return ESP_FAIL;}
    /* The recursive store mutex prevents upload acquisition or cleanup from
     * entering between this status check and the durable intent commit. */
    if(sc_store_delete_visibility(file_id,cancelled,ctx)!=reviewed||cancelled(ctx)){unlock();return ESP_ERR_INVALID_STATE;}
    esp_err_t result=begin_delete_unchecked(file_id);unlock();return result;
}

esp_err_t sc_store_prepare_delete_proof(uint32_t file_id,meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_ready||!s_mutex||!file_id||!cancelled||cancelled(ctx))return ESP_ERR_INVALID_STATE;
    FILE *file=NULL;lock();
    if(s_live.f||s_upload_file||s_cleanup_id||!cat_find(file_id)){unlock();return ESP_ERR_INVALID_STATE;}
    meeting_deletion_store deletion;meeting_deletion_io delete_io;meeting_deletion intent;unsigned slot;
    if(!meeting_deletion_store_open(&deletion,file_id,&delete_io)){unlock();return ESP_FAIL;}
    meeting_deletion_status status=meeting_deletion_load(&delete_io,&intent,&slot);
    meeting_deletion_store_close(&deletion);
    if(status!=MEETING_DELETION_OK||intent.file_id!=file_id||intent.local_done){unlock();return ESP_FAIL;}
    nvs_handle_t records;meeting_record record;
    if(nvs_open("meeting_rec",NVS_READONLY,&records)!=ESP_OK){unlock();return ESP_FAIL;}
    meeting_record_status rs=meeting_record_nvs_read(records,file_id,&record);nvs_close(records);
    if(rs!=MEETING_RECORD_OK||memcmp(record.meeting_id,intent.meeting_id,16)){unlock();return ESP_FAIL;}
    char path[48];path_dat(path,sizeof(path),file_id);file=fopen(path,"rb");
    if(file){s_upload_file=file;s_upload_id=file_id;}
    unlock();if(!file)return ESP_FAIL;
    meeting_snapshot snapshot;esp_err_t result=ESP_FAIL;
    if(sc_store_prepare_snapshot(file_id,&snapshot,cancelled,ctx)==ESP_OK&&!cancelled(ctx)){
        meeting_delete_proof_nvs proof;meeting_confirmation_io proof_io;
        if(meeting_delete_proof_nvs_open(&proof,file_id,&proof_io)){
            if(meeting_delete_proof_prepare(file_id,file,&snapshot,record.recorded_at_present,
                                            record.recorded_at,record.time_trust,&proof_io,cancelled,ctx))result=ESP_OK;
            meeting_delete_proof_nvs_close(&proof);
        }
    }
    if(sc_store_upload_release(file)!=ESP_OK)return ESP_FAIL;
    return result;
}

static bool delete_file_verified(const char *path)
{
    struct stat st;
    if(stat(path,&st)!=0)return errno==ENOENT;
    if(!S_ISREG(st.st_mode)||unlink(path)!=0)return false;
    return stat(path,&st)!=0&&errno==ENOENT;
}
static bool cleanup_check_space(void *ctx);

esp_err_t sc_store_finish_delete_local(uint32_t file_id,meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_mounted||!s_mutex||!file_id||!cancelled||cancelled(ctx))return ESP_ERR_INVALID_STATE;
    lock();esp_err_t result=ESP_FAIL;meeting_deletion_store deletion;meeting_deletion_io delete_io;
    meeting_deletion intent;unsigned slot;meeting_snapshot snapshot;meeting_manifest expected;
    if(s_live.f||s_upload_file||s_cleanup_id){result=ESP_ERR_INVALID_STATE;goto done;}
    if(!meeting_deletion_store_open(&deletion,file_id,&delete_io))goto done;
    if(meeting_deletion_load(&delete_io,&intent,&slot)!=MEETING_DELETION_OK||intent.file_id!=file_id)goto close_deletion;
    if(intent.legacy_local_only||intent.empty_local_only){
        /* A durable local-only intent precedes every unlink. Never infer a
         * server identity from an old file or erase damaged record metadata. */
        nvs_handle_t records;meeting_record record;esp_err_t opened=nvs_open("meeting_rec",NVS_READONLY,&records);
        if(opened==ESP_OK){meeting_record_status status=meeting_record_nvs_read(records,file_id,&record);nvs_close(records);
            if(intent.legacy_local_only&&status!=MEETING_RECORD_MISSING)goto close_deletion;
            if(intent.empty_local_only&&(status!=MEETING_RECORD_OK||record.end_intent!=0||memcmp(record.meeting_id,intent.meeting_id,16)))goto close_deletion;}
        else if(opened!=ESP_ERR_NVS_NOT_FOUND||intent.empty_local_only)goto close_deletion;
        char audio[48],index[48];uint8_t header[HDR_SIZE];struct stat st;
        path_dat(audio,sizeof(audio),file_id);path_idx(index,sizeof(index),file_id);
        if(stat(audio,&st)==0){if(!S_ISREG(st.st_mode)||!hdr_read(audio,header)||sc_rd32(header+8)!=file_id||
            (intent.empty_local_only&&(st.st_size!=HDR_SIZE||sc_rd32(header+20)!=FRAMES_OPEN)))goto close_deletion;}
        else if(errno!=ENOENT)goto close_deletion;
        for(int i=0;i<SC_STORE_READERS;i++)if(s_readers[i].used&&s_readers[i].file_id==file_id&&s_readers[i].f){
            int closed=fclose(s_readers[i].f);s_readers[i].f=NULL;if(closed!=0)goto close_deletion;
        }
        if(cancelled(ctx)||!delete_file_verified(audio)||cancelled(ctx)||!delete_file_verified(index)||
           cancelled(ctx)||!cleanup_check_space(NULL))goto close_deletion;
        if(!intent.local_done){if(intent.generation==UINT64_MAX)goto close_deletion;intent.generation++;intent.local_done=true;
            if(!meeting_deletion_commit(&delete_io,&intent))goto close_deletion;}
        cat_remove(file_id);
        if(intent.empty_local_only){s_empty_interrupted=false;for(size_t i=0;i<s_cat_count;i++)if(s_cat[i].frames==0){s_empty_interrupted=true;break;}}
        result=ESP_OK;goto close_deletion;
    }
    nvs_handle_t snapshots;
    if(nvs_open("meeting_snap",NVS_READONLY,&snapshots)!=ESP_OK)goto close_deletion;
    meeting_snapshot_nvs snap_store={.handle=snapshots,.file_id=file_id};meeting_snapshot_io snap_io;
    meeting_snapshot_status ss=meeting_snapshot_nvs_bind(&snap_store,&snap_io)?meeting_snapshot_load(&snap_io,&snapshot,&slot):MEETING_SNAPSHOT_ERROR;
    nvs_close(snapshots);
    if(ss!=MEETING_SNAPSHOT_OK||memcmp(snapshot.meeting_id,intent.meeting_id,16))goto close_deletion;
    meeting_delete_proof_nvs proof;meeting_confirmation_io proof_io;
    if(!meeting_delete_proof_nvs_open(&proof,file_id,&proof_io))goto close_deletion;
    bool verified=meeting_delete_proof_load(file_id,&snapshot,&proof_io,&expected);meeting_delete_proof_nvs_close(&proof);
    if(!verified)goto close_deletion;
    char audio[48],index[48];path_dat(audio,sizeof(audio),file_id);path_idx(index,sizeof(index),file_id);
    FILE *source=fopen(audio,"rb");
    if(source){
        meeting_manifest actual;
        bool matches=meeting_upload_manifest_cancel(source,&snapshot,expected.recorded_at_present,expected.recorded_at,expected.time_trust,&actual,cancelled,ctx)&&
                     meeting_receipt_matches(&expected,&actual,true);
        if(fclose(source)!=0)matches=false;
        if(!matches)goto close_deletion;
    }else if(errno!=ENOENT)goto close_deletion;
    for(int i=0;i<SC_STORE_READERS;i++){
        if(s_readers[i].used&&s_readers[i].file_id==file_id&&s_readers[i].f){
            int closed=fclose(s_readers[i].f);s_readers[i].f=NULL;
            if(closed!=0)goto close_deletion;
        }
    }
    if(cancelled(ctx)||!delete_file_verified(audio)||cancelled(ctx)||!delete_file_verified(index)||
       cancelled(ctx)||!cleanup_check_space(NULL))goto close_deletion;
    if(!intent.local_done){
        if(intent.generation==UINT64_MAX)goto close_deletion;
        intent.generation++;intent.local_done=true;
        if(!meeting_deletion_commit(&delete_io,&intent))goto close_deletion;
    }
    cat_remove(file_id);result=ESP_OK;
close_deletion:
    meeting_deletion_store_close(&deletion);
done:
    unlock();return result;
}

esp_err_t sc_store_cancel_delete_remote(uint32_t file_id,const meeting_upload_transport *transport)
{
    if(!s_mounted||!s_mutex||!file_id||!transport||!transport->request||!transport->cancelled)return ESP_ERR_INVALID_ARG;
    lock();esp_err_t result=ESP_FAIL;meeting_deletion_store deletion;meeting_deletion_io delete_io;meeting_deletion intent;unsigned slot;
    meeting_snapshot snapshot;meeting_manifest expected;
    if(s_live.f||s_upload_file||s_cleanup_id){unlock();return ESP_ERR_INVALID_STATE;}
    if(!meeting_deletion_store_open(&deletion,file_id,&delete_io)){unlock();return ESP_FAIL;}
    meeting_deletion_status ds=meeting_deletion_load(&delete_io,&intent,&slot);meeting_deletion_store_close(&deletion);
    if(ds!=MEETING_DELETION_OK||intent.file_id!=file_id||!intent.local_done){unlock();return ESP_FAIL;}
    if(intent.legacy_local_only||intent.empty_local_only){unlock();return intent.local_done?ESP_OK:ESP_ERR_INVALID_STATE;}
    if(intent.remote!=MEETING_DELETE_REMOTE_PENDING){unlock();return ESP_OK;}
    nvs_handle_t snapshots;if(nvs_open("meeting_snap",NVS_READONLY,&snapshots)!=ESP_OK){unlock();return ESP_FAIL;}
    meeting_snapshot_nvs snap_store={.handle=snapshots,.file_id=file_id};meeting_snapshot_io snap_io;
    meeting_snapshot_status ss=meeting_snapshot_nvs_bind(&snap_store,&snap_io)?meeting_snapshot_load(&snap_io,&snapshot,&slot):MEETING_SNAPSHOT_ERROR;
    nvs_close(snapshots);
    if(ss!=MEETING_SNAPSHOT_OK||memcmp(snapshot.meeting_id,intent.meeting_id,16)){unlock();return ESP_FAIL;}
    meeting_delete_proof_nvs proof;meeting_confirmation_io proof_io;
    if(!meeting_delete_proof_nvs_open(&proof,file_id,&proof_io)){unlock();return ESP_FAIL;}
    bool ok=meeting_delete_proof_load(file_id,&snapshot,&proof_io,&expected);meeting_delete_proof_nvs_close(&proof);
    if(!ok){unlock();return ESP_FAIL;}
    s_cleanup_id=file_id;unlock();
    int remote=meeting_cancel_request(intent.meeting_id,&expected,transport);
    lock();
    if(remote==1||remote==2){
        if(meeting_deletion_store_open(&deletion,file_id,&delete_io)){
            meeting_deletion now;ds=meeting_deletion_load(&delete_io,&now,&slot);
            if(ds==MEETING_DELETION_OK&&now.file_id==intent.file_id&&now.generation==intent.generation&&now.local_done&&
               now.remote==MEETING_DELETE_REMOTE_PENDING&&!memcmp(now.meeting_id,intent.meeting_id,16)&&
               !memcmp(now.operation_id,intent.operation_id,16)&&now.generation<UINT64_MAX){
                now.generation++;now.remote=(meeting_delete_remote)remote;
                if(meeting_deletion_commit(&delete_io,&now))result=ESP_OK;
            }
            meeting_deletion_store_close(&deletion);
        }
    }else result=ESP_ERR_INVALID_STATE; /* offline, cancelled or unknown: retry */
    s_cleanup_id=0;unlock();return result;
}

esp_err_t sc_store_confirm_upload(FILE *file, const meeting_snapshot *snapshot, const meeting_manifest *manifest)
{
    if (!file || !snapshot || !manifest || !s_mutex) return ESP_ERR_INVALID_ARG;
    lock();
    esp_err_t result = ESP_FAIL;
    nvs_handle_t handle;
    if (file != s_upload_file || !s_upload_id) goto done;
    if (nvs_open("meeting_ack", NVS_READWRITE, &handle) != ESP_OK) goto done;
    meeting_confirmation_nvs storage={.handle=handle,.file_id=s_upload_id};
    meeting_confirmation_io io;
    meeting_confirmation confirmation={.file_id=s_upload_id,.manifest=*manifest};
    memcpy(confirmation.source_sha256,snapshot->source_sha256,32);
    if (meeting_confirmation_matches_snapshot(&confirmation,snapshot) &&
        meeting_confirmation_nvs_bind(&storage,&io) && meeting_confirmation_commit(&io,&confirmation)) result=ESP_OK;
    nvs_close(handle);
done:
    unlock();
    return result;
}

sc_confirmation_status sc_store_check_confirmation(FILE *file, const meeting_snapshot *snapshot,
                                                     bool has_time, int64_t recorded_at, uint8_t time_trust,
                                                     meeting_cancel_fn cancelled, void *ctx)
{
    if (!file || !snapshot || !s_mutex) return SC_CONFIRMATION_ERROR;
    lock();
    uint32_t id = file == s_upload_file ? s_upload_id : 0;
    unlock();
    if (!id) return SC_CONFIRMATION_ERROR;
    nvs_handle_t handle;
    if (nvs_open("meeting_ack", NVS_READWRITE, &handle) != ESP_OK) return SC_CONFIRMATION_ERROR;
    meeting_confirmation_nvs storage={.handle=handle,.file_id=id};
    meeting_confirmation_io io;
    meeting_confirmation confirmation;
    sc_confirmation_status result=SC_CONFIRMATION_ERROR;
    if (!meeting_confirmation_nvs_bind(&storage,&io)) goto done;
    meeting_confirmation_status status=meeting_confirmation_load(&io,&confirmation);
    if (status==MEETING_CONFIRMATION_EMPTY) { result=SC_CONFIRMATION_ABSENT; goto done; }
    if (status!=MEETING_CONFIRMATION_OK || confirmation.file_id!=id ||
        !meeting_confirmation_matches_snapshot(&confirmation,snapshot)) goto done;
    meeting_manifest actual;
    if (!meeting_upload_manifest_cancel(file,snapshot,has_time,recorded_at,time_trust,&actual,cancelled,ctx) ||
        !meeting_receipt_matches(&confirmation.manifest,&actual,true) ||
        (cancelled && cancelled(ctx)) ||
        !meeting_confirmation_commit(&io,&confirmation)) goto done;
    result=SC_CONFIRMATION_PRESENT;
done:
    nvs_close(handle);
    return result;
}

esp_err_t sc_store_upload_release(FILE *file)
{
    if (!s_mutex || !file) return ESP_ERR_INVALID_ARG;
    lock();
    if (file != s_upload_file) { unlock(); return ESP_ERR_INVALID_ARG; }
    int rc = fclose(file);
    s_upload_file = NULL;
    s_upload_id = 0;
    unlock();
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

size_t sc_store_list(sc_file_entry_t *out, size_t cap)
{
    return sc_store_list_range(0,out,cap);
}
size_t sc_store_list_range(size_t start,sc_file_entry_t *out,size_t cap)
{
    if (!s_ready||(!out&&cap)) return 0;
    lock();
    size_t remaining=start<s_cat_count?s_cat_count-start:0;
    size_t n = remaining < cap ? remaining : cap;
    for (size_t i = 0; i < n; i++) {
        const cat_entry_t *e = &s_cat[start+i];
        uint32_t frames = e->frames;
        uint32_t end = e->end_time;
        if (s_live.f && e->file_id == s_live.file_id) {
            frames = s_live.frames;
            end = s_live.start_time + frames / FRAMES_PER_S;
        }
        out[i] = (sc_file_entry_t){ .file_id = e->file_id, .end_time = end, .size = frames * SC_UNIT_DATA };
    }
    unlock();
    return n;
}

size_t sc_store_count(void)
{
    if(!s_mutex)return 0;
    lock();size_t count=s_cat_count;unlock();return count;
}

bool sc_store_get(uint32_t file_id, sc_store_info_t *out)
{
    if (!s_ready) return false;
    lock();
    bool ok = false;
    if (s_live.f && file_id == s_live.file_id) {
        *out = (sc_store_info_t){
            .file_id = file_id,
            .start_time = s_live.start_time,
            .end_time = s_live.start_time + s_live.frames / FRAMES_PER_S,
            .frames = s_live.frames,
            .live = true,
        };
        memcpy(out->file_key, s_live.file_key, 32);
        memcpy(out->nonce, s_live.nonce, 16);
        ok = true;
    } else if (cat_find(file_id)) {
        char p[48];
        uint8_t h[HDR_SIZE];
        path_dat(p, sizeof(p), file_id);
        if (hdr_read(p, h)) {
            *out = (sc_store_info_t){
                .file_id = file_id,
                .start_time = sc_rd32(h + 12),
                .end_time = sc_rd32(h + 16),
                .frames = sc_rd32(h + 20),
                .live = false,
            };
            memcpy(out->file_key, h + 24, 32);
            memcpy(out->nonce, h + 56, 16);
            ok = out->frames != FRAMES_OPEN;
        }
    }
    unlock();
    return ok;
}

void sc_store_usage(uint32_t *total_kb, uint32_t *free_kb)
{
    size_t total = 0, used = 0;
    esp_err_t err=s_ready?esp_littlefs_info(PART_LABEL, &total, &used):ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        ESP_LOGE(TAG,"Storage usage unavailable: ready=%d error=%s",s_ready,esp_err_to_name(err));
        *total_kb = 0;
        *free_kb = 0;
        return;
    }
    *total_kb = (uint32_t)(total / 1024u);
    *free_kb = (uint32_t)((total > used ? total - used : 0) / 1024u);
}

#if CONFIG_MEETING_SUPERVISED_TEST_NEARFULL
#define TEST_FIXTURE BASE_PATH "/.meeting-nearfull-5284"
static const char test_fixture_marker[]="MEETING-NEARFULL-TEST-5284\n";
bool sc_store_test_nearfull_cleanup(void)
{
    if(!s_ready||sc_store_count()!=0)return false;
    FILE *f=fopen(TEST_FIXTURE,"rb");if(!f)return false;
    char marker[sizeof(test_fixture_marker)-1];
    bool ours=fread(marker,1,sizeof(marker),f)==sizeof(marker)&&
              !memcmp(marker,test_fixture_marker,sizeof(marker));
    fclose(f);if(!ours||unlink(TEST_FIXTURE)!=0)return false;
    uint32_t total,free_kb;sc_store_usage(&total,&free_kb);
    ESP_LOGI(TAG,"TEST near-full fixture removed: free=%lu KB",(unsigned long)free_kb);
    return total>0;
}
bool sc_store_test_nearfull_prepare(void)
{
    uint32_t total=0,free_kb=0;sc_store_usage(&total,&free_kb);
    if(!s_ready||sc_store_count()!=0||free_kb<1024)return false;
    int fd=open(TEST_FIXTURE,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return false;
    FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(TEST_FIXTURE);return false;}
    bool ok=fwrite(test_fixture_marker,1,sizeof(test_fixture_marker)-1,f)==sizeof(test_fixture_marker)-1;
    static const uint8_t zeros[1024]={0};
    while(ok&&free_kb>384){
        unsigned blocks=free_kb>600?32:8;
        for(unsigned i=0;i<blocks&&ok;i++)ok=fwrite(zeros,1,sizeof(zeros),f)==sizeof(zeros);
        if(ok)ok=fflush(f)==0&&fsync(fileno(f))==0;
        if(ok){sc_store_usage(&total,&free_kb);ok=total>0;}
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if(fclose(f)!=0)ok=false;
    if(!ok||free_kb<160){(void)sc_store_test_nearfull_cleanup();return false;}
    ESP_LOGI(TAG,"TEST near-full fixture ready: free=%lu KB",(unsigned long)free_kb);
    return true;
}
#endif

// ---------------------------------------------------------------- 录制

static bool live_sync(void)
{
    if (!s_live.f || s_live.failed) return false;
    bool ok = fflush(s_live.f) == 0 && fsync(fileno(s_live.f)) == 0;
    if (s_live.idx && (fflush(s_live.idx) != 0 || fsync(fileno(s_live.idx)) != 0)) ok = false;
    if (!ok) { s_live.failed = true; return false; }
    s_live.synced = s_live.frames;
    return true;
}

esp_err_t sc_store_sync(void)
{
    lock();
    bool ok = live_sync();
    unlock();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t sc_store_begin(uint32_t file_id,uint32_t start_time)
{return sc_store_begin_timed(file_id,start_time,0);}

bool sc_store_record_time(uint32_t file_id,bool *present,int64_t *epoch,uint8_t *trust)
{
    if(!present||!epoch||!trust)return false;
    lock();nvs_handle_t handle;meeting_record record;bool ok=false;
    if(nvs_open("meeting_rec",NVS_READONLY,&handle)==ESP_OK){
        if(meeting_record_nvs_read(handle,file_id,&record)==MEETING_RECORD_OK){
            *present=record.recorded_at_present;*epoch=record.recorded_at;*trust=record.time_trust;ok=true;
        }
        nvs_close(handle);
    }
    unlock();return ok;
}

esp_err_t sc_store_begin_timed(uint32_t file_id, uint32_t start_time,int64_t trusted_epoch)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    lock();
    esp_err_t err = ESP_OK;
    if (s_live.f || s_upload_file || s_cleanup_id) {
        err = ESP_ERR_INVALID_STATE;
        goto out;
    }
    if (s_cat_count >= SC_STORE_MAX_FILES) {
        err = ESP_ERR_NO_MEM;
        goto out;
    }
    /* File IDs are reserved durably by the caller. Never silently change a
     * reserved ID: the new value would not be covered by the high-water mark. */
    if (!file_id || cat_find(file_id) || !meeting_deletion_store_upload_permitted(file_id)) { err=ESP_ERR_INVALID_STATE; goto out; }

    // A retired ID must never be reused even if the old record was compacted.
    nvs_handle_t retired_nvs;
    esp_err_t retired_open=nvs_open("meeting_done",NVS_READONLY,&retired_nvs);
    if(retired_open==ESP_OK){
        meeting_retired retired;int retired_status=meeting_retirement_nvs_read(retired_nvs,file_id,&retired);
        nvs_close(retired_nvs);
        if(retired_status!=1){err=ESP_ERR_INVALID_STATE;goto out;}
    }else if(retired_open!=ESP_ERR_NVS_NOT_FOUND){err=ESP_FAIL;goto out;}

    // A new identity is durable before any audio file is created.
    nvs_handle_t record_nvs;
    if (nvs_open("meeting_rec", NVS_READWRITE, &record_nvs) != ESP_OK) { err = ESP_FAIL; goto out; }
    meeting_record record = {.file_id=file_id,.recorded_at_present=trusted_epoch!=0,
                             .recorded_at=trusted_epoch,.time_trust=trusted_epoch?2:0}; meeting_record previous;
    if (meeting_record_nvs_read(record_nvs,file_id,&previous) != MEETING_RECORD_MISSING) {
        nvs_close(record_nvs); err = ESP_FAIL; goto out;
    }
    esp_fill_random(record.meeting_id,sizeof(record.meeting_id));
    record.meeting_id[6]=(record.meeting_id[6]&0x0f)|0x40;
    record.meeting_id[8]=(record.meeting_id[8]&0x3f)|0x80;
    bool identity_saved=meeting_record_nvs_create(record_nvs,&record);
    nvs_close(record_nvs);
    if (!identity_saved) { err=ESP_FAIL; goto out; }
    char p[48];
    memset(&s_live, 0, sizeof(s_live));
    sc_crypto_random(s_live.file_key, sizeof(s_live.file_key));
    sc_crypto_random(s_live.nonce, sizeof(s_live.nonce));
    s_live.file_id = file_id;
    s_live.start_time = start_time;
    s_live.pos = HDR_SIZE;

    path_dat(p, sizeof(p), file_id);
    s_live.f = meeting_create_recording_file(p);
    path_idx(p, sizeof(p), file_id);
    s_live.idx = s_live.f ? meeting_create_recording_file(p) : NULL;
    if (!s_live.f || !s_live.idx) {
        ESP_LOGE(TAG, "创建录音文件失败: errno=%d", errno);
        if (s_live.f) fclose(s_live.f);
        if (s_live.idx) fclose(s_live.idx);
        memset(&s_live, 0, sizeof(s_live));
        s_ready = false; /* preserve both files for inspection */
        err = ESP_FAIL;
        goto out;
    }
    uint8_t h[HDR_SIZE];
    hdr_encode(h, file_id, start_time, start_time, FRAMES_OPEN, s_live.file_key, s_live.nonce);
    if (fwrite(h, 1, HDR_SIZE, s_live.f) != HDR_SIZE || !live_sync()) {
        fclose(s_live.f);
        fclose(s_live.idx);
        memset(&s_live, 0, sizeof(s_live));
        s_ready = false;
        err = ESP_FAIL;
        goto out;
    }
    sc_ring_reset(&s_ring, 0);

    s_cat[s_cat_count++] = (cat_entry_t){ .file_id = file_id, .start_time = start_time,
                                          .end_time = start_time, .frames = 0 };
    cat_sort();
    ESP_LOGI(TAG, "开始录音文件 %lu", (unsigned long)file_id);
out:
    unlock();
    return err;
}

esp_err_t sc_store_append(const uint8_t *frame, uint8_t len)
{
    if (!frame || len == 0 || len > SC_UNIT_DATA) return ESP_ERR_INVALID_ARG;
    lock();
    esp_err_t err = ESP_OK;
    if (!s_live.f || s_live.failed) {
        err = ESP_ERR_INVALID_STATE;
        goto out;
    }
    if (s_live.frames >= SC_STORE_MAX_FRAMES) {
        err = ESP_ERR_INVALID_SIZE;
        goto out;
    }
    // 环形缓冲即将淘汰尚未同步的帧时先同步,保证"环外的帧一定能从文件读到"。
    if (sc_ring_first_after_push(&s_ring, len) > s_live.synced && !live_sync()) { err = ESP_FAIL; goto out; }

    if (s_live.frames % SC_STORE_IDX_STRIDE == 0) {
        uint8_t b[4];
        sc_wr32(b, s_live.pos);
        if (!s_live.idx || fwrite(b, 1, 4, s_live.idx) != 4) { s_live.failed = true; err = ESP_FAIL; goto out; }
    }
    if (fputc(len, s_live.f) == EOF || (len && fwrite(frame, 1, len, s_live.f) != len)) {
        s_live.failed = true;
        err = ESP_FAIL;
        goto out;
    }
    s_live.pos += 1u + len;
    s_live.frames++;
    sc_ring_push(&s_ring, frame, len);
    if (s_live.frames - s_live.synced >= SYNC_EVERY && !live_sync()) err = ESP_FAIL;
out:
    unlock();
    return err;
}

esp_err_t sc_store_end(uint32_t end_time, uint8_t end_intent)
{
    lock();
    if (!s_live.f) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t id = s_live.file_id;
    uint32_t frames = s_live.frames;
    uint32_t bytes = s_live.pos;
    meeting_record record = {0};
    const char *failed_stage = frames > 0 ? "sync" : "empty";
    bool ok = frames > 0 && live_sync();
    nvs_handle_t record_nvs;
    if (ok) {
        failed_stage = "end_intent_open";
        if (nvs_open("meeting_rec", NVS_READWRITE, &record_nvs) != ESP_OK) ok=false;
        else {
            failed_stage = "end_intent_write";
            ok=meeting_record_nvs_end_intent(record_nvs,id,end_intent);
            if(ok){failed_stage = "end_intent_read";ok=meeting_record_nvs_read(record_nvs,id,&record)==MEETING_RECORD_OK;}
            nvs_close(record_nvs);
        }
    }
    /* Rewriting the head of a multi-megabyte LittleFS file can require a
     * second copy. Keep the open header and publish an independent seal only
     * after the durable file and index are closed successfully. */
    if (fclose(s_live.f) != 0) { if(ok) failed_stage = "audio_close"; ok = false; }
    if (s_live.idx && fclose(s_live.idx) != 0) { if(ok) failed_stage = "index_close"; ok = false; }
    memset(&s_live, 0, sizeof(s_live));
    if(ok){
        char path[48];struct stat st;path_dat(path,sizeof(path),id);
        failed_stage = "file_size";
        ok=stat(path,&st)==0&&st.st_size>=0&&(uint64_t)st.st_size==bytes;
        if(ok){
            failed_stage = "seal_open";
            if(nvs_open("meeting_rec",NVS_READWRITE,&record_nvs)==ESP_OK){
                meeting_seal seal={.file_id=id,.frames=frames,.bytes=bytes,.reason=(uint8_t)(end_intent-1)};
                memcpy(seal.meeting_id,record.meeting_id,16);
                failed_stage = "seal_write";ok=meeting_seal_nvs_commit(record_nvs,&seal);nvs_close(record_nvs);
            }else ok=false;
        }
    }
    if (!ok) {
        s_ready = false;
        (void)failed_stage; /* Some host-test logging stubs discard arguments. */
        ESP_LOGE(TAG, "录音保存失败 stage=%s，保留文件并停止存储操作", failed_stage);
        unlock();
        return ESP_FAIL;
    }

    // 读者若正读这条录音,下一次读取会按文件重新打开(帧数已固定)。
    for (int i = 0; i < SC_STORE_READERS; i++) {
        if (s_readers[i].used && s_readers[i].file_id == id && s_readers[i].f) {
            fclose(s_readers[i].f);
            s_readers[i].f = NULL;
        }
    }
    cat_entry_t *e = cat_find(id);
    if (frames == 0) {
        cat_remove(id);
        remove_files(id);
    } else if (e) {
        e->frames = frames;
        e->end_time = end_time;
    }
    ESP_LOGI(TAG, "录音文件 %lu 结束:%lu 帧", (unsigned long)id, (unsigned long)frames);
    unlock();
    return ESP_OK;
}

uint32_t sc_store_live_id(void)
{
    return s_live.f ? s_live.file_id : 0;
}

uint32_t sc_store_live_frames(void)
{
    return s_live.f ? s_live.frames : 0;
}

// ---------------------------------------------------------------- 读取

sc_reader_t *sc_store_reader_open(uint32_t file_id)
{
    if (!s_ready) return NULL;
    lock();
    sc_reader_t *rd = NULL;
    if (cat_find(file_id) && s_cleanup_id != file_id) {
        for (int i = 0; i < SC_STORE_READERS; i++) {
            if (!s_readers[i].used) {
                rd = &s_readers[i];
                *rd = (sc_reader_t){ .used = true, .file_id = file_id };
                break;
            }
        }
    }
    unlock();
    return rd;
}

void sc_store_reader_close(sc_reader_t *rd)
{
    if (!rd) return;
    lock();
    if (rd->f) fclose(rd->f);
    *rd = (sc_reader_t){ 0 };
    unlock();
}

static uint32_t idx_lookup(uint32_t id, uint32_t block)
{
    char p[48];
    path_idx(p, sizeof(p), id);
    FILE *f = fopen(p, "rb");
    uint32_t pos = HDR_SIZE;
    if (f) {
        uint8_t b[4];
        if (fseek(f, (long)block * 4, SEEK_SET) == 0 && fread(b, 1, 4, f) == 4) pos = sc_rd32(b);
        fclose(f);
    }
    return pos;
}

static bool reader_skip_to(sc_reader_t *rd, uint32_t seq)
{
    while (rd->cur_seq < seq) {
        int len = fgetc(rd->f);
        if (len == EOF || len > (int)SC_UNIT_DATA) return false;
        if (len > 0 && fseek(rd->f, len, SEEK_CUR) != 0) return false;
        rd->cur_seq++;
    }
    return true;
}

int sc_store_reader_read(sc_reader_t *rd, uint32_t seq, uint8_t (*bufs)[SC_UNIT_DATA],
                         uint8_t *lens, int max)
{
    if (!rd || !rd->used || max <= 0) return -1;
    lock();
    int n = 0;
    bool live = s_live.f && s_live.file_id == rd->file_id;
    uint32_t total;
    if (live) {
        total = s_live.frames;
    } else {
        cat_entry_t *e = cat_find(rd->file_id);
        if (!e) { n = -1; goto out; }
        total = e->frames;
    }
    if (seq >= total) goto out;

    if (live && sc_ring_contains(&s_ring, seq)) {
        while (n < max && sc_ring_get(&s_ring, seq + (uint32_t)n, bufs[n], &lens[n])) n++;
        goto out;
    }

    // 文件路径:录制中的文件只能读到已同步部分;不够就重新打开以看到新数据。
    uint32_t limit = live ? s_live.synced : total;
    if (!rd->f || seq >= rd->visible) {
        if (rd->f) fclose(rd->f);
        char p[48];
        path_dat(p, sizeof(p), rd->file_id);
        rd->f = fopen(p, "rb");
        rd->visible = limit;
        rd->cur_seq = UINT32_MAX;
        if (!rd->f) { n = -1; goto out; }
    }
    if (rd->cur_seq == UINT32_MAX || rd->cur_seq > seq || seq - rd->cur_seq > SC_STORE_IDX_STRIDE) {
        // 新句柄、向后跳或跳得远:按索引重新定位,再顺序跳过块内剩余记录。
        uint32_t block = seq / SC_STORE_IDX_STRIDE;
        uint32_t pos = idx_lookup(rd->file_id, block);
        rd->cur_seq = pos == HDR_SIZE ? 0 : block * SC_STORE_IDX_STRIDE;
        if (fseek(rd->f, (long)pos, SEEK_SET) != 0) { n = -1; goto out; }
    }
    if (!reader_skip_to(rd, seq)) { n = -1; goto out; }
    uint32_t end = rd->visible < total ? rd->visible : total;
    while (n < max && rd->cur_seq < end) {
        int len = fgetc(rd->f);
        if (len == EOF || len > (int)SC_UNIT_DATA) { if (n == 0) n = -1; break; }
        if (len > 0 && fread(bufs[n], 1, (size_t)len, rd->f) != (size_t)len) { if (n == 0) n = -1; break; }
        lens[n] = (uint8_t)len;
        rd->cur_seq++;
        n++;
    }
out:
    unlock();
    return n;
}

// ---------------------------------------------------------------- 管理

typedef struct { uint32_t file_id; meeting_cancel_fn cancelled; void *ctx; } cleanup_context;
static bool cleanup_cancelled(void *ctx)
{
    cleanup_context *c=ctx;return c->cancelled(c->ctx);
}
static bool cleanup_readers_closed(void *ctx)
{
    cleanup_context *c=ctx;bool clear=true;lock();
    for(int i=0;i<SC_STORE_READERS;i++)if(s_readers[i].used&&s_readers[i].file_id==c->file_id)clear=false;
    unlock();return clear;
}
static bool cleanup_check_space(void *ctx)
{
    (void)ctx;size_t total=0,used=0;
    return esp_littlefs_info(PART_LABEL,&total,&used)==ESP_OK&&used<=total;
}
meeting_cleanup_result sc_store_cleanup_confirmed(uint32_t file_id,meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_mounted||!s_mutex||!file_id||!cancelled)return MEETING_CLEANUP_ERROR;
    lock();
    if(s_live.f||s_upload_file||s_cleanup_id){unlock();return MEETING_CLEANUP_PAUSED;}
    s_cleanup_id=file_id;unlock();
    meeting_cleanup_result result=MEETING_CLEANUP_ERROR;
    nvs_handle_t records=0,snapshots=0,receipts=0,intents=0;
    bool record_open=false,snapshot_open=false,receipt_open=false,intent_open=false;
    if(nvs_open("meeting_rec",NVS_READONLY,&records)!=ESP_OK)goto done;
    record_open=true;meeting_record record;
    if(meeting_record_nvs_read(records,file_id,&record)!=MEETING_RECORD_OK)goto done;
    if(nvs_open("meeting_snap",NVS_READONLY,&snapshots)!=ESP_OK)goto done;
    snapshot_open=true;meeting_snapshot_nvs snap_store={.handle=snapshots,.file_id=file_id};meeting_snapshot_io snapshot_io;
    meeting_snapshot snapshot;unsigned slot;
    if(!meeting_snapshot_nvs_bind(&snap_store,&snapshot_io)||
       meeting_snapshot_load(&snapshot_io,&snapshot,&slot)!=MEETING_SNAPSHOT_OK||memcmp(record.meeting_id,snapshot.meeting_id,16))goto done;
    if(nvs_open("meeting_ack",NVS_READWRITE,&receipts)!=ESP_OK)goto done;
    receipt_open=true;
    if(nvs_open("meeting_clean",NVS_READWRITE,&intents)!=ESP_OK)goto done;
    intent_open=true;
    meeting_confirmation_nvs ack_store={.handle=receipts,.file_id=file_id},intent_store={.handle=intents,.file_id=file_id};
    meeting_confirmation_io ack_io,intent_io;
    if(!meeting_confirmation_nvs_bind(&ack_store,&ack_io)||!meeting_confirmation_nvs_bind(&intent_store,&intent_io))goto done;
    char audio[48],index[48];path_dat(audio,sizeof(audio),file_id);path_idx(index,sizeof(index),file_id);
    cleanup_context c={.file_id=file_id,.cancelled=cancelled,.ctx=ctx};
    meeting_cleanup_files files={.audio_path=audio,.index_path=index,.cancelled=cleanup_cancelled,
        .close_readers=cleanup_readers_closed,.read_space=cleanup_check_space,.ctx=&c};
    meeting_cleanup_io io;
    if(meeting_cleanup_files_bind(&files,&io))result=meeting_cleanup_run(file_id,&snapshot,&ack_io,&intent_io,&io);
done:
    if(intent_open)nvs_close(intents);
    if(receipt_open)nvs_close(receipts);
    if(snapshot_open)nvs_close(snapshots);
    if(record_open)nvs_close(records);
    lock();if(result==MEETING_CLEANUP_DONE)cat_remove(file_id);s_cleanup_id=0;unlock();
    return result;
}

static bool cleanup_key_id(const char *key,char prefix,uint32_t *id)
{
    if(!key||!id||strlen(key)!=9||key[0]!=prefix)return false;
    uint32_t value=0;
    for(unsigned i=1;i<9;i++){
        char c=key[i];unsigned d;
        if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='a'&&c<='f')d=(unsigned)(c-'a'+10);else return false;
        value=(value<<4)|d;
    }
    if(!value)return false;
    *id=value;return true;
}

typedef struct {uint32_t id;nvs_handle_t records,snapshots,receipts,intents;} retirement_context;
static int retirement_files_state(uint32_t id)
{
    char audio[48],index[48];path_dat(audio,sizeof(audio),id);path_idx(index,sizeof(index),id);
    const char *paths[]={audio,index};bool present=false;
    for(unsigned i=0;i<2;i++){
        struct stat st;
        if(stat(paths[i],&st)==0){present=true;continue;}
        if(errno!=ENOENT)return -1;
    }
    return present?1:0;
}
static bool retirement_cancelled(void *ctx){(void)ctx;return false;}
static bool retirement_files_absent(void *ctx)
{
    retirement_context *c=ctx;return retirement_files_state(c->id)==0&&cleanup_check_space(NULL);
}
static bool retirement_proof(void *ctx,const meeting_retired *identity)
{
    retirement_context *c=ctx;meeting_record record;meeting_snapshot snapshot;unsigned slot;
    if(identity->file_id!=c->id||meeting_record_nvs_read(c->records,c->id,&record)!=MEETING_RECORD_OK||
       memcmp(record.meeting_id,identity->meeting_id,16))return false;
    meeting_snapshot_nvs snapshot_store={.handle=c->snapshots,.file_id=c->id};meeting_snapshot_io snapshot_io;
    if(!meeting_snapshot_nvs_bind(&snapshot_store,&snapshot_io)||
       meeting_snapshot_load(&snapshot_io,&snapshot,&slot)!=MEETING_SNAPSHOT_OK||memcmp(snapshot.meeting_id,identity->meeting_id,16))return false;
    meeting_confirmation_nvs ack={.handle=c->receipts,.file_id=c->id},intent={.handle=c->intents,.file_id=c->id};
    meeting_confirmation_io ack_io,intent_io;meeting_confirmation a,b;
    uint8_t encoded_a[MEETING_CONFIRMATION_SIZE],encoded_b[MEETING_CONFIRMATION_SIZE];
    if(!meeting_confirmation_nvs_bind(&ack,&ack_io)||!meeting_confirmation_nvs_bind(&intent,&intent_io)||
       meeting_confirmation_load(&ack_io,&a)!=MEETING_CONFIRMATION_OK||meeting_confirmation_load(&intent_io,&b)!=MEETING_CONFIRMATION_OK||
       a.file_id!=c->id||b.file_id!=c->id||!meeting_confirmation_matches_snapshot(&a,&snapshot)||
       !meeting_confirmation_encode(&a,encoded_a)||!meeting_confirmation_encode(&b,encoded_b)||memcmp(encoded_a,encoded_b,sizeof(encoded_a)))return false;
    return meeting_confirmation_commit(&ack_io,&a)&&meeting_confirmation_commit(&intent_io,&b);
}
static int retirement_next(const char *ns,char prefix,uint32_t last,uint32_t *next)
{
    nvs_iterator_t it=NULL;bool found=false;esp_err_t err=nvs_entry_find("nvs",ns,NVS_TYPE_ANY,&it);
    while(err==ESP_OK){
        nvs_entry_info_t info;uint32_t id;
        if(nvs_entry_info(it,&info)!=ESP_OK||info.type!=NVS_TYPE_BLOB||!cleanup_key_id(info.key,prefix,&id)){err=ESP_FAIL;break;}
        if(id>last&&(!found||id<*next)){*next=id;found=true;}
        err=nvs_entry_next(&it);
    }
    if(it)nvs_release_iterator(it);
    return err!=ESP_ERR_NVS_NOT_FOUND?-1:found?0:1;
}
static bool retire_one(uint32_t id,bool marker_exists)
{
    const char *names[]={"meeting_done","meeting_snap","meeting_ack","meeting_rec","meeting_clean"};
    nvs_handle_t handles[5]={0};unsigned opened=0;bool ok=false;
    for(;opened<5;opened++)if(nvs_open(names[opened],NVS_READWRITE,&handles[opened])!=ESP_OK)goto done;
    retirement_context ctx={.id=id,.snapshots=handles[1],.receipts=handles[2],.records=handles[3],.intents=handles[4]};
    meeting_retired identity={.file_id=id};
    if(marker_exists){if(meeting_retirement_nvs_read(handles[0],id,&identity)!=0)goto done;}
    else {meeting_record record;if(meeting_record_nvs_read(handles[3],id,&record)!=MEETING_RECORD_OK)goto done;memcpy(identity.meeting_id,record.meeting_id,16);}
    meeting_retirement_nvs state={.markers=handles[0],.snapshots=handles[1],.receipts=handles[2],.records=handles[3],.intents=handles[4],
        .file_id=id,.ctx=&ctx,.cancelled=retirement_cancelled,.verify_completed=retirement_proof,.files_absent=retirement_files_absent};
    meeting_retirement_io io;
    ok=meeting_retirement_nvs_bind(&state,&io)&&meeting_retirement_run(&identity,true,&io)==MEETING_RETIRE_DONE;
done:
    while(opened)nvs_close(handles[--opened]);
    return ok;
}
static bool retire_completed_after_mount(void)
{
    const char *names[]={"meeting_done","meeting_clean"};const char prefixes[]={'d','c'};
    for(unsigned pass=0;pass<2;pass++){
        uint32_t last=0;
        for(;;){
            uint32_t id=0;int result=retirement_next(names[pass],prefixes[pass],last,&id);
            if(result<0)return false;
            if(result>0)break;
            int files=retirement_files_state(id);
            if(files<0||(pass==0&&files!=0))return false;
            if(files==0&&!retire_one(id,pass==0))return false;
            last=id;
        }
    }
    return true;
}
meeting_cleanup_result sc_store_resume_cleanups(meeting_cancel_fn cancelled,void *ctx)
{
    if(!s_mounted||!s_mutex||!cancelled)return MEETING_CLEANUP_ERROR;
    lock();if(s_cleanup_enumerating){unlock();return MEETING_CLEANUP_PAUSED;}s_cleanup_enumerating=true;unlock();
    uint32_t last=0;meeting_cleanup_result result=MEETING_CLEANUP_DONE;
    for(;;){
        if(cancelled(ctx)){result=MEETING_CLEANUP_PAUSED;break;}
        nvs_iterator_t iterator=NULL;uint32_t next=0;bool found=false;
        esp_err_t err=nvs_entry_find("nvs","meeting_clean",NVS_TYPE_ANY,&iterator);
        while(err==ESP_OK){
            nvs_entry_info_t info;uint32_t id;
            if(nvs_entry_info(iterator,&info)!=ESP_OK||info.type!=NVS_TYPE_BLOB||!cleanup_key_id(info.key,'c',&id)){
                err=ESP_FAIL;break;
            }
            if(id>last&&(!found||id<next)){next=id;found=true;}
            err=nvs_entry_next(&iterator);
        }
        if(iterator)nvs_release_iterator(iterator);
        if(err!=ESP_ERR_NVS_NOT_FOUND){result=MEETING_CLEANUP_ERROR;break;}
        if(!found)break;
        result=sc_store_cleanup_confirmed(next,cancelled,ctx);
        if(result!=MEETING_CLEANUP_DONE)break;
        last=next;
    }
    lock();s_cleanup_enumerating=false;unlock();return result;
}
bool sc_store_cleanup_recovery_failed(void){return s_cleanup_recovery_failed;}

esp_err_t sc_store_delete(uint32_t file_id)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    lock();
    esp_err_t err = ESP_OK;
    if ((s_live.f && s_live.file_id == file_id) || (s_upload_file && s_upload_id == file_id) || s_cleanup_id == file_id) {
        err = ESP_ERR_INVALID_STATE;
    } else if (!cat_find(file_id)) {
        err = ESP_ERR_NOT_FOUND;
    } else {
        for (int i = 0; i < SC_STORE_READERS; i++) {
            if (s_readers[i].used && s_readers[i].file_id == file_id && s_readers[i].f) {
                fclose(s_readers[i].f);
                s_readers[i].f = NULL;
            }
        }
        remove_files(file_id);
        cat_remove(file_id);
        ESP_LOGI(TAG, "删除录音 %lu", (unsigned long)file_id);
    }
    unlock();
    return err;
}

esp_err_t sc_store_wipe(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    lock();
    if (s_upload_file || s_cleanup_id) { unlock(); return ESP_ERR_INVALID_STATE; }
    if (s_live.f) sc_store_end(s_live.start_time, 1);
    for (int i = 0; i < SC_STORE_READERS; i++) {
        if (s_readers[i].f) fclose(s_readers[i].f);
        s_readers[i] = (sc_reader_t){ 0 };
    }
    while (s_cat_count > 0) {
        remove_files(s_cat[s_cat_count - 1].file_id);
        s_cat_count--;
    }
    unlock();
    ESP_LOGW(TAG, "已清空全部录音");
    return ESP_OK;
}
