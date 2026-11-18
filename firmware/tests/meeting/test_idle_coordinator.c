#include "meeting_idle_coordinator.h"
#include <assert.h>
#include <stdio.h>
static int64_t fake_time;
static int64_t read_time(void *ctx){(void)ctx;return fake_time;}
static bool slow_preempt(void *ctx){(void)ctx;fake_time=3001;return true;}
static bool fast_preempt(void *ctx){(void)ctx;fake_time=2999;return true;}

int main(void)
{
    assert(MEETING_RECORD_WAIT_MS == 3000);
    assert(meeting_idle_before_deadline(2999, 3000));
    assert(!meeting_idle_before_deadline(3000, 3000));
    assert(!meeting_idle_before_deadline(3001, 3000));
    fake_time=0;assert(!meeting_idle_preempt_before_deadline(3000,read_time,slow_preempt,NULL));
    fake_time=0;assert(meeting_idle_preempt_before_deadline(3000,read_time,fast_preempt,NULL));
    meeting_idle_coordinator c = {0};
    uint32_t a = meeting_idle_request_record(&c);
    assert(a != 0);
    assert(meeting_idle_record_event(&c, a, MEETING_IDLE_BLE_STOPPED));
    meeting_idle_record_timeout(&c, a);
    uint32_t b = meeting_idle_request_record(&c);
    assert(b != 0 && b != a);
    assert(!meeting_idle_record_event(&c, a, MEETING_IDLE_NETWORK_STOPPED));
    assert(!meeting_idle_record_event(&c, a, MEETING_IDLE_CAPTURE_STARTED));
    assert(!meeting_idle_record_event(&c, b, MEETING_IDLE_CAPTURE_STARTED));
    assert(meeting_idle_record_event(&c, b, MEETING_IDLE_BLE_STOPPED));
    assert(!meeting_idle_release_started(&c, b, MEETING_IDLE_RESOURCE_BLE));
    assert(meeting_idle_record_event(&c, b, MEETING_IDLE_NETWORK_STOPPED));
    assert(meeting_idle_record_event(&c, b, MEETING_IDLE_CAPTURE_STARTED));
    assert(!meeting_idle_record_event(&c, b, MEETING_IDLE_CAPTURE_STARTED));
    assert(!meeting_idle_record_finish(&c, a));
    assert(meeting_idle_record_finish(&c, b));
    assert(!meeting_idle_record_finish(&c, b));
    assert(meeting_idle_request_record(&c) != 0);
    puts("idle coordinator stale event checks: PASS");

    meeting_idle_coordinator pending = {0};
    a = meeting_idle_request_record(&pending);
    assert(meeting_idle_release_started(&pending, a, MEETING_IDLE_RESOURCE_BLE));
    assert(!meeting_idle_record_event(&pending, a, MEETING_IDLE_BLE_STOPPED));
    meeting_idle_record_timeout(&pending, a);
    assert(meeting_idle_request_record(&pending) == 0);
    assert(!meeting_idle_release_finished(&pending, a + 1, MEETING_IDLE_RESOURCE_BLE));
    assert(meeting_idle_release_finished(&pending, a, MEETING_IDLE_RESOURCE_BLE));
    assert(!meeting_idle_release_finished(&pending, a, MEETING_IDLE_RESOURCE_BLE));
    b = meeting_idle_request_record(&pending);
    assert(b && b != a);
    assert(!meeting_idle_record_event(&pending, a, MEETING_IDLE_CAPTURE_STARTED));
    puts("idle coordinator pending release checks: PASS");
}
