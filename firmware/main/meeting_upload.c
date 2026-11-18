#include "meeting_upload.h"
#include <mbedtls/sha256.h>
#include <stdio.h>

static void uuid_text(const uint8_t id[16], char out[37])
{
    static const char hex[]="0123456789abcdef";
    unsigned p=0;
    for(unsigned i=0;i<16;i++){
        if(i==4||i==6||i==8||i==10)out[p++]='-';
        out[p++]=hex[id[i]>>4];out[p++]=hex[id[i]&15];
    }
    out[p]='\0';
}
bool meeting_upload_manifest(FILE *file,const meeting_snapshot *snapshot,
                             bool has_time,int64_t recorded_at,uint8_t time_trust,
                             meeting_manifest *out)
{
    return meeting_upload_manifest_cancel(file,snapshot,has_time,recorded_at,time_trust,out,NULL,NULL);
}
bool meeting_upload_manifest_cancel(FILE *file,const meeting_snapshot *snapshot,
                             bool has_time,int64_t recorded_at,uint8_t time_trust,
                             meeting_manifest *out,meeting_cancel_fn cancelled,void *ctx)
{
    if(!file||!snapshot||!out||!meeting_snapshot_matches_file_cancel(file,snapshot,cancelled,ctx))return false;
    return meeting_upload_manifest_verified_cancel(file,snapshot,has_time,recorded_at,time_trust,out,cancelled,ctx);
}
bool meeting_upload_manifest_verified_cancel(FILE *file,const meeting_snapshot *snapshot,
                             bool has_time,int64_t recorded_at,uint8_t time_trust,
                             meeting_manifest *out,meeting_cancel_fn cancelled,void *ctx)
{
    if(!file||!snapshot||!out || time_trust>2 || (!has_time&&time_trust==2) ||
       (has_time&&(recorded_at<946684800||recorded_at>4102444800LL)))return false;
    meeting_scr1_scan_result scan={.valid_end=snapshot->valid_end,.frames=snapshot->frames};
    meeting_manifest result={0};uuid_text(snapshot->meeting_id,result.id);
    result.count=(uint32_t)((scan.valid_end+MEETING_UPLOAD_CHUNK_SIZE-1)/MEETING_UPLOAD_CHUNK_SIZE);
    result.bytes=scan.valid_end;result.reason=snapshot->reason;result.recorded_at_present=has_time;
    result.recorded_at=has_time?recorded_at:0;result.time_trust=time_trust;
    if(!result.count||result.count>8192 || !meeting_projection_digest_cancel(file,&scan,0,scan.valid_end,result.digest,cancelled,ctx))return false;
    *out=result;return true;
}
bool meeting_upload_chunk(FILE *file,const meeting_snapshot *snapshot,uint32_t seq,
                          uint8_t *buffer,size_t capacity,size_t *length,uint8_t digest[32])
{
    if(!file||!snapshot||!buffer||!length||!digest)return false;
    uint64_t offset=(uint64_t)seq*MEETING_UPLOAD_CHUNK_SIZE;
    if(offset>=snapshot->valid_end)return false;
    uint64_t remain=snapshot->valid_end-offset;size_t n=remain>MEETING_UPLOAD_CHUNK_SIZE?MEETING_UPLOAD_CHUNK_SIZE:(size_t)remain;
    if(capacity<n)return false;
    meeting_scr1_scan_result scan={.valid_end=snapshot->valid_end,.frames=snapshot->frames};
    /* The projected bytes are already in buffer. Hash those exact bytes
     * instead of re-reading the file: the transmitted body and its digest
     * then necessarily describe the same read. */
    if(!meeting_scr1_read_projected(file,&scan,offset,buffer,n))return false;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    bool hashed=mbedtls_sha256_starts(&hash,0)==0&&mbedtls_sha256_update(&hash,buffer,n)==0&&
                mbedtls_sha256_finish(&hash,digest)==0;
    mbedtls_sha256_free(&hash);
    if(!hashed)return false;
    *length=n;return true;
}
