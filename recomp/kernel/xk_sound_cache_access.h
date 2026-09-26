#ifndef XK_SOUND_CACHE_ACCESS_H
#define XK_SOUND_CACHE_ACCESS_H
#include "xk_sound_cache.h"

/* Candidate integration boundary; xv_sound_ray uses verification only.
 * Zero-initialize before publication. Never hold guard during guest execution.
 * World-change hooks must call invalidate before publishing new world state. */
typedef struct {
    unsigned ways;
    uint32_t hash, epoch, frame, lifetime;
    uintptr_t domain;
    float listener[3], sound[3], listener_epsilon2, sound_epsilon2;
    float vector[3];
    unsigned exact_ray; /* require original start/vector bits in addition to endpoints */
} xv_sound_cache_query;
typedef struct {
    xv_sound_cache cache;
    unsigned guard, pending_reset;
    uint64_t generation; /* guarded; tickets survive a cast outside the lock */
} xv_sound_cache_access;
typedef struct {
    xv_sound_cache_query query;
    xv_sound_cache_entry matched; /* copied under guard, meaningful only on a hit */
    uint64_t generation;
    int valid;
} xv_sound_cache_ticket;

static inline void xv_sound_cache_invalidate(xv_sound_cache_access *state)
{
    /* No waiting for the rendering/simulation thread that owns the guard. */
    __atomic_store_n(&state->pending_reset, 1u, __ATOMIC_RELEASE);
}
static inline int xv_sound_cache_begin(xv_sound_cache_access *state,
    const xv_sound_cache_query *query, xv_sound_cache_ticket *ticket, uint8_t *hit)
{
    ticket->valid = 0;
    if (__atomic_exchange_n(&state->guard, 1u, __ATOMIC_ACQUIRE)) return 0;
    if (__atomic_exchange_n(&state->pending_reset, 0u, __ATOMIC_ACQ_REL)) {
        memset(&state->cache, 0, sizeof state->cache);
        ++state->generation;
    }
    ticket->query = *query;
    ticket->generation = state->generation;
    ticket->valid = 1;
    uint8_t answer = 0;
    int found = xv_sound_cache_probe(&state->cache, query->ways, query->hash,
        query->domain, query->epoch, query->frame, query->lifetime,
        query->listener, query->sound, query->listener_epsilon2,
        query->sound_epsilon2, &answer, &ticket->matched, query->exact_ray ? query->vector : NULL);
    if (__atomic_load_n(&state->pending_reset, __ATOMIC_ACQUIRE)) {
        ticket->valid = 0;
        found = 0;
    }
    if (found) *hit = answer; /* copy while guarded; never return an entry pointer */
    __atomic_store_n(&state->guard, 0u, __ATOMIC_RELEASE);
    return found;
}
static inline int xv_sound_cache_commit(xv_sound_cache_access *state,
    const xv_sound_cache_ticket *ticket, uint8_t hit)
{
    if (!ticket->valid ||
        __atomic_exchange_n(&state->guard, 1u, __ATOMIC_ACQUIRE)) return 0;
    int stored = 0;
    if (!__atomic_load_n(&state->pending_reset, __ATOMIC_ACQUIRE) &&
        state->generation == ticket->generation) {
        const xv_sound_cache_query *q = &ticket->query;
        stored = xv_sound_cache_store_ray(&state->cache, q->ways, q->hash, q->domain,
            q->epoch, q->frame, q->listener, q->sound, hit,
            q->exact_ray ? q->vector : NULL);
    }
    __atomic_store_n(&state->guard, 0u, __ATOMIC_RELEASE);
    return stored;
}
#endif
