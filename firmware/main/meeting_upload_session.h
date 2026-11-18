#pragma once
#include "meeting_https.h"
/* Caller verified this snapshot against source while holding the same source
 * lock, and holds that lock until this synchronous call returns. Confirmed
 * is only a verified remote receipt, not permission to delete local data. */
meeting_upload_result meeting_upload_session(FILE *source, const meeting_snapshot *snapshot,
                                              bool has_time, int64_t recorded_at, uint8_t time_trust,
                                              meeting_https_config *config, meeting_manifest *confirmed);
