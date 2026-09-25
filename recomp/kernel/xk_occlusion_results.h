#pragma once
#include <stdint.h>
/* One GPU-completion producer, one serialized scene consumer. Neither waits.
 * Payload ownership transfers through release/acquire sequence publication.
 * Never reset a live queue; counter wrap is intentional unsigned arithmetic. */
#define XV_OCCL_RESULT_CAP 1024u
_Static_assert(__atomic_always_lock_free(4, 0), "32-bit atomics required");
typedef struct { uint32_t handle, frame, real, proxy, flags; } xv_occl_event;
typedef struct {
    _Alignas(64) uint32_t head;
    _Alignas(64) uint32_t tail;
    _Alignas(64) uint32_t dropped;
    xv_occl_event events[XV_OCCL_RESULT_CAP];
} xv_occl_results;
static inline int xv_occl_results_push(xv_occl_results *q, xv_occl_event e)
{
    uint32_t h=__atomic_load_n(&q->head,__ATOMIC_RELAXED);
    uint32_t t=__atomic_load_n(&q->tail,__ATOMIC_ACQUIRE);
    if(h-t>=XV_OCCL_RESULT_CAP) {
        /* Sticky loss indicator: consumer must fail open after any overflow. */
        __atomic_store_n(&q->dropped,1u,__ATOMIC_RELEASE); return 0;
    }
    q->events[h&(XV_OCCL_RESULT_CAP-1)]=e;
    __atomic_store_n(&q->head,h+1,__ATOMIC_RELEASE); return 1;
}
static inline int xv_occl_results_pop(xv_occl_results *q,xv_occl_event *e)
{
    uint32_t t=__atomic_load_n(&q->tail,__ATOMIC_RELAXED);
    uint32_t h=__atomic_load_n(&q->head,__ATOMIC_ACQUIRE);
    if(t==h)return 0;
    *e=q->events[t&(XV_OCCL_RESULT_CAP-1)];
    __atomic_store_n(&q->tail,t+1,__ATOMIC_RELEASE);return 1;
}
