/* XV_REC_DEFER queue (runtime/xv_rec_queue.h): one producer and one sleeping consumer, as the scene helper and the
 * recording worker use it. Small rings (wrap records and full waits all the time), random record sizes, records the
 * consumer may only take while a drain waits (the verify mode's), random drains. Checks: every record arrives once, in
 * order, with its bytes; a drain returns only after the consumer used everything published before it; no record of
 * the drain-only kind is used without a drain waiting; nothing hangs (every wait is bounded by the test's watchdog).
 * Build it with -fsanitize=thread too (make rec-queue-tsan). SceKernel event flags are pthread ones here. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include "../../runtime/xv_rec_queue.h"
#if XV_REC_QUEUE_PADDED
#include <stddef.h>
_Static_assert(_Alignof(xv_rq) >= 64, "queue alignment");
_Static_assert(offsetof(xv_rq, head) % 64 == 0, "producer alignment");
_Static_assert(offsetof(xv_rq, tail) >= offsetof(xv_rq, head) + 64, "consumer isolation");
_Static_assert(offsetof(xv_rq, state) >= offsetof(xv_rq, tail) + 64, "wake-state isolation");
#endif

/* ---- minimal SceKernel event flags ---- */
typedef struct { pthread_mutex_t m; pthread_cond_t c; unsigned bits; } flag_t;
static flag_t flags[4]; static int nflags;
SceUID sceKernelCreateEventFlag(const char *name, int attr, int bits, SceKernelEventFlagOptParam *opt)
{
    (void)name; (void)attr; (void)opt; flag_t *f = &flags[nflags];
    pthread_mutex_init(&f->m, NULL); pthread_cond_init(&f->c, NULL); f->bits = (unsigned)bits; return nflags++;
}
int sceKernelSetEventFlag(SceUID id, unsigned int bits)
{
    flag_t *f = &flags[id]; pthread_mutex_lock(&f->m); f->bits |= bits; pthread_cond_broadcast(&f->c); pthread_mutex_unlock(&f->m); return 0;
}
int sceKernelWaitEventFlag(SceUID id, unsigned int bits, unsigned int wait, unsigned int *out, SceUInt *timeout)
{
    flag_t *f = &flags[id]; int rc = 0;
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl);
    uint64_t ns = (uint64_t)dl.tv_nsec + (uint64_t)(timeout ? *timeout : 1000000u) * 1000u;
    dl.tv_sec += (time_t)(ns / 1000000000u); dl.tv_nsec = (long)(ns % 1000000000u);
    pthread_mutex_lock(&f->m);
    while (!(f->bits & bits)) if (pthread_cond_timedwait(&f->c, &f->m, &dl) == ETIMEDOUT) { rc = -1; break; }
    if (out) *out = f->bits;
    if (!rc && (wait & SCE_EVENT_WAITCLEAR_PAT)) f->bits &= ~bits;
    pthread_mutex_unlock(&f->m);
    return rc;
}

static xv_rq q;
static uint64_t produced, consumed_seq;       /* consumed_seq: next expected sequence number (consumer) */
static unsigned consumed_verify_without_drain, stop;
static uint32_t rng = 1;
static uint32_t next_rand(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
typedef struct { xv_rq_hdr h; uint64_t seq; uint32_t len; uint32_t sum; } rec_t;

static void *consumer(void *arg)
{
    (void)arg;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
        xv_rq_running(&q);
        for (;;) {
            uint32_t head = __atomic_load_n(&q.head, __ATOMIC_ACQUIRE), tail = q.tail;
            xv_rq_hdr *h = xv_rq_at(&q, &tail, head);
            if (!h) { if (tail != q.tail) xv_rq_release(&q, tail); break; }
            if (h->mode == 1 && !__atomic_load_n(&q.want, __ATOMIC_ACQUIRE)) break;
            do {
                rec_t *r = (rec_t *)h;
                if (h->mode == 1 && !__atomic_load_n(&q.want, __ATOMIC_ACQUIRE)) consumed_verify_without_drain++;
                assert(r->seq == consumed_seq);
                uint32_t sum = 0; const uint8_t *b = (const uint8_t *)(r + 1);
                for (uint32_t i = 0; i < r->len; i++) sum = sum * 31u + b[i];
                assert(sum == r->sum);
                consumed_seq++;
                tail += h->bytes;
                if (h->mode != 1) break;          /* mode 2: one record per release; mode 1: the whole batch */
            } while ((h = xv_rq_at(&q, &tail, head)) != NULL);
            xv_rq_release(&q, tail);
        }
        xv_rq_idle(&q, 1000);
    }
    return NULL;
}

static void run(uint32_t ring_bytes, unsigned batch, uint64_t records, unsigned drain_every)
{
    memset(&q, 0, sizeof q); nflags = 0;
    q.ring = malloc(ring_bytes); q.size = ring_bytes; q.batch = batch;
    q.wake = sceKernelCreateEventFlag("wake", 0, 0, NULL); q.done = sceKernelCreateEventFlag("done", 0, 0, NULL);
    produced = consumed_seq = 0; consumed_verify_without_drain = 0; stop = 0;
    pthread_t t; pthread_create(&t, NULL, consumer, NULL);
    unsigned drains = 0; int mode = 2;
    for (uint64_t i = 0; i < records; i++) {
        if (i % 5000 == 0) {                        /* switch between deferred and verify blocks, as frames do */
            xv_rq_wait(&q, 0, 64); drains++;
            assert(__atomic_load_n(&consumed_seq, __ATOMIC_ACQUIRE) == produced);
            mode = (i / 5000) % 3 == 2 ? 1 : 2;
        }
        uint32_t len = next_rand() % (ring_bytes / 8u);
        rec_t *r = xv_rq_reserve(&q, (uint32_t)sizeof(rec_t) + len);
        r->h.type = 1; r->h.mode = (uint16_t)mode; r->seq = produced; r->len = len;
        uint8_t *b = (uint8_t *)(r + 1); uint32_t sum = 0;
        for (uint32_t k = 0; k < len; k++) { b[k] = (uint8_t)(next_rand() >> 3); sum = sum * 31u + b[k]; }
        r->sum = sum;
        xv_rq_publish(&q, mode == 2);
        produced++;
        if (next_rand() % drain_every == 0) {
            xv_rq_wait(&q, 0, next_rand() % 2 ? 64 : 0); drains++;
            assert(__atomic_load_n(&consumed_seq, __ATOMIC_ACQUIRE) == produced);
        }
    }
    xv_rq_wait(&q, 0, 64);
    assert(__atomic_load_n(&consumed_seq, __ATOMIC_ACQUIRE) == produced);
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE); sceKernelSetEventFlag(q.wake, 1);
    pthread_join(t, NULL);
    assert(consumed_verify_without_drain == 0);
    printf("  ring %6u B batch %u: %llu records, %u drains, %u full waits, high %u B\n", ring_bytes, batch,
        (unsigned long long)produced, drains, q.full_waits, q.high);
    free(q.ring);
}

static void *watchdog(void *arg) { (void)arg; sleep(600); fprintf(stderr, "rec_queue_test: HANG\n"); abort(); return NULL; }

int main(int argc, char **argv)
{
    uint64_t n = argc > 1 ? strtoull(argv[1], NULL, 10) : 400000;
    pthread_t w; pthread_create(&w, NULL, watchdog, NULL); pthread_detach(w);
    run(4096, 1, n, 37);
    run(4096, 4, n, 211);
    run(65536, 1, n, 997);
    run(1u << 20, 2, n, 5000);
    printf("rec_queue_test: pass\n");
    return 0;
}
