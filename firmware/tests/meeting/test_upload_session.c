#include "meeting_upload_session.h"
#include "meeting_upload_activity.h"
#include <assert.h>
#include <string.h>

static const meeting_manifest expected = {
    .id = "00112233-4455-6677-8899-aabbccddeeff",
    .count = 7,
    .bytes = 25000,
    .digest = {1, 2, 3},
    .reason = 1,
    .recorded_at_present = true,
    .recorded_at = 1795651200,
    .time_trust = 2,
};

static struct {
    bool bind_fails;
    bool manifest_fails;
    bool cancelled;
    bool missing_route;
    bool cancel_after_file;
    meeting_upload_result file_result;
    meeting_upload_result chunk_result;
    unsigned file_calls;
    unsigned chunk_calls;
    unsigned close_calls;
} mock;

static void check_manifest(const meeting_manifest *manifest)
{
    assert(strcmp(manifest->id, expected.id) == 0);
    assert(manifest->count == expected.count);
    assert(manifest->bytes == expected.bytes);
    assert(memcmp(manifest->digest, expected.digest, sizeof(expected.digest)) == 0);
    assert(manifest->reason == expected.reason);
    assert(manifest->recorded_at_present == expected.recorded_at_present);
    assert(manifest->recorded_at == expected.recorded_at);
    assert(manifest->time_trust == expected.time_trust);
}

static bool cancelled(void *ctx)
{
    assert(ctx);
    return mock.cancelled;
}

void meeting_https_close(meeting_https_config *config)
{
    assert(config);
    ++mock.close_calls;
}

bool meeting_https_bind(meeting_https_config *config, meeting_upload_transport *out)
{
    if (!config || mock.bind_fails) return false;
    *out = (meeting_upload_transport){.ctx = config, .cancelled = cancelled};
    return true;
}

bool meeting_upload_manifest_verified_cancel(FILE *source, const meeting_snapshot *snapshot,
    bool has_time, int64_t recorded_at, uint8_t time_trust, meeting_manifest *out,
    meeting_cancel_fn is_cancelled, void *ctx)
{
    assert(source && snapshot && is_cancelled);
    assert(has_time && recorded_at == expected.recorded_at && time_trust == expected.time_trust);
    if (mock.manifest_fails || is_cancelled(ctx)) return false;
    *out = expected;
    return true;
}

meeting_upload_result meeting_https_upload_file(meeting_https_config *config, FILE *source,
    const meeting_snapshot *snapshot, const meeting_manifest *manifest)
{
    assert(config && source && snapshot);
    check_manifest(manifest);
    ++mock.file_calls;
    config->file_route_unavailable = mock.missing_route;
    if (mock.cancel_after_file) mock.cancelled = true;
    return mock.file_result;
}

meeting_upload_result meeting_upload_run_observed(FILE *source, const meeting_snapshot *snapshot,
    const meeting_manifest *manifest, const meeting_upload_transport *transport,
    meeting_upload_progress_fn progress, void *ctx)
{
    assert(source && snapshot && transport->ctx);
    check_manifest(manifest);
    ++mock.chunk_calls;
    if (progress && mock.chunk_result == MEETING_UPLOAD_CONFIRMED) {
        progress(ctx, manifest->count);
    }
    return mock.chunk_result;
}

static meeting_upload_result run_session(FILE *source, meeting_https_config *config,
                                        meeting_manifest *confirmed)
{
    const meeting_snapshot snapshot = {.valid_end = expected.bytes, .reason = expected.reason};
    return meeting_upload_session(source, &snapshot, true, expected.recorded_at, expected.time_trust,
                                  config, confirmed);
}

static void confirmed_whole_file(FILE *source)
{
    memset(&mock, 0, sizeof(mock));
    mock.file_result = MEETING_UPLOAD_CONFIRMED;
    meeting_https_config config = {.activity_generation = meeting_upload_activity_begin()};
    meeting_manifest confirmed = {0};

    assert(run_session(source, &config, &confirmed) == MEETING_UPLOAD_CONFIRMED);
    assert(mock.file_calls == 1 && mock.chunk_calls == 0 && mock.close_calls == 1);
    check_manifest(&confirmed);

    meeting_activity_snapshot activity = meeting_upload_activity_snapshot();
    assert(activity.generation == config.activity_generation);
    assert(activity.phase == MEETING_ACTIVITY_UPLOADING);
    assert(strcmp(activity.meeting_id, expected.id) == 0);
    assert(memcmp(activity.digest, expected.digest, sizeof(expected.digest)) == 0);
    assert(activity.confirmed_next == expected.count);
}

static void unconfirmed_whole_file(FILE *source)
{
    memset(&mock, 0, sizeof(mock));
    meeting_https_config config = {.file_route_unavailable = true};
    meeting_manifest confirmed = {.count = 99};

    mock.file_result = MEETING_UPLOAD_FAILED;
    assert(run_session(source, &config, &confirmed) == MEETING_UPLOAD_FAILED);
    mock.file_result = MEETING_UPLOAD_PAUSED;
    assert(run_session(source, &config, &confirmed) == MEETING_UPLOAD_PAUSED);
    assert(mock.file_calls == 2 && mock.chunk_calls == 0 && mock.close_calls == 2);
    assert(confirmed.count == 99);
}

static void unavailable_whole_file_route(FILE *source)
{
    memset(&mock, 0, sizeof(mock));
    mock.file_result = MEETING_UPLOAD_FAILED;
    mock.missing_route = true;
    meeting_https_config config = {0};
    meeting_manifest confirmed = {.count = 99};

    mock.chunk_result = MEETING_UPLOAD_FAILED;
    assert(run_session(source, &config, &confirmed) == MEETING_UPLOAD_FAILED);
    assert(confirmed.count == 99);

    mock.chunk_result = MEETING_UPLOAD_CONFIRMED;
    assert(run_session(source, &config, &confirmed) == MEETING_UPLOAD_CONFIRMED);
    assert(mock.file_calls == 2 && mock.chunk_calls == 2 && mock.close_calls == 2);
    check_manifest(&confirmed);

    mock.cancel_after_file = true;
    assert(run_session(source, &config, NULL) == MEETING_UPLOAD_PAUSED);
    assert(mock.file_calls == 3 && mock.chunk_calls == 2 && mock.close_calls == 3);
}

static void no_network_without_manifest(FILE *source)
{
    memset(&mock, 0, sizeof(mock));
    meeting_https_config config = {0};
    mock.cancelled = true;
    assert(run_session(source, &config, NULL) == MEETING_UPLOAD_PAUSED);
    mock.cancelled = false;
    mock.manifest_fails = true;
    assert(run_session(source, &config, NULL) == MEETING_UPLOAD_FAILED);
    mock.bind_fails = true;
    assert(run_session(source, &config, NULL) == MEETING_UPLOAD_FAILED);
    assert(mock.file_calls == 0 && mock.chunk_calls == 0 && mock.close_calls == 0);
}

int main(void)
{
    FILE *source = tmpfile();
    assert(source);
    confirmed_whole_file(source);
    unconfirmed_whole_file(source);
    unavailable_whole_file_route(source);
    no_network_without_manifest(source);
    fclose(source);
    return 0;
}
