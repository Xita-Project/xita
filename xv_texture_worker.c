/* A single bounded job: the guest decodes one half while core 0 decodes the
 * other. No guest scheduler call, texture-cache mutation, allocation, GXM call,
 * or shared flush-queue access occurs on the worker. */
#include <stdlib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "xv_texture_worker.h"
#include "xv_cpu.h"
#include "xv_log.h"

#include "xv_gpu_upload.h"
static SceUID thread = -1, wake = -1, done = -1;
static int configured, enabled, stopping;
static struct {
    xv_texture_job image;
    unsigned first, last;
    int result;
    uint64_t work_us;
} job;
static uint64_t worker_us, caller_us, wait_us;
static unsigned jobs, pixels;

static int texture_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    xv_cpu_log_thread("texture-decode");
    for (;;) {
        if (sceKernelWaitSema(wake, 1, NULL) < 0) continue;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (stopping) return 0;
        uint64_t start = sceKernelGetProcessTimeWide();
        job.result = xv_tex_decode_range(&job.image, job.first, job.last);
        /* Complete stores into this worker's disjoint UNCACHED upload range. */
        xv_gpu_write_barrier();
        job.work_us = sceKernelGetProcessTimeWide() - start;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        sceKernelSignalSema(done, 1);
    }
}

static void delete_handles(void)
{
    if (thread >= 0) sceKernelDeleteThread(thread);
    if (wake >= 0) sceKernelDeleteSema(wake);
    if (done >= 0) sceKernelDeleteSema(done);
    thread = wake = done = -1;
}

static int start_worker(void)
{
    if (thread >= 0) return 1;
    wake = sceKernelCreateSema("xv_tex_wake", 0, 0, 1, NULL);
    done = sceKernelCreateSema("xv_tex_done", 0, 0, 1, NULL);
    if (wake < 0 || done < 0) goto fail;
    thread = sceKernelCreateThread("xv_texture", texture_thread,
        sceKernelGetThreadCurrentPriority(), 64 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    if (thread < 0 || sceKernelStartThread(thread, 0, NULL) < 0) goto fail;
    xv_logf("[cpu-work] texture worker enabled on core 0; conversions >= 16384 pixels split with guest\n");
    return 1;
fail:
    delete_handles();
    enabled = 0;
    xv_logf("[cpu-work] texture worker unavailable; using serial conversion\n");
    return 0;
}

int xv_texture_decode(const xv_texture_job *image)
{
    unsigned units = xv_tex_units(image);
    if (!configured) {
        const char *e = getenv("XV_TEXTURE_WORKER");
        enabled = !e || atoi(e) != 0;
        configured = 1;
    }
    /* Restrict splitting to large power-of-two images. Half-range boundaries
     * then separate full cache lines even for Morton order and transposition.
     * Tiny dummies/fonts stay serial and incur no thread wake or wait. */
    int split = enabled && image->w >= 32 && image->h >= 32 &&
        image->w * image->h >= (image->bc_reorder ? 65536u : 16384u) && !(image->w & (image->w - 1)) &&
        !(image->h & (image->h - 1)) && !((uintptr_t)image->dst & 63u);
    if (!split || !start_worker()) return xv_tex_decode_range(image, 0, units);
    job.image = *image; job.first = units / 2; job.last = units;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (sceKernelSignalSema(wake, 1) < 0) return xv_tex_decode_range(image, 0, units);
    uint64_t start = sceKernelGetProcessTimeWide();
    int result = xv_tex_decode_range(image, 0, units / 2);
    uint64_t waiting = sceKernelGetProcessTimeWide();
    /* Never release the job's pointers while a worker might still use them. */
    while (sceKernelWaitSema(done, 1, NULL) < 0) sceKernelDelayThread(200);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    wait_us += sceKernelGetProcessTimeWide() - waiting;
    caller_us += waiting - start; worker_us += job.work_us;
    jobs++; pixels += image->w * image->h;
    return result ? result : job.result;
}

void xv_texture_worker_report(void)
{
    if (!jobs) return;
    xv_logf("[cpu-work] textures %u / %u Kpixels: guest %.2f ms, worker %.2f ms, join %.2f ms (window totals)\n",
        jobs, pixels / 1024, caller_us / 1000.0, worker_us / 1000.0, wait_us / 1000.0);
    jobs = pixels = 0; caller_us = worker_us = wait_us = 0;
}

void xv_texture_worker_shutdown(void)
{
    if (thread >= 0) {
        stopping = 1;
        __atomic_thread_fence(__ATOMIC_RELEASE);
        sceKernelSignalSema(wake, 1);
        sceKernelWaitThreadEnd(thread, NULL, NULL);
    }
    delete_handles();
    stopping = configured = enabled = 0;
}
