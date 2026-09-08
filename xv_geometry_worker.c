#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "xv_geometry_sort.h"
#include "xv_geometry_worker.h"
#include "xv_cpu.h"
#include "xv_log.h"

typedef struct {
    SceUID thread, wake, done;
    unsigned core, count;
    int unavailable, stopping;
    int32_t *values;
    uint64_t us;
} sort_worker;
static sort_worker workers[2] = {{.thread=-1,.wake=-1,.done=-1,.core=0},
                                 {.thread=-1,.wake=-1,.done=-1,.core=1}};
static int configured, enabled;
static int32_t merged[32768];
static unsigned jobs[3], triangles;
static uint64_t worker_us[2], caller_us, wait_us;

static int geometry_thread(SceSize n, void *p)
{
    (void)n;
    sort_worker *w = *(sort_worker **)p;
    xv_cpu_log_thread(w->core ? "geometry-sort-1" : "geometry-sort-0");
    for (;;) {
        if (sceKernelWaitSema(w->wake, 1, NULL) < 0) continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (w->stopping) return 0;
        uint64_t start = sceKernelGetProcessTimeWide();
        xv_sort_triangles(w->values, w->count);
        w->us = sceKernelGetProcessTimeWide() - start;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        sceKernelSignalSema(w->done, 1);
    }
}
static void delete_handles(sort_worker *w)
{
    if (w->thread >= 0) sceKernelDeleteThread(w->thread);
    if (w->wake >= 0) sceKernelDeleteSema(w->wake);
    if (w->done >= 0) sceKernelDeleteSema(w->done);
    w->thread = w->wake = w->done = -1;
}
static int start_worker(sort_worker *w)
{
    if (w->thread >= 0) return 1;
    if (w->unavailable) return 0;
    w->wake = sceKernelCreateSema("xv_geo_wake", 0, 0, 1, NULL);
    w->done = sceKernelCreateSema("xv_geo_done", 0, 0, 1, NULL);
    if (w->wake < 0 || w->done < 0) goto fail;
    /* Keep the core-1 render pump ahead of auxiliary sort work. */
    int priority = sceKernelGetThreadCurrentPriority() + 1;
    w->thread = sceKernelCreateThread(w->core ? "xv_geometry1" : "xv_geometry0",
        geometry_thread, priority, 32 * 1024, 0,
        w->core ? SCE_KERNEL_CPU_MASK_USER_1 : SCE_KERNEL_CPU_MASK_USER_0, NULL);
    if (w->thread < 0 || sceKernelStartThread(w->thread, sizeof w, &w) < 0) goto fail;
    xv_logf("[cpu-work] geometry sort worker enabled on core %u\n", w->core);
    return 1;
fail:
    delete_handles(w); w->unavailable = 1;
    xv_logf("[cpu-work] geometry worker %u unavailable; remaining work stays with caller\n", w->core);
    return 0;
}
void xv_geometry_sort_parallel(int32_t *values, unsigned count)
{
    if (!configured) {
        const char *e = getenv("XV_GEOMETRY_WORKER");
        enabled = e ? atoi(e) : 2; configured = 1;
    }
    if (enabled <= 0 || count < 512 || count > 32768) {
        xv_sort_triangles(values, count); jobs[0]++; return;
    }
    sort_worker *active[2]; unsigned nw = 0;
    if (start_worker(&workers[0])) active[nw++] = &workers[0];
    if (enabled >= 2 && count >= 2048 && start_worker(&workers[1]))
        active[nw++] = &workers[1];
    if (!nw) { xv_sort_triangles(values, count); jobs[0]++; return; }
    unsigned cuts[4] = {0}, submitted[2] = {0};
    for (unsigned i = 1; i <= nw + 1; i++) cuts[i] = count * i / (nw + 1);
    for (unsigned i = 0; i < nw; i++) {
        sort_worker *w = active[i];
        w->values = values + cuts[i + 1]; w->count = cuts[i + 2] - cuts[i + 1];
        __atomic_thread_fence(__ATOMIC_RELEASE);
        submitted[i] = sceKernelSignalSema(w->wake, 1) >= 0;
    }
    uint64_t start = sceKernelGetProcessTimeWide();
    xv_sort_triangles(values, cuts[1]);
    for (unsigned i = 0; i < nw; i++) if (!submitted[i])
        xv_sort_triangles(active[i]->values, active[i]->count);
    uint64_t waiting = sceKernelGetProcessTimeWide();
    for (unsigned i = 0; i < nw; i++) if (submitted[i]) {
        sort_worker *w = active[i];
        while (sceKernelWaitSema(w->done, 1, NULL) < 0) sceKernelDelayThread(200);
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        worker_us[w->core] += w->us;
    }
    wait_us += sceKernelGetProcessTimeWide() - waiting;
    caller_us += waiting - start;
    xv_merge_triangles(values, cuts[1], cuts[2], merged);
    memcpy(values, merged, cuts[2] * sizeof *values);
    if (nw == 2) {
        xv_merge_triangles(values, cuts[2], count, merged);
        memcpy(values, merged, count * sizeof *values);
    }
    jobs[submitted[0] + (nw > 1 ? submitted[1] : 0)]++; triangles += count;
}
void xv_geometry_worker_report(void)
{
    if (jobs[0] || jobs[1] || jobs[2])
        xv_logf("[cpu-work] geometry lists %u serial / %u two-way / %u three-way, %u triangles: guest %.2f ms C0 %.2f ms C1 %.2f ms join %.2f ms (window totals; sort only)\n",
            jobs[0], jobs[1], jobs[2], triangles, caller_us / 1000.0,
            worker_us[0] / 1000.0, worker_us[1] / 1000.0, wait_us / 1000.0);
    memset(jobs, 0, sizeof jobs); triangles = 0;
    worker_us[0] = worker_us[1] = caller_us = wait_us = 0;
}
void xv_geometry_worker_shutdown(void)
{
    for (unsigned i = 0; i < 2; i++) {
        sort_worker *w = &workers[i];
        if (w->thread >= 0) {
            w->stopping = 1; __atomic_thread_fence(__ATOMIC_RELEASE);
            sceKernelSignalSema(w->wake, 1); sceKernelWaitThreadEnd(w->thread, NULL, NULL);
        }
        delete_handles(w); w->stopping = w->unavailable = 0;
    }
    configured = enabled = 0;
}
