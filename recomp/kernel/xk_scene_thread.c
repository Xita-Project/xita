/* xk_scene_thread.c - see xk_scene_thread.h. XV_SCENE_THREAD build flag, XV_SCENE_THREAD env (default
 * XV_SCENE_THREAD_DEFAULT). No overlap yet: gain 0, proves the scene half is thread-portable. */
#include "xk.h"
#include "xk_scene_thread.h"
#include "../xv_x86rt.h"
#include <stdlib.h>
#include <stdio.h>

/* Scene-yield census (both ports): every scheduler handoff taken while the scene body runs on the helper is
 * counted by the guest return address of the kernel call and whether the thread was blocking (state 1) or
 * merely yielding (SwitchToThread/Sleep(0)); the report lists the top sites. */
#define SCENE_SITES 24
static struct { uint32_t eip, caller; unsigned n_block, n_yield; } scene_sites[SCENE_SITES]; static unsigned scene_nsites, scene_site_overflow;
void xv_scene_thread_yield_census(uint32_t eip, uint32_t caller, int blocking)
{
    for (unsigned i = 0; i < scene_nsites; ++i) if (scene_sites[i].eip == eip && scene_sites[i].caller == caller) { if (blocking) scene_sites[i].n_block++; else scene_sites[i].n_yield++; return; }
    if (scene_nsites == SCENE_SITES) { scene_site_overflow++; return; }
    scene_sites[scene_nsites].eip = eip; scene_sites[scene_nsites].caller = caller; scene_sites[scene_nsites].n_block = blocking; scene_sites[scene_nsites].n_yield = !blocking; scene_nsites++;
}
static void scene_census_report(void)
{
    if (!scene_nsites) return;
    for (unsigned i = 0; i < scene_nsites; ++i) for (unsigned j = i + 1; j < scene_nsites; ++j)
        if (scene_sites[j].n_block + scene_sites[j].n_yield > scene_sites[i].n_block + scene_sites[i].n_yield) { typeof(scene_sites[0]) t = scene_sites[i]; scene_sites[i] = scene_sites[j]; scene_sites[j] = t; }
    char line[300]; int ln = snprintf(line, sizeof line, "[scene-yields] sites (wrapper-ret<-caller block/yield):");
    for (unsigned i = 0; i < scene_nsites && i < 10; ++i) ln += snprintf(line + ln, sizeof line - ln, " %X<-%X %u/%u", scene_sites[i].eip, scene_sites[i].caller, scene_sites[i].n_block, scene_sites[i].n_yield);
    if (scene_site_overflow) ln += snprintf(line + ln, sizeof line - ln, " (+%u unlisted)", scene_site_overflow);
    XK_LOG("%s\n", line); scene_nsites = 0; scene_site_overflow = 0;
}
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
static xk_thread *scene_guest;
int xv_scene_thread_active(const void *guest_thread) { return depth && guest_thread == scene_guest && sceKernelGetThreadId() == helper; }

/* ---- increment C (XV_SCENE_OVERLAP=1): the owner continues after dispatch ------------------------------
 * The helper runs the body without ever entering the guest scheduler (xv_preempt and xk_yield return at
 * once on it: the census showed the scene's own thread takes only preemption yields, never a wait), the
 * owner emulates the body's `ret 8` and goes on; the next dispatch and the Present HLE join the scene in
 * flight (backpressure). The owner-side D3D census lists the HLE calls made outside the scene. */
#define SCENE_STACK_BYTES (256u * 1024u)
static uint32_t scene_stack;   /* private guest stack for the overlapped body (see dispatch) */
static int overlap; static unsigned in_flight, overlaps, joins_present, joins_dispatch, suppressed_yields, suppressed_waits; static uint32_t suppressed_eip;
#define OWNER_D3D 48
static struct { const char *name; unsigned n; } owner_d3d[OWNER_D3D]; static unsigned owner_d3d_n, owner_d3d_over;
int xv_scene_thread_on_helper(void);
void xv_scene_thread_d3d_call(const char *name)
{
    if (enabled <= 0 || xv_scene_thread_on_helper()) return;
    for (unsigned i = 0; i < owner_d3d_n; ++i) if (owner_d3d[i].name == name) { owner_d3d[i].n++; return; }
    if (owner_d3d_n < OWNER_D3D) { owner_d3d[owner_d3d_n].name = name; owner_d3d[owner_d3d_n].n = 1; owner_d3d_n++; } else owner_d3d_over++;
}
int xv_scene_thread_no_yield(void) { return overlap && xv_scene_thread_on_helper(); }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { suppressed_yields++; if (blocking) { suppressed_waits++; suppressed_eip = eip; } }
static void overlap_report(void)
{
    if (overlap) XK_LOG("[scene-overlap] dispatched-without-wait %u, joins at present %u / next dispatch %u, suppressed helper yields %u (blocking %u, last eip %X)\n",
                        overlaps, joins_present, joins_dispatch, suppressed_yields, suppressed_waits, suppressed_eip);
    overlaps = joins_present = joins_dispatch = suppressed_yields = suppressed_waits = 0;
    if (owner_d3d_n) {
        char line[400]; int ln = snprintf(line, sizeof line, "[scene-owner-d3d] owner-side calls:");
        for (unsigned i = 0; i < owner_d3d_n; ++i) { if (ln > 330) { XK_LOG("%s\n", line); ln = snprintf(line, sizeof line, "[scene-owner-d3d]  "); } ln += snprintf(line + ln, sizeof line - ln, " %s %u", owner_d3d[i].name, owner_d3d[i].n); }
        if (owner_d3d_over) ln += snprintf(line + ln, sizeof line - ln, " (+%u unlisted)", owner_d3d_over);
        XK_LOG("%s\n", line); owner_d3d_n = 0; owner_d3d_over = 0;
    }
}
int xv_scene_thread_on_helper(void) { return helper >= 0 && sceKernelGetThreadId() == helper; }
static void join(unsigned *counter)
{
    if (!in_flight) return;
    uint64_t t0 = xk_os_monotonic_us();
    sceKernelWaitSema(done, 1, NULL);
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    in_flight = 0; depth = 0; (*counter)++;
}
void xv_scene_thread_join(void) { if (enabled > 0 && overlap) join(&joins_present); }

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
    { const char *o = getenv("XV_SCENE_OVERLAP"); overlap = o ? atoi(o) != 0 : 0; }
    if (overlap) { scene_stack = xk_mem_alloc(SCENE_STACK_BYTES, 4096, 0, 0, 1); if (!scene_stack) { XK_LOG("[scene-thread] no guest stack for the overlap; overlap off\n"); overlap = 0; } }
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
    if (overlap && in_flight) join(&joins_dispatch);             /* backpressure: scene N must finish before scene N+1 */
    if (depth) { declined_nested++; return 0; }                  /* recursive scene entry on the owner */
    depth = 1;
    xctx *c = context;
    ctx = *c; scene_guest = xk_cur;
    xv_scene_owner_alias = sceKernelGetThreadId();
    uint64_t t0 = xk_os_monotonic_us();
    if (overlap) { uint32_t top = scene_stack + SCENE_STACK_BYTES - 64u; for (unsigned i = 0; i < 4; ++i) X_W32(top + 4u * i) = X_M32(c->r[4] + 4u * i); ctx.r[4] = top; }   /* body frame on the private stack: return address + 8-byte argument copied */
    sceKernelSignalSema(go, 1);
    if (overlap) { in_flight = 1; overlaps++; dispatched++; c->r[4] += 12; return 1; }   /* the body's `ret 8`: the owner continues */
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
    scene_census_report(); overlap_report();
}
#elif defined(XV_SCENE_THREAD) && XV_SCENE_THREAD
/* Host (Linux) version of the same mechanism: a pthread helper and two POSIX semaphores. Host fibers are
 * ucontext on one thread, so while the helper runs the scene it becomes the single runner: a yield inside
 * the scene swaps contexts on the helper thread and the owner stays parked in sem_wait on thread 8's stack
 * until the body returns (the Vita behaviour: the helper *is* thread 8 for the scene's duration). Host owner
 * checks call xv_owner_pthread_self(), which maps the helper to the owner while the scene runs. */
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#ifndef XV_SCENE_THREAD_DEFAULT
#define XV_SCENE_THREAD_DEFAULT 0
#endif
extern void f_000BCB30(xctx *restrict c);
static int enabled = -1, configured, helper_valid;
static pthread_t helper, owner_alias;
static sem_t go, done;
static xctx ctx;
static unsigned depth, dispatched, declined_nested;
static uint64_t wait_us, wait_max_us;
static xk_thread *scene_guest;
int xv_scene_thread_active(const void *guest_thread) { return helper_valid && __atomic_load_n(&depth, __ATOMIC_ACQUIRE) && guest_thread == scene_guest && pthread_equal(pthread_self(), helper); }

/* ---- increment C (XV_SCENE_OVERLAP=1): the owner continues after dispatch ------------------------------
 * The helper runs the body without ever entering the guest scheduler (xv_preempt and xk_yield return at
 * once on it: the census showed the scene's own thread takes only preemption yields, never a wait), the
 * owner emulates the body's `ret 8` and goes on; the next dispatch and the Present HLE join the scene in
 * flight (backpressure). The owner-side D3D census lists the HLE calls made outside the scene. */
#define SCENE_STACK_BYTES (256u * 1024u)
static uint32_t scene_stack;   /* private guest stack for the overlapped body (see dispatch) */
static int overlap; static unsigned in_flight, overlaps, joins_present, joins_dispatch, suppressed_yields, suppressed_waits; static uint32_t suppressed_eip;
#define OWNER_D3D 48
static struct { const char *name; unsigned n; } owner_d3d[OWNER_D3D]; static unsigned owner_d3d_n, owner_d3d_over;
int xv_scene_thread_on_helper(void);
void xv_scene_thread_d3d_call(const char *name)
{
    if (enabled <= 0 || xv_scene_thread_on_helper()) return;
    for (unsigned i = 0; i < owner_d3d_n; ++i) if (owner_d3d[i].name == name) { owner_d3d[i].n++; return; }
    if (owner_d3d_n < OWNER_D3D) { owner_d3d[owner_d3d_n].name = name; owner_d3d[owner_d3d_n].n = 1; owner_d3d_n++; } else owner_d3d_over++;
}
int xv_scene_thread_no_yield(void) { return overlap && xv_scene_thread_on_helper(); }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { suppressed_yields++; if (blocking) { suppressed_waits++; suppressed_eip = eip; } }
static void overlap_report(void)
{
    if (overlap) XK_LOG("[scene-overlap] dispatched-without-wait %u, joins at present %u / next dispatch %u, suppressed helper yields %u (blocking %u, last eip %X)\n",
                        overlaps, joins_present, joins_dispatch, suppressed_yields, suppressed_waits, suppressed_eip);
    overlaps = joins_present = joins_dispatch = suppressed_yields = suppressed_waits = 0;
    if (owner_d3d_n) {
        char line[400]; int ln = snprintf(line, sizeof line, "[scene-owner-d3d] owner-side calls:");
        for (unsigned i = 0; i < owner_d3d_n; ++i) { if (ln > 330) { XK_LOG("%s\n", line); ln = snprintf(line, sizeof line, "[scene-owner-d3d]  "); } ln += snprintf(line + ln, sizeof line - ln, " %s %u", owner_d3d[i].name, owner_d3d[i].n); }
        if (owner_d3d_over) ln += snprintf(line + ln, sizeof line - ln, " (+%u unlisted)", owner_d3d_over);
        XK_LOG("%s\n", line); owner_d3d_n = 0; owner_d3d_over = 0;
    }
}
int xv_scene_thread_on_helper(void) { return helper_valid && pthread_equal(pthread_self(), helper); }
static void join(unsigned *counter)
{
    if (!in_flight) return;
    uint64_t t0 = xk_os_monotonic_us();
    while (sem_wait(&done) < 0 && errno == EINTR) {}
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    in_flight = 0; __atomic_store_n(&depth, 0, __ATOMIC_RELEASE); (*counter)++;
}
void xv_scene_thread_join(void) { if (enabled > 0 && overlap) join(&joins_present); }

pthread_t xv_owner_pthread_self(void)
{
    pthread_t me = pthread_self();
    if (helper_valid && __atomic_load_n(&depth, __ATOMIC_ACQUIRE) && pthread_equal(me, helper)) return owner_alias;
    return me;
}
static void *helper_main(void *arg)
{
    (void)arg;
    for (;;) {
        while (sem_wait(&go) < 0 && errno == EINTR) {}
        f_000BCB30(&ctx);
        sem_post(&done);
    }
    return 0;
}
static void configure(void)
{
    configured = 1;
    const char *e = getenv("XV_SCENE_THREAD"); enabled = e ? atoi(e) != 0 : XV_SCENE_THREAD_DEFAULT;
    { const char *o = getenv("XV_SCENE_OVERLAP"); overlap = o ? atoi(o) != 0 : 0; }
    if (overlap) { scene_stack = xk_mem_alloc(SCENE_STACK_BYTES, 4096, 0, 0, 1); if (!scene_stack) { XK_LOG("[scene-thread] no guest stack for the overlap; overlap off\n"); overlap = 0; } }
    if (!enabled) { XK_LOG("[scene-thread] process-start disabled\n"); return; }
    if (sem_init(&go, 0, 0) || sem_init(&done, 0, 0) || pthread_create(&helper, NULL, helper_main, NULL)) { XK_LOG("[scene-thread] helper thread failed; disabled\n"); enabled = 0; return; }
    helper_valid = 1;
    XK_LOG("[scene-thread] process-start enabled (host pthread): BCB30 runs on a helper thread, owner waits\n");
}
int xv_scene_thread_run(void *context)
{
    if (!configured) configure();
    if (!enabled) return 0;
    if (pthread_equal(pthread_self(), helper)) return 0;         /* the helper's own entry: run the body */
    if (overlap && in_flight) join(&joins_dispatch);             /* backpressure: scene N must finish before scene N+1 */
    if (depth) { declined_nested++; return 0; }                  /* recursive scene entry on the owner */
    xctx *c = context;
    ctx = *c; scene_guest = xk_cur;
    owner_alias = pthread_self();
    __atomic_store_n(&depth, 1, __ATOMIC_RELEASE);
    uint64_t t0 = xk_os_monotonic_us();
    if (overlap) { uint32_t top = scene_stack + SCENE_STACK_BYTES - 64u; for (unsigned i = 0; i < 4; ++i) X_W32(top + 4u * i) = X_M32(c->r[4] + 4u * i); ctx.r[4] = top; }   /* body frame on the private stack: return address + 8-byte argument copied */
    sem_post(&go);
    if (overlap) { in_flight = 1; overlaps++; dispatched++; c->r[4] += 12; return 1; }   /* the body's `ret 8`: the owner continues */
    while (sem_wait(&done) < 0 && errno == EINTR) {}
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    *c = ctx;
    __atomic_store_n(&depth, 0, __ATOMIC_RELEASE); dispatched++;
    return 1;
}
void xv_scene_thread_report(unsigned frames)
{
    if (enabled <= 0) return;
    XK_LOG("[scene-thread] %u frames: dispatched %u, owner wait %.2f ms/frame (max %.1f ms), nested declines %u\n",
           frames, dispatched, dispatched ? (double)wait_us / dispatched / 1000.0 : 0.0, wait_max_us / 1000.0, declined_nested);
    dispatched = 0; wait_us = wait_max_us = 0; declined_nested = 0;
    scene_census_report(); overlap_report();
}
#else
int xv_scene_thread_active(const void *guest_thread) { (void)guest_thread; return 0; }
int xv_scene_thread_on_helper(void) { return 0; }
int xv_scene_thread_no_yield(void) { return 0; }
void xv_scene_thread_join(void) {}
void xv_scene_thread_d3d_call(const char *name) { (void)name; }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { (void)eip; (void)blocking; }
int xv_scene_thread_run(void *context) { (void)context; return 0; }
void xv_scene_thread_report(unsigned frames) { (void)frames; }
#if !defined(__vita__)
#include <pthread.h>
pthread_t xv_owner_pthread_self(void) { return pthread_self(); }   /* no helper: identity */
#endif
#endif
