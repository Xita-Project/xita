#ifndef XV_VERTEX_PROFILE_H
#define XV_VERTEX_PROFILE_H
#include <stdint.h>

enum xv_vertex_work {
    XV_VERTEX_EQUAL, XV_VERTEX_DIFFERENT, XV_VERTEX_SNAPSHOT,
    XV_VERTEX_DISPATCH, XV_VERTEX_WORK_COUNT
};

/* Diagnostic builds only. The recorder owns these counters. Byte counts are
 * requested spans, not measured bus traffic; a failed equality can exit early.
 * Times include preemption and timer overhead. Never use this build as the
 * ordinary performance baseline or add these times to overlapping GPU work. */
#if defined(XV_VERTEX_PROFILE) && XV_VERTEX_PROFILE
#include <string.h>
extern uint64_t xk_os_monotonic_us(void) __attribute__((weak));
extern void xv_logf(const char *, ...);
enum { XV_VERTEX_WORK_BINS = 5 };
static struct { uint64_t calls, bytes, us; } xv_vertex_work_cost[XV_VERTEX_WORK_COUNT][XV_VERTEX_WORK_BINS];
static inline uint64_t xv_vertex_work_begin(void)
{ return xk_os_monotonic_us ? xk_os_monotonic_us() + 1 : 0; }
static inline void xv_vertex_work_end(enum xv_vertex_work kind, unsigned bytes, uint64_t begin)
{
    if (!begin || (unsigned)kind >= XV_VERTEX_WORK_COUNT) return;
    uint64_t now = xk_os_monotonic_us() + 1;
    if (now < begin) return;
    unsigned bin = bytes <= 4096 ? 0 : bytes <= 16384 ? 1 :
                   bytes <= 65536 ? 2 : bytes <= 262144 ? 3 : 4;
    xv_vertex_work_cost[kind][bin].calls++;
    xv_vertex_work_cost[kind][bin].bytes += bytes;
    xv_vertex_work_cost[kind][bin].us += now - begin;
}
static inline void xv_vertex_work_report(unsigned frames)
{
    static const char *const names[] = {"equal", "different", "snapshot", "dispatch"};
    static const char *const bins[] = {"0..4K", "4K..16K", "16K..64K", "64K..256K", ">256K"};
    if (!frames) return;
    for (unsigned k = 0; k < XV_VERTEX_WORK_COUNT; k++)
        for (unsigned b = 0; b < XV_VERTEX_WORK_BINS; b++)
            if (xv_vertex_work_cost[k][b].calls)
                xv_logf("[vertex-work] %u frames: %s %s calls %llu bytes %llu elapsed-us %llu (diagnostic caller time)\n",
                    frames, names[k], bins[b],
                    (unsigned long long)xv_vertex_work_cost[k][b].calls,
                    (unsigned long long)xv_vertex_work_cost[k][b].bytes,
                    (unsigned long long)xv_vertex_work_cost[k][b].us);
    memset(xv_vertex_work_cost, 0, sizeof xv_vertex_work_cost);
}
#else
static inline uint64_t xv_vertex_work_begin(void) { return 0; }
static inline void xv_vertex_work_end(enum xv_vertex_work k, unsigned b, uint64_t t)
{ (void)k; (void)b; (void)t; }
static inline void xv_vertex_work_report(unsigned frames) { (void)frames; }
#endif
#endif
