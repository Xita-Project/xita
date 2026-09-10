#include "xv_upload_worker.h"
#include "xv_gpu_upload.h"
#include "xv_cpu.h"
#include "xv_log.h"
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>

#define UPLOAD_JOBS 64u
typedef struct { void *dst; const void *src; unsigned bytes; } upload_job;
static upload_job queue[UPLOAD_JOBS];
static SceUID thread = -1, wake = -1;
static int unavailable, stopping;
static uint32_t submitted, completed; /* producer-owned / atomic worker-owned */
static unsigned queued, fallback;
static uint32_t work_us, work_bytes, work_jobs, waits, wait_us;

static int upload_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    xv_cpu_log_thread("vertex-upload");
    uint32_t head = __atomic_load_n(&completed, __ATOMIC_ACQUIRE);
    for (;;) {
        if (sceKernelWaitSema(wake, 1, NULL) < 0) continue;
        if (__atomic_load_n(&stopping, __ATOMIC_ACQUIRE)) return 0;
        /* The semaphore publishes one descriptor. Copy it locally before
         * releasing its ring position; completion follows all GPU stores. */
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        upload_job job = queue[head & (UPLOAD_JOBS - 1u)];
        uint64_t begin = sceKernelGetProcessTimeWide();
        memcpy(job.dst, job.src, job.bytes);
        xv_gpu_write_barrier(); /* destination is USER_RW_UNCACHE */
        __atomic_fetch_add(&work_us, (uint32_t)(sceKernelGetProcessTimeWide()-begin), __ATOMIC_RELAXED);
        __atomic_fetch_add(&work_bytes, job.bytes, __ATOMIC_RELAXED);
        __atomic_fetch_add(&work_jobs, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&completed, ++head, __ATOMIC_RELEASE);
    }
}
static int start_worker(void)
{
    if (thread >= 0) return 1;
    if (unavailable) return 0;
    wake = sceKernelCreateSema("xv_upload_wake", 0, 0, UPLOAD_JOBS, NULL);
    if (wake < 0) goto fail;
    thread = sceKernelCreateThread("xv_vertex_upload", upload_thread,
        sceKernelGetThreadCurrentPriority()+1, 32*1024, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0) goto fail;
    xv_logf("[cpu-work] vertex upload worker enabled on core 0; immutable snapshots, bounded queue\n");
    return 1;
fail:
    if (thread >= 0) sceKernelDeleteThread(thread);
    if (wake >= 0) sceKernelDeleteSema(wake);
    thread = wake = -1; unavailable = 1;
    xv_logf("[cpu-work] vertex upload worker unavailable; copies stay on caller\n");
    return 0;
}
int xv_upload_worker_submit(void *dst, const void *src, unsigned bytes, uint32_t *ticket)
{
    if (!dst || !src || !bytes || !ticket) return 0;
    if (!start_worker() || submitted-__atomic_load_n(&completed, __ATOMIC_ACQUIRE)>=UPLOAD_JOBS) {
        fallback++; return 0; /* caller copies this disjoint range without waiting */
    }
    queue[submitted & (UPLOAD_JOBS-1u)] = (upload_job){dst,src,bytes};
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* Exactly one wake per job. Failed dispatch leaves no published ticket. */
    if (sceKernelSignalSema(wake,1)<0) { fallback++; return 0; }
    *ticket = ++submitted; queued++;
    return 1;
}
void xv_upload_worker_wait(uint32_t ticket)
{
    if ((int32_t)(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)-ticket)>=0) return;
    uint64_t begin=sceKernelGetProcessTimeWide();
    do { sceKernelDelayThread(100); }
    while ((int32_t)(__atomic_load_n(&completed,__ATOMIC_ACQUIRE)-ticket)<0);
    __atomic_fetch_add(&waits,1u,__ATOMIC_RELAXED);
    __atomic_fetch_add(&wait_us,(uint32_t)(sceKernelGetProcessTimeWide()-begin),__ATOMIC_RELAXED);
}
void xv_upload_worker_report(unsigned frames)
{
    uint32_t jobs=__atomic_exchange_n(&work_jobs,0,__ATOMIC_RELAXED);
    uint32_t bytes=__atomic_exchange_n(&work_bytes,0,__ATOMIC_RELAXED);
    uint32_t us=__atomic_exchange_n(&work_us,0,__ATOMIC_RELAXED);
    uint32_t nwait=__atomic_exchange_n(&waits,0,__ATOMIC_RELAXED);
    uint32_t wait=__atomic_exchange_n(&wait_us,0,__ATOMIC_RELAXED);
    if (queued || fallback || jobs || nwait)
        xv_logf("[vertex-worker] %u frames: %u queued / %u caller fallbacks; C0 %u batches %u KiB %.3f ms; completion waits %u %.3f ms (overlapping window totals)\n",
            frames,queued,fallback,jobs,bytes>>10,us/1000.0,nwait,wait/1000.0);
    queued=fallback=0;
}
void xv_upload_worker_shutdown(void)
{
    if (thread >= 0) {
        xv_upload_worker_wait(submitted);
        __atomic_store_n(&stopping,1,__ATOMIC_RELEASE);
        while (sceKernelSignalSema(wake,1)<0) sceKernelDelayThread(100);
        sceKernelWaitThreadEnd(thread,NULL,NULL);
        sceKernelDeleteThread(thread); sceKernelDeleteSema(wake);
    }
    thread=wake=-1; unavailable=stopping=0; submitted=completed=0;
    queued=fallback=work_us=work_bytes=work_jobs=waits=wait_us=0;
}
