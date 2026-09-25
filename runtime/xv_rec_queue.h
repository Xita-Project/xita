/* xv_rec_queue.h - single-producer / single-consumer record queue with a sleeping consumer (XV_REC_DEFER).
 *
 * Records are variable-size, 16-byte aligned, and never wrap: one that does not fit before the end of the ring
 * leaves a wrap record there and starts at 0. head and tail are monotonic byte positions: the producer publishes
 * with a release store of head, the consumer frees with a release store of tail after using a record.
 *
 * Wake protocol (state: 0 running, 1 sleeping, 2 pending):
 *   producer, after publishing:  if exchange(state, 2) was 1, set the wake flag
 *   consumer, finding no work:   CAS(state, 0 -> 1); on failure (pending) look again; on success look once more for a
 *                                waiting producer, then wait for the wake flag (with a timeout)
 * A producer that must wait (a drain, or a full ring) sets want, wakes the consumer and waits for the done flag; the
 * consumer sets done whenever it finds the queue empty (or its work done) while want is set.
 *
 * No thread-local state. The event flags are SceKernel ones (pthread-backed in the host shim and the unit test). */
#pragma once
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>

typedef struct { uint16_t type, mode; uint32_t bytes; } xv_rq_hdr;   /* bytes: the whole record */
#define XV_RQ_WRAP 0xFFFFu
typedef struct {
    uint8_t *ring; uint32_t size;          /* power of two */
    uint32_t head, tail, reserved;
    unsigned state, want, unsignaled, batch;
    SceUID wake, done;
    /* producer-side statistics */
    unsigned full_waits, drains, drains_busy; uint32_t high; uint64_t bytes_published;
} xv_rq;

static inline void xv_rq_notify(xv_rq *q)
{
    q->unsignaled = 0;
    if (__atomic_exchange_n(&q->state, 2u, __ATOMIC_ACQ_REL) == 1u) sceKernelSetEventFlag(q->wake, 1);
}
/* Producer: until the ring has need_free bytes free, or (need_free 0) until the consumer has used every record. */
static inline void xv_rq_wait(xv_rq *q, uint32_t need_free, unsigned spin)
{
    __atomic_store_n(&q->want, 1u, __ATOMIC_RELEASE);
    xv_rq_notify(q);
    for (unsigned n = 0;; ++n) {
        uint32_t tail = __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE);
        if (tail == q->head || (need_free && q->size - (q->head - tail) >= need_free)) break;
        if (n < spin) continue;
        unsigned bits; SceUInt timeout = 2000;
        sceKernelWaitEventFlag(q->done, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
    }
    __atomic_store_n(&q->want, 0u, __ATOMIC_RELEASE);
}
static inline int xv_rq_empty(xv_rq *q) { return __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE) == __atomic_load_n(&q->head, __ATOMIC_ACQUIRE); }
/* Producer: space for one record (the header's bytes field is set); published by xv_rq_publish. */
static inline void *xv_rq_reserve(xv_rq *q, uint32_t bytes)
{
    bytes = (bytes + 15u) & ~15u;
    uint32_t pos = q->head & (q->size - 1u), room = q->size - pos, skip = room < bytes ? room : 0u;
    if (q->size - (q->head - __atomic_load_n(&q->tail, __ATOMIC_ACQUIRE)) < skip + bytes + 16u) {
        q->full_waits++;
        xv_rq_wait(q, skip + bytes + 16u, 64);
    }
    if (skip) { xv_rq_hdr *w = (xv_rq_hdr *)(q->ring + pos); w->type = XV_RQ_WRAP; w->mode = 0; w->bytes = skip; pos = 0; }
    q->reserved = skip + bytes;
    xv_rq_hdr *h = (xv_rq_hdr *)(q->ring + pos); h->bytes = bytes;
    return h;
}
static inline void xv_rq_publish(xv_rq *q, int wake)
{
    uint32_t used = q->head + q->reserved - __atomic_load_n(&q->tail, __ATOMIC_RELAXED);
    if (used > q->high) q->high = used;
    q->bytes_published += q->reserved;
    __atomic_store_n(&q->head, q->head + q->reserved, __ATOMIC_RELEASE);
    q->reserved = 0;
    if (wake && ++q->unsignaled >= q->batch) xv_rq_notify(q);
}
/* Consumer: the record at *tail (wrap records skipped, *tail advanced over them), or NULL when *tail reaches head. */
static inline xv_rq_hdr *xv_rq_at(xv_rq *q, uint32_t *tail, uint32_t head)
{
    for (;;) {
        if (*tail == head) return NULL;
        xv_rq_hdr *h = (xv_rq_hdr *)(q->ring + (*tail & (q->size - 1u)));
        if (h->type != XV_RQ_WRAP) return h;
        *tail += h->bytes;
    }
}
static inline void xv_rq_release(xv_rq *q, uint32_t tail) { __atomic_store_n(&q->tail, tail, __ATOMIC_RELEASE); }
/* Consumer with nothing (it may use) to do: answer a waiting producer, then sleep until woken (or timeout_us). */
static inline void xv_rq_idle(xv_rq *q, SceUInt timeout_us)
{
    if (__atomic_load_n(&q->want, __ATOMIC_ACQUIRE)) sceKernelSetEventFlag(q->done, 1);
    unsigned expected = 0u;
    if (!__atomic_compare_exchange_n(&q->state, &expected, 1u, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;   /* pending */
    if (__atomic_load_n(&q->want, __ATOMIC_ACQUIRE)) {
        if (!xv_rq_empty(q)) return;   /* a waiting producer published first: work again */
        sceKernelSetEventFlag(q->done, 1);
    }
    unsigned bits; SceUInt timeout = timeout_us;
    sceKernelWaitEventFlag(q->wake, 1, SCE_EVENT_WAITOR | SCE_EVENT_WAITCLEAR_PAT, &bits, &timeout);
}
static inline void xv_rq_running(xv_rq *q) { __atomic_store_n(&q->state, 0u, __ATOMIC_RELEASE); }
