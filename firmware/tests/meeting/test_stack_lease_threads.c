#include "meeting_stack_lease.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static meeting_stack_lease lease;
static uint32_t token;
static int entered, closing;

static void *worker(void *unused)
{
    (void)unused;
    assert(!pthread_mutex_lock(&lock));
    uint32_t reference = meeting_stack_enter(&lease, MEETING_STACK_AUDIO, token);
    assert(reference);
    entered = 1;
    assert(!pthread_cond_signal(&changed));
    while (!closing) assert(!pthread_cond_wait(&changed, &lock));
    assert(!meeting_stack_enter(&lease, MEETING_STACK_AUDIO, token));
    assert(meeting_stack_leave(&lease, MEETING_STACK_AUDIO, token, reference));
    assert(!pthread_mutex_unlock(&lock));
    return NULL;
}

enum { PARALLEL = MEETING_STACK_MAX_REFERENCES };
static int parallel_entered, parallel_done, parallel_closing, release_last;
static uint32_t parallel_token;

static void *parallel_worker(void *arg)
{
    uintptr_t index = (uintptr_t)arg;
    assert(!pthread_mutex_lock(&lock));
    uint32_t reference = meeting_stack_enter(&lease, MEETING_STACK_TLS, parallel_token);
    assert(reference);
    ++parallel_entered;
    assert(!pthread_cond_broadcast(&changed));
    while (!parallel_closing || (index == PARALLEL - 1 && !release_last))
        assert(!pthread_cond_wait(&changed, &lock));
    assert(meeting_stack_leave(&lease, MEETING_STACK_TLS, parallel_token, reference));
    assert(!meeting_stack_leave(&lease, MEETING_STACK_TLS, parallel_token, reference));
    ++parallel_done;
    assert(!pthread_cond_broadcast(&changed));
    assert(!pthread_mutex_unlock(&lock));
    return NULL;
}

static void parallel_drain_test(void)
{
    for (unsigned round = 0; round < 100; ++round) {
        pthread_t threads[PARALLEL];
        assert(!pthread_mutex_lock(&lock));
        parallel_entered = parallel_done = parallel_closing = release_last = 0;
        parallel_token = meeting_stack_acquire(&lease, MEETING_STACK_TLS);
        assert(parallel_token);
        for (uintptr_t i = 0; i < PARALLEL; ++i)
            assert(!pthread_create(&threads[i], NULL, parallel_worker, (void *)i));
        while (parallel_entered != PARALLEL) assert(!pthread_cond_wait(&changed, &lock));
        assert(!meeting_stack_enter(&lease, MEETING_STACK_TLS, parallel_token));
        assert(meeting_stack_close(&lease, MEETING_STACK_TLS, parallel_token));
        parallel_closing = 1;
        assert(!pthread_cond_broadcast(&changed));
        while (parallel_done != PARALLEL - 1) assert(!pthread_cond_wait(&changed, &lock));
        assert(!meeting_stack_release(&lease, MEETING_STACK_TLS, parallel_token));
        assert(!meeting_stack_acquire(&lease, MEETING_STACK_AUDIO));
        release_last = 1;
        assert(!pthread_cond_broadcast(&changed));
        assert(!pthread_mutex_unlock(&lock));
        for (unsigned i = 0; i < PARALLEL; ++i) assert(!pthread_join(threads[i], NULL));
        assert(!pthread_mutex_lock(&lock));
        assert(meeting_stack_release(&lease, MEETING_STACK_TLS, parallel_token));
        assert(!pthread_mutex_unlock(&lock));
    }
}

int main(void)
{
    parallel_drain_test();
    for (int i = 0; i < 1000; ++i) {
        pthread_t thread;
        assert(!pthread_mutex_lock(&lock));
        entered = closing = 0;
        token = meeting_stack_acquire(&lease, MEETING_STACK_AUDIO);
        assert(token);
        assert(!pthread_create(&thread, NULL, worker, NULL));
        while (!entered) assert(!pthread_cond_wait(&changed, &lock));
        assert(meeting_stack_close(&lease, MEETING_STACK_AUDIO, token));
        assert(!meeting_stack_release(&lease, MEETING_STACK_AUDIO, token));
        assert(!meeting_stack_acquire(&lease, MEETING_STACK_TLS));
        closing = 1;
        assert(!pthread_cond_signal(&changed));
        assert(!pthread_mutex_unlock(&lock));
        assert(!pthread_join(thread, NULL));
        assert(!pthread_mutex_lock(&lock));
        assert(meeting_stack_release(&lease, MEETING_STACK_AUDIO, token));
        uint32_t next = meeting_stack_acquire(&lease, MEETING_STACK_TLS);
        assert(next && next != token);
        assert(!meeting_stack_leave(&lease, MEETING_STACK_AUDIO, token, 1));
        assert(meeting_stack_close(&lease, MEETING_STACK_TLS, next));
        assert(meeting_stack_release(&lease, MEETING_STACK_TLS, next));
        assert(!pthread_mutex_unlock(&lock));
    }
}
