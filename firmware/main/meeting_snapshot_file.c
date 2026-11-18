#include "meeting_snapshot_file.h"
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

bool meeting_snapshot_source_hash(FILE *file, uint8_t digest[32])
{
    return meeting_snapshot_source_hash_cancel(file,digest,NULL,NULL);
}
bool meeting_snapshot_source_hash_cancel(FILE *file, uint8_t digest[32], meeting_cancel_fn cancelled, void *cancel_ctx)
{
    if (!file || !digest) return false;
    struct stat before, after;
    if (fstat(fileno(file), &before) != 0 || before.st_size < 0 ||
        fseek(file, 0, SEEK_SET) != 0) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t chunk[512], calculated[32];
    uint64_t total = 0;
    while (ok && total < (uint64_t)before.st_size) {
        if (cancelled && cancelled(cancel_ctx)) { ok=false; break; }
        size_t want = (uint64_t)before.st_size - total > sizeof(chunk) ? sizeof(chunk) : (size_t)((uint64_t)before.st_size - total);
        size_t got = fread(chunk, 1, want, file);
        if (got != want || mbedtls_sha256_update(&ctx, chunk, got) != 0) { ok = false; break; }
        total += got;
    }
    if (ok && cancelled && cancelled(cancel_ctx)) ok=false;
    if (ok) ok = mbedtls_sha256_finish(&ctx, calculated) == 0;
    mbedtls_sha256_free(&ctx);
    if (fstat(fileno(file), &after) != 0 || before.st_size != after.st_size || before.st_mtime != after.st_mtime) ok = false;
    if (ok) memcpy(digest, calculated, 32);
    memset(calculated, 0, sizeof(calculated));
    return ok;
}

bool meeting_snapshot_matches_file(FILE *file, const meeting_snapshot *snapshot)
{
    return meeting_snapshot_matches_file_cancel(file,snapshot,NULL,NULL);
}
static bool hash_observed(void *ctx,const uint8_t *data,size_t length)
{
    return mbedtls_sha256_update(ctx,data,length)==0;
}
bool meeting_snapshot_matches_file_cancel(FILE *file, const meeting_snapshot *snapshot, meeting_cancel_fn cancelled, void *ctx)
{
    if (!file || !snapshot) return false;
    struct stat before,after;
    if(fstat(fileno(file),&before)!=0||before.st_size<0)return false;
    meeting_scr1_scan_result scan; uint8_t digest[32]={0};
    mbedtls_sha256_context hash;mbedtls_sha256_init(&hash);
    bool ok=mbedtls_sha256_starts(&hash,0)==0&&
        meeting_scr1_scan_file_observe(file,&scan,cancelled,ctx,hash_observed,&hash)&&
        scan.valid_end==snapshot->valid_end&&scan.frames==snapshot->frames&&
        !(cancelled&&cancelled(ctx))&&mbedtls_sha256_finish(&hash,digest)==0;
    mbedtls_sha256_free(&hash);
    if(fstat(fileno(file),&after)!=0||before.st_size!=after.st_size||before.st_mtime!=after.st_mtime)ok=false;
    unsigned diff = 0;
    for (size_t i = 0; i < sizeof(digest); ++i) diff |= digest[i] ^ snapshot->source_sha256[i];
    memset(digest, 0, sizeof(digest));
    return ok && diff == 0;
}

bool meeting_projection_digest(FILE *file, const meeting_scr1_scan_result *scan,
                               uint64_t offset, uint64_t length, uint8_t digest[32])
{
    return meeting_projection_digest_cancel(file,scan,offset,length,digest,NULL,NULL);
}
bool meeting_projection_digest_cancel(FILE *file, const meeting_scr1_scan_result *scan,
                                      uint64_t offset, uint64_t length, uint8_t digest[32],meeting_cancel_fn cancelled,void *cancel_ctx)
{
    if (!file || !scan || !digest || offset > scan->valid_end || length > scan->valid_end - offset) return false;
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    bool ok = mbedtls_sha256_starts(&ctx, 0) == 0;
    uint8_t chunk[512], calculated[32];
    uint64_t done = 0;
    while (ok && done < length) {
        if (cancelled && cancelled(cancel_ctx)) { ok=false; break; }
        size_t n = length - done > sizeof(chunk) ? sizeof(chunk) : (size_t)(length - done);
        if (!meeting_scr1_read_projected(file, scan, offset + done, chunk, n) ||
            mbedtls_sha256_update(&ctx, chunk, n) != 0) { ok = false; break; }
        done += n;
    }
    if (ok && cancelled && cancelled(cancel_ctx)) ok=false;
    if (ok) ok = mbedtls_sha256_finish(&ctx, calculated) == 0;
    mbedtls_sha256_free(&ctx);
    if (ok) memcpy(digest, calculated, sizeof(calculated));
    memset(calculated, 0, sizeof(calculated));
    return ok;
}
