#include "meeting_worker.h"
#include "meeting_runtime.h"
#include "sc_store.h"
#include "esp_log.h"
#include "meeting_upload_activity.h"

static const char *TAG = "meeting_worker";

static bool config_valid(const meeting_online_config *config)
{
    return config && config->has_ip && config->https.is_cancelled && config->https.time_valid;
}

static bool upload_ready(const meeting_online_config *config)
{
    return config->has_ip(config->network_ctx) && config->https.time_valid(config->https.cancel_ctx) &&
           !config->https.is_cancelled(config->https.cancel_ctx);
}

static meeting_upload_result upload_recording(const meeting_online_config *config, uint32_t id)
{
    bool has_time;
    int64_t recorded_at;
    uint8_t trust;
    if (!sc_store_record_time(id, &has_time, &recorded_at, &trust)) {
        ESP_LOGW(TAG, "file %lu: recording metadata unavailable; retained for maintenance", (unsigned long)id);
        return MEETING_UPLOAD_FAILED;
    }

    meeting_online_config observed = *config;
    uint64_t generation = meeting_upload_activity_begin();
    observed.https.activity_generation = generation;
    meeting_upload_result status = meeting_runtime_upload(id, has_time, recorded_at, trust, &observed);
    meeting_upload_activity_finish(generation, status == MEETING_UPLOAD_CONFIRMED ? MEETING_ACTIVITY_CONFIRMED :
        status == MEETING_UPLOAD_PAUSED ? MEETING_ACTIVITY_PAUSED : MEETING_ACTIVITY_FAILED);
    /* Runtime confirms only after receipt verification, durable cleanup
     * intent and file removal. Retire the volatile progress for that ID. */
    if (status == MEETING_UPLOAD_CONFIRMED) (void)meeting_upload_activity_retire(generation);
    return status;
}

meeting_upload_result meeting_worker_upload_one(const meeting_online_config *config, uint32_t id)
{
    if (!id || !config_valid(config)) return MEETING_UPLOAD_FAILED;
    if (!upload_ready(config)) return MEETING_UPLOAD_PAUSED;
    return upload_recording(config, id);
}

meeting_worker_result meeting_worker_pass(const meeting_online_config *config, sc_file_entry_t *scratch, size_t capacity)
{
    meeting_worker_result result = {0};
    if (!scratch || !capacity || !config_valid(config) || !upload_ready(config)) return result;

    size_t end = sc_store_count();
    while (end) {
        size_t start = end > capacity ? end - capacity : 0;
        size_t n = sc_store_list_range(start, scratch, end - start);
        end = start;
        while (n) {
            if (!upload_ready(config)) {
                result.paused = true;
                break;
            }
            uint32_t id = scratch[--n].file_id;
            if (!id || scratch[n].size == 0) continue;

            meeting_upload_result status = upload_recording(config, id);
            result.attempted++;
            if (status == MEETING_UPLOAD_PAUSED) {
                result.paused = true;
                break;
            }
            if (status == MEETING_UPLOAD_CONFIRMED) result.confirmed++;
            else result.failed++;
        }
        if (result.paused) break;
    }
    return result;
}
