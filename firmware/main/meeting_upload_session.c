#include "meeting_upload_session.h"
#include "meeting_upload_activity.h"

static void activity_progress(void *ctx,uint32_t next)
{
    uint64_t generation=*(const uint64_t *)ctx;
    (void)meeting_upload_activity_progress(generation,next);
}

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "sdkconfig.h"
#define SESSION_LOG(...) ESP_LOGI("meeting_upload",__VA_ARGS__)
#else
#define SESSION_LOG(...) ((void)0)
#endif

#if defined(CONFIG_MEETING_SUPERVISED_TEST_PREEMPT_CHUNK) || defined(CONFIG_MEETING_SUPERVISED_TEST_UPLOAD_INTERRUPTS)
#include "meeting_commission.h"
#endif

meeting_upload_result meeting_upload_session(FILE *source, const meeting_snapshot *snapshot,
                                              bool has_time, int64_t recorded_at, uint8_t time_trust,
                                              meeting_https_config *config, meeting_manifest *confirmed)
{
    meeting_upload_transport transport;
    if (!source || !snapshot || !meeting_https_bind(config, &transport)) return MEETING_UPLOAD_FAILED;
    if (transport.cancelled(transport.ctx)) return MEETING_UPLOAD_PAUSED;

    meeting_manifest manifest;
    if (!meeting_upload_manifest_verified_cancel(source, snapshot, has_time, recorded_at, time_trust,
                                                 &manifest, transport.cancelled, transport.ctx)) {
        return transport.cancelled(transport.ctx) ? MEETING_UPLOAD_PAUSED : MEETING_UPLOAD_FAILED;
    }
    SESSION_LOG("manifest ready");
    if (transport.cancelled(transport.ctx)) return MEETING_UPLOAD_PAUSED;

    uint64_t generation = config->activity_generation;
    bool observing = generation && meeting_upload_activity_bind(generation, manifest.id, manifest.digest);
    bool use_chunks = false;
#if defined(CONFIG_MEETING_SUPERVISED_TEST_PREEMPT_CHUNK) || defined(CONFIG_MEETING_SUPERVISED_TEST_UPLOAD_INTERRUPTS)
    /* These supervised tests inject faults at specific chunk protocol steps. */
    use_chunks = meeting_commission_allowed();
#endif
    meeting_upload_result result = MEETING_UPLOAD_FAILED;
    if (!use_chunks) {
        result = meeting_https_upload_file(config, source, snapshot, &manifest);
        use_chunks = result == MEETING_UPLOAD_FAILED && config->file_route_unavailable;
        if (result == MEETING_UPLOAD_CONFIRMED && observing) {
            activity_progress(&generation, manifest.count);
        }
    }
    if (use_chunks) {
        /* A missing route is the only automatic fallback. An unconfirmed
         * whole-file request may already have committed on the server. */
        result = transport.cancelled(transport.ctx) ? MEETING_UPLOAD_PAUSED :
            meeting_upload_run_observed(source, snapshot, &manifest, &transport,
                                        observing ? activity_progress : NULL,
                                        observing ? &generation : NULL);
    }
    meeting_https_close(config);
    if (result == MEETING_UPLOAD_CONFIRMED && confirmed) *confirmed = manifest;
    return result;
}
