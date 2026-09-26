#include "xk_sound_cache_access.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static xv_sound_cache_access state;
static xv_sound_cache_query query(uintptr_t domain)
{
    xv_sound_cache_query q = {0};
    q.ways = 4; q.hash = 7; q.epoch = 1; q.domain = domain;
    q.frame = 100; q.lifetime = 6;
    q.listener_epsilon2 = 0.09f; q.sound_epsilon2 = 0.01f;
    return q;
}
static void *worker(void *arg)
{
    uintptr_t domain = (uintptr_t)arg;
    xv_sound_cache_query q = query(domain);
    for (unsigned i = 0; i < 40000; ++i) {
        xv_sound_cache_ticket t;
        uint8_t hit = 99;
        if (xv_sound_cache_begin(&state, &q, &t, &hit)) assert(hit == domain);
        else xv_sound_cache_commit(&state, &t, (uint8_t)domain);
    }
    return NULL;
}
static void *invalidator(void *arg)
{
    (void)arg;
    for (unsigned i = 0; i < 10000; ++i) xv_sound_cache_invalidate(&state);
    return NULL;
}
int main(void)
{
    xv_sound_cache_query q = query(1);
    xv_sound_cache_ticket old, fresh;
    uint8_t hit = 99;
    assert(!xv_sound_cache_begin(&state, &q, &old, &hit) && old.valid);
    /* Simulate world change while the real collision query runs. */
    xv_sound_cache_invalidate(&state);
    assert(!xv_sound_cache_commit(&state, &old, 1));
    assert(!xv_sound_cache_begin(&state, &q, &fresh, &hit));
    assert(!xv_sound_cache_commit(&state, &old, 1));
    assert(xv_sound_cache_commit(&state, &fresh, 2));
    assert(xv_sound_cache_begin(&state, &q, &fresh, &hit) && hit == 2);
    assert(fresh.matched.frame == 100 && fresh.matched.domain == 1 && fresh.matched.hit == 2);
    /* A cast may mutate caller storage; its ticket retains the original input. */
    xv_sound_cache_invalidate(&state);
    assert(!xv_sound_cache_begin(&state, &q, &fresh, &hit));
    q.domain = 3; q.sound[0] = 20;
    assert(xv_sound_cache_commit(&state, &fresh, 1));
    q = query(1);
    assert(xv_sound_cache_begin(&state, &q, &fresh, &hit) && hit == 1);
    /* Busy lookup/commit never wait, and invalidate is allowed while guarded. */
    __atomic_store_n(&state.guard, 1u, __ATOMIC_RELEASE);
    assert(!xv_sound_cache_begin(&state, &q, &old, &hit) && !old.valid);
    assert(!xv_sound_cache_commit(&state, &fresh, 2));
    xv_sound_cache_invalidate(&state);
    __atomic_store_n(&state.guard, 0u, __ATOMIC_RELEASE);
    assert(!xv_sound_cache_begin(&state, &q, &fresh, &hit));
    pthread_t a, b, reset;
    assert(!pthread_create(&a, NULL, worker, (void *)(uintptr_t)1));
    assert(!pthread_create(&b, NULL, worker, (void *)(uintptr_t)2));
    assert(!pthread_create(&reset, NULL, invalidator, NULL));
    assert(!pthread_join(a, NULL)); assert(!pthread_join(b, NULL));
    assert(!pthread_join(reset, NULL));
    puts("PASS: busy fallback, input snapshots, stale commit rejection, concurrent views and invalidation");
    return 0;
}
