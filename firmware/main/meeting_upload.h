#pragma once
#include "meeting_receipt.h"
#include "meeting_snapshot_file.h"
#define MEETING_UPLOAD_CHUNK_SIZE 4096u
/* Caller holds exclusive source access for the entire session and must call
 * meeting_upload_manifest successfully before each new session. */
bool meeting_upload_manifest(FILE *file, const meeting_snapshot *snapshot,
                             bool has_time, int64_t recorded_at, uint8_t time_trust,
                             meeting_manifest *out);
bool meeting_upload_manifest_cancel(FILE *file, const meeting_snapshot *snapshot,
                             bool has_time, int64_t recorded_at, uint8_t time_trust,
                             meeting_manifest *out, meeting_cancel_fn cancelled, void *ctx);
/* Only after the same snapshot was verified against this source under an
 * uninterrupted exclusive source lock; never pass a cached verification. */
bool meeting_upload_manifest_verified_cancel(FILE *file, const meeting_snapshot *snapshot,
                             bool has_time, int64_t recorded_at, uint8_t time_trust,
                             meeting_manifest *out, meeting_cancel_fn cancelled, void *ctx);
bool meeting_upload_chunk(FILE *file, const meeting_snapshot *snapshot, uint32_t seq,
                          uint8_t *buffer, size_t capacity, size_t *length, uint8_t digest[32]);
