#pragma once
#include "meeting_upload.h"
/* request executes a single authenticated HTTPS request with a bounded response.
 * Returns false on timeout, cancellation, invalid TLS, oversized response or I/O.
 * Must not follow redirects or log credentials. Caller owns source lock and
 * aborts requests promptly when capture starts. No result authorizes deletion.
 * The response buffer may alias the request body; implementations must finish
 * consuming the complete body before writing any response bytes. */
typedef struct {
 void *ctx;
 bool (*cancelled)(void *ctx);
 bool (*request)(void *ctx,const char *method,const char *path,const char *content_type,
                 const char *sha256,const uint8_t *body,size_t body_len,
                 int *status,char *response,size_t response_cap,size_t *response_len);
} meeting_upload_transport;
typedef enum { MEETING_UPLOAD_CONFIRMED, MEETING_UPLOAD_PAUSED, MEETING_UPLOAD_FAILED } meeting_upload_result;
meeting_upload_result meeting_upload_run(FILE *source,const meeting_snapshot *snapshot,
                                          const meeting_manifest *manifest,const meeting_upload_transport *transport);
/* Only for a caller that just built this manifest from source under the same
 * uninterrupted exclusive source lock. No cached or caller-supplied manifest
 * may use this entry point. The receipt does not authorize local deletion. */
meeting_upload_result meeting_upload_run_prevalidated(FILE *source,const meeting_snapshot *snapshot,
                                          const meeting_manifest *manifest,const meeting_upload_transport *transport);
typedef void (*meeting_upload_progress_fn)(void *ctx,uint32_t confirmed_next);
meeting_upload_result meeting_upload_run_observed(FILE *source,const meeting_snapshot *snapshot,
    const meeting_manifest *manifest,const meeting_upload_transport *transport,
    meeting_upload_progress_fn progress,void *progress_ctx);
