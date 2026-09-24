/* XV_CACHE_PROBE: who enters the shared resource-cache functions (the allocator on the cache object at [2E2D2C], its
 * request array [2E2D24] and the file-request queue) while a scene is in flight. On the Xbox the tick and the scene
 * were one thread and the game never locks these; under the overlap they run on two. Entry hooks come from
 * tools/patch_cache_probe.py; the counts print with the 60-frame D3D report. Diagnostic only, no behaviour change. */
#include "xk.h"
#include <stdint.h>
#include <stdio.h>

extern int xv_scene_thread_on_helper(void);
extern int xv_scene_thread_present_policy(void);   /* nonzero while a scene is in flight */
extern int xv_scene_thread_overlapped(void);       /* the in-flight scene overlaps the tick */

#define PROBE_MAX 64
static uint32_t probe_fn[PROBE_MAX]; static unsigned probe_n;
static unsigned probe_helper[PROBE_MAX], probe_helper_ov[PROBE_MAX], probe_owner_inflight[PROBE_MAX], probe_owner_idle[PROBE_MAX];
static unsigned probe_total_helper_ov[PROBE_MAX], probe_total_inflight[PROBE_MAX];

static int probe_slot(uint32_t fn)
{
    unsigned n = __atomic_load_n(&probe_n, __ATOMIC_ACQUIRE);
    for (unsigned i = 0; i < n; ++i) if (probe_fn[i] == fn) return (int)i;
    static volatile int lock;
    while (__atomic_exchange_n(&lock, 1, __ATOMIC_ACQUIRE)) { }
    n = probe_n; int r = -1;
    for (unsigned i = 0; i < n; ++i) if (probe_fn[i] == fn) { r = (int)i; break; }
    if (r < 0 && n < PROBE_MAX) { probe_fn[n] = fn; __atomic_store_n(&probe_n, n + 1, __ATOMIC_RELEASE); r = (int)n; }
    __atomic_store_n(&lock, 0, __ATOMIC_RELEASE);
    return r;
}

void xv_cache_probe(uint32_t fn)
{
    int i = probe_slot(fn); if (i < 0) return;
    if (xv_scene_thread_on_helper()) __atomic_fetch_add(xv_scene_thread_overlapped() ? &probe_helper_ov[i] : &probe_helper[i], 1u, __ATOMIC_RELAXED);
    else if (xv_scene_thread_present_policy()) __atomic_fetch_add(&probe_owner_inflight[i], 1u, __ATOMIC_RELAXED);
    else __atomic_fetch_add(&probe_owner_idle[i], 1u, __ATOMIC_RELAXED);
}

void xv_cache_probe_report(unsigned frames)
{
    char line[1100]; int ln = snprintf(line, sizeof line, "[cache-probe] %u frames, fn helper-overlapped/helper-other/owner-in-flight/owner-idle:", frames);
    unsigned n = __atomic_load_n(&probe_n, __ATOMIC_ACQUIRE), shown = 0;
    for (unsigned i = 0; i < n && ln < (int)sizeof line - 48; ++i) {
        unsigned hv = __atomic_exchange_n(&probe_helper_ov[i], 0u, __ATOMIC_RELAXED);
        unsigned h = __atomic_exchange_n(&probe_helper[i], 0u, __ATOMIC_RELAXED);
        unsigned o = __atomic_exchange_n(&probe_owner_inflight[i], 0u, __ATOMIC_RELAXED);
        unsigned d = __atomic_exchange_n(&probe_owner_idle[i], 0u, __ATOMIC_RELAXED);
        probe_total_helper_ov[i] += hv; probe_total_inflight[i] += o;
        if (hv | h | o | d) { ln += snprintf(line + ln, sizeof line - ln, " %X:%u/%u/%u/%u", probe_fn[i], hv, h, o, d); shown++; }
    }
    if (shown) XK_LOG("%s\n", line);
    /* running totals of the two concurrent classes, so a short log still answers "does the scene ever call X while the tick can" */
    ln = snprintf(line, sizeof line, "[cache-probe] totals helper-overlapped/owner-in-flight:");
    for (unsigned i = 0; i < n && ln < (int)sizeof line - 40; ++i)
        if (probe_total_helper_ov[i] | probe_total_inflight[i]) ln += snprintf(line + ln, sizeof line - ln, " %X:%u/%u", probe_fn[i], probe_total_helper_ov[i], probe_total_inflight[i]);
    XK_LOG("%s\n", line);
}
