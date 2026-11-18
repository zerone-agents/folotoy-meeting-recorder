#pragma once
#include "meeting_upload_session.h"
/* Only call after snapshot verification while holding the same source lock,
 * from the network worker while it owns Wi-Fi.
 * Disconnect or recording start cancels the current session; no local deletion. */
typedef struct {
 meeting_https_config https;
 bool (*has_ip)(void *ctx);
 void *network_ctx;
} meeting_online_config;
meeting_upload_result meeting_online_session(FILE *source,const meeting_snapshot *snapshot,
                                              bool has_time,int64_t recorded_at,uint8_t time_trust,
                                              const meeting_online_config *config, meeting_manifest *confirmed);
