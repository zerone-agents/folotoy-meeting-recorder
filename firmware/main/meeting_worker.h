#pragma once
#include "meeting_online_session.h"
#include "sc_proto.h"

/* Caller supplies scratch space and a stable, authenticated configuration.
 * Each pass examines the current catalog oldest-first. A paused transfer ends
 * the pass so recording can take ownership. No audio is deleted here. */
typedef struct { unsigned attempted, confirmed, failed; bool paused; } meeting_worker_result;
meeting_worker_result meeting_worker_pass(const meeting_online_config *config, sc_file_entry_t *scratch, size_t capacity);

/* Upload a specific sealed recording ahead of the catalog pass. Confirmation
 * includes runtime's verified receipt and completed local cleanup. */
meeting_upload_result meeting_worker_upload_one(const meeting_online_config *config, uint32_t id);
