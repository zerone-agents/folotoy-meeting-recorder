#include "meeting_idle_window.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    meeting_idle_window w;
    meeting_window_init(&w, 0);
    assert(meeting_window_tick(&w, 19999, false) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 20000, false) == MEETING_WINDOW_STOP_UPLOAD);
    assert(meeting_window_event(&w, w.generation + 1, MEETING_WINDOW_UPLOAD_STOPPED, 20001) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 20001) == MEETING_WINDOW_STOP_STA);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_STA_STOPPED, 20002) == MEETING_WINDOW_START_BLE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_BLE_STARTED, 20003) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 20004, true) == MEETING_WINDOW_STOP_BLE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_BLE_STOPPED, 20005) == MEETING_WINDOW_RECORD_READY);
    assert(meeting_window_tick(&w, 25000, true) == MEETING_WINDOW_NONE);
    puts("idle window recording preemption: PASS");

    meeting_window_init(&w, 0);
    assert(meeting_window_tick(&w, 20000, false) == MEETING_WINDOW_STOP_UPLOAD);
    assert(meeting_window_tick(&w, 23000, false) == MEETING_WINDOW_CANCEL_WINDOW);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 23001) == MEETING_WINDOW_RESUME_UPLOAD);
    uint32_t old = w.generation;
    assert(meeting_window_tick(&w, 90000, false) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, old, MEETING_WINDOW_UPLOAD_RESUMED, 90001) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 110001, false) == MEETING_WINDOW_STOP_UPLOAD);
    assert(w.generation != old);
    assert(meeting_window_event(&w, old, MEETING_WINDOW_UPLOAD_STOPPED, 110002) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 110003) == MEETING_WINDOW_STOP_STA);
    puts("idle window drain timeout: PASS");

    meeting_window_init(&w, 0);
    assert(meeting_window_tick(&w, 20000, false) == MEETING_WINDOW_STOP_UPLOAD);
    assert(meeting_window_tick(&w, 21000, true) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 23000, true) == MEETING_WINDOW_CANCEL_WINDOW);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 23001) == MEETING_WINDOW_STOP_STA);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_STA_STOPPED, 23002) == MEETING_WINDOW_RECORD_READY);
    assert(meeting_window_record_finished(&w, w.generation) == MEETING_WINDOW_RESUME_UPLOAD);
    assert(meeting_window_record_finished(&w, w.generation) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 23003, true) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_RESUMED, 23004) == MEETING_WINDOW_STOP_UPLOAD);
    puts("idle window late drain while recording requested: PASS");

    meeting_window_init(&w, 0);
    assert(meeting_window_tick(&w, 20000, false) == MEETING_WINDOW_STOP_UPLOAD);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 20001) == MEETING_WINDOW_STOP_STA);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_ACTION_FAILED, 20002) == MEETING_WINDOW_FAIL_CLOSED);
    assert(meeting_window_tick(&w, 30000, true) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_STA_STOPPED, 30001) == MEETING_WINDOW_NONE);
    puts("idle window action failure retains ownership: PASS");

    meeting_window_init(&w, 0);
    assert(meeting_window_tick(&w, 0, true) == MEETING_WINDOW_STOP_UPLOAD);
    assert(meeting_window_event(&w, w.generation, MEETING_WINDOW_UPLOAD_STOPPED, 1) == MEETING_WINDOW_STOP_STA);
    uint32_t timed_out = w.generation;
    assert(meeting_window_cancel_record(&w, timed_out) == MEETING_WINDOW_NONE);
    assert(meeting_window_event(&w, timed_out, MEETING_WINDOW_STA_STOPPED, 3001) == MEETING_WINDOW_RESUME_UPLOAD);
    assert(meeting_window_event(&w, timed_out, MEETING_WINDOW_UPLOAD_RESUMED, 3002) == MEETING_WINDOW_NONE);
    assert(meeting_window_tick(&w, 3003, true) == MEETING_WINDOW_STOP_UPLOAD);
    assert(w.generation != timed_out);
    assert(meeting_window_event(&w, timed_out, MEETING_WINDOW_STA_STOPPED, 3004) == MEETING_WINDOW_NONE);
    puts("idle window timed-out recording cannot start late: PASS");
}
