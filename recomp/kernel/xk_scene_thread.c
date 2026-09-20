/* xk_scene_thread.c - see xk_scene_thread.h. XV_SCENE_THREAD build flag, XV_SCENE_THREAD env (default
 * XV_SCENE_THREAD_DEFAULT). No overlap yet: gain 0, proves the scene half is thread-portable. */
#include "xk.h"
#include "xk_scene_thread.h"
#include "../xv_x86rt.h"
#include <stdlib.h>
#if defined(XV_SCENE_THREAD) && XV_SCENE_THREAD && defined(__vita__)
#include <psp2/kernel/threadmgr.h>
#ifndef XV_SCENE_THREAD_DEFAULT
#define XV_SCENE_THREAD_DEFAULT 0
#endif
extern void f_000BCB30(xctx *restrict c);
int xv_scene_helper_thread = -1, xv_scene_owner_alias = -1;   /* read by xv_owner_thread_id() */
static int enabled = -1, configured;
static SceUID helper = -1, go = -1, done = -1;
static xctx ctx;
static unsigned depth, dispatched, declined_nested;
static uint64_t wait_us, wait_max_us;

static int helper_main(SceSize args, void *argp)
{
    (void)args; (void)argp;
    for (;;) {
        if (sceKernelWaitSema(go, 1, NULL) < 0) return -1;
        f_000BCB30(&ctx);
        sceKernelSignalSema(done, 1);
    }
}
static void configure(void)
{
    configured = 1;
    const char *e = getenv("XV_SCENE_THREAD"); enabled = e ? atoi(e) != 0 : XV_SCENE_THREAD_DEFAULT;
    if (!enabled) { XK_LOG("[scene-thread] process-start disabled\n"); return; }
    go = sceKernelCreateSema("xv_scene_go", 0, 0, 1, NULL); done = sceKernelCreateSema("xv_scene_done", 0, 0, 1, NULL);
    helper = go >= 0 && done >= 0 ? sceKernelCreateThread("xv_scene", helper_main, sceKernelGetThreadCurrentPriority(), 1024 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_ALL, NULL) : -1;
    if (helper < 0 || sceKernelStartThread(helper, 0, NULL) < 0) { XK_LOG("[scene-thread] helper thread failed; disabled\n"); enabled = 0; return; }
    xv_scene_helper_thread = helper;
    XK_LOG("[scene-thread] process-start enabled: BCB30 runs on helper thread %08x, owner waits\n", (unsigned)helper);
}
int xv_scene_thread_run(void *context)
{
    if (!configured) configure();
    if (!enabled) return 0;
    if (sceKernelGetThreadId() == helper) return 0;             /* the helper's own entry: run the body */
    if (depth) { declined_nested++; return 0; }                  /* recursive scene entry on the owner */
    depth = 1;
    xctx *c = context;
    ctx = *c;
    xv_scene_owner_alias = sceKernelGetThreadId();
    uint64_t t0 = xk_os_monotonic_us();
    sceKernelSignalSema(go, 1);
    sceKernelWaitSema(done, 1, NULL);
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    *c = ctx;
    depth = 0; dispatched++;
    return 1;
}
void xv_scene_thread_report(unsigned frames)
{
    if (enabled <= 0) return;
    XK_LOG("[scene-thread] %u frames: dispatched %u, owner wait %.2f ms/frame (max %.1f ms), nested declines %u\n",
           frames, dispatched, dispatched ? (double)wait_us / dispatched / 1000.0 : 0.0, wait_max_us / 1000.0, declined_nested);
    dispatched = 0; wait_us = wait_max_us = 0; declined_nested = 0;
}
#else
int xv_scene_thread_run(void *context) { (void)context; return 0; }
void xv_scene_thread_report(unsigned frames) { (void)frames; }
#endif
