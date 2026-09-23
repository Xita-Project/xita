/* xk_scene_thread.c - see xk_scene_thread.h. XV_SCENE_THREAD build flag, XV_SCENE_THREAD env (default
 * XV_SCENE_THREAD_DEFAULT). No overlap yet: gain 0, proves the scene half is thread-portable. */
#include "xk.h"
#include "xk_scene_thread.h"
#include "../xv_x86rt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
/* A fatal guest trap on the helper (Pi yp2 20:37: idiv by [edi+34h] with edi=0x40 in f_001105E0, a torn/NULL object
 * pointer) parked the helper in xv_trap's yield loop forever: the scene never finished, the game hung with nothing
 * proxied. The helper now unwinds to its loop and finishes the frame as abandoned (the owner's join proceeds, the
 * frame is dropped); xv_trap calls xv_scene_thread_abandon() when on the helper. Guest state the scene had half
 * written stays as it is - a dropped frame, not a hang. */
static jmp_buf scene_abandon_jmp; static volatile int scene_abandon_armed; static unsigned scene_abandons;
int xv_scene_thread_on_helper(void);
void xv_scene_thread_abandon(uint32_t eip)
{
    if (!scene_abandon_armed || !xv_scene_thread_on_helper()) return;
    scene_abandons++;
    XK_LOG("[scene-thread] ABANDON scene %u: guest trap at %08X on the helper; frame dropped, helper unwound\n", scene_abandons, eip);
    longjmp(scene_abandon_jmp, 1);
}

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
/* Scene phase timers (XV_SCENE_PHASES=1, both builds): wall time of the direct guest callees of the scene entry
 * f_000BCB30 and of its main callee f_0005DBC0, on the thread that runs the scene, installed by
 * tools/patch_scene_phase_timers.py (a t0 stack handles the nesting; a callee's time includes its own callees).
 * The host sampler says the scene is flat per function; this splits the Vita's ~65 ms scene into its phases. */
#include <stdlib.h>
#define PHASE_MAX 128
/* Two sets: [0] the owner's thread (tick side, the frame loop f_000BD420's callees), [1] the scene helper. The threads
 * run concurrently under the overlap; a shared t0 stack interleaved their timings. */
int xv_scene_thread_on_helper(void);
static struct { uint32_t parent, addr; uint64_t us; unsigned n; } phase_tab[2][PHASE_MAX]; static unsigned phase_used[2];
static uint64_t phase_t0[2][16]; static uint32_t phase_addr[2][16]; static unsigned phase_depth[2]; static int phases = -1;
static const char *phase_tag[2] = { "[tick-phases]", "[scene-phases]" };
/* Keyed by (parent, callee): the parent is the innermost timed call on this thread's stack, so a parent's self time
 * is its own inclusive time minus its direct timed children. The tool passes the callee address at begin. */
void xv_scene_phase_begin(uint32_t addr)
{
    if (phases < 0) { const char *e = getenv("XV_SCENE_PHASES"); phases = e ? atoi(e) : 0; }
    if (phases <= 0) return;
    unsigned t = xv_scene_thread_on_helper() ? 1 : 0;
    if (phase_depth[t] < 16) { phase_addr[t][phase_depth[t]] = addr; phase_t0[t][phase_depth[t]++] = xk_os_monotonic_us(); }
}
void xv_scene_phase_end(uint32_t addr)
{
    if (phases <= 0) return;
    unsigned t = xv_scene_thread_on_helper() ? 1 : 0;
    if (!phase_depth[t]) return;
    uint64_t dt = xk_os_monotonic_us() - phase_t0[t][--phase_depth[t]];
    uint32_t parent = phase_depth[t] ? phase_addr[t][phase_depth[t] - 1] : 0; unsigned i;
    for (i = 0; i < phase_used[t]; ++i) if (phase_tab[t][i].addr == addr && phase_tab[t][i].parent == parent) break;
    if (i == phase_used[t]) { if (phase_used[t] >= PHASE_MAX) return; phase_tab[t][phase_used[t]].parent = parent; phase_tab[t][phase_used[t]++].addr = addr; }
    phase_tab[t][i].us += dt; phase_tab[t][i].n++;
}
static void phase_report(unsigned frames)
{
    if (phases <= 0 || !frames) return;
    for (unsigned t = 0; t < 2; ++t) {
        if (!phase_used[t]) continue;
        /* inclusive per callee (summed over parents), then per parent: inclusive, self, top children */
        char line[400]; int ln = snprintf(line, sizeof line, "%s %u frames (ms/frame, calls; inclusive by callee):", phase_tag[t], frames);
        for (unsigned k = 0; k < 30 && k < phase_used[t]; ++k) {
            unsigned best = k; for (unsigned i = k + 1; i < phase_used[t]; ++i) if (phase_tab[t][i].us > phase_tab[t][best].us) best = i;
            if (best != k) { __typeof__(phase_tab[0][0]) x = phase_tab[t][k]; phase_tab[t][k] = phase_tab[t][best]; phase_tab[t][best] = x; }
            if (!phase_tab[t][k].us) break;
            if (ln > 320) { XK_LOG("%s\n", line); ln = snprintf(line, sizeof line, "%s  ", phase_tag[t]); }
            ln += snprintf(line + ln, sizeof line - ln, " %X %.2f (%u)", phase_tab[t][k].addr, (double)phase_tab[t][k].us / frames / 1000.0, phase_tab[t][k].n);
        }
        XK_LOG("%s\n", line);
        /* self time: for every parent that is itself a timed callee, inclusive - sum(children) */
        for (unsigned p = 0; p < phase_used[t]; ++p) {
            uint32_t P = phase_tab[t][p].addr; uint64_t children = 0; unsigned nchild = 0;
            for (unsigned i = 0; i < phase_used[t]; ++i) if (phase_tab[t][i].parent == P) { children += phase_tab[t][i].us; nchild++; }
            if (!nchild || phase_tab[t][p].us < 500u * frames) continue;   /* parents worth 0.5 ms/frame or more */
            int dup = 0; for (unsigned q = 0; q < p; ++q) if (phase_tab[t][q].addr == P) dup = 1; if (dup) continue;
            ln = snprintf(line, sizeof line, "%s   %X incl %.2f self %.2f (%u children):", phase_tag[t], P, (double)phase_tab[t][p].us / frames / 1000.0,
                          (double)(phase_tab[t][p].us > children ? phase_tab[t][p].us - children : 0) / frames / 1000.0, nchild);
            for (unsigned c = 0; c < 8; ++c) {   /* the 8 biggest children */
                unsigned best = PHASE_MAX; for (unsigned i = 0; i < phase_used[t]; ++i) if (phase_tab[t][i].parent == P && phase_tab[t][i].n && (best == PHASE_MAX || phase_tab[t][i].us > phase_tab[t][best].us)) best = i;
                if (best == PHASE_MAX || !phase_tab[t][best].us) break;
                ln += snprintf(line + ln, sizeof line - ln, " %X %.2f", phase_tab[t][best].addr, (double)phase_tab[t][best].us / frames / 1000.0);
                phase_tab[t][best].n = 0;   /* consumed for this listing; counts are reset below anyway */
            }
            XK_LOG("%s\n", line);
        }
        for (unsigned i = 0; i < phase_used[t]; ++i) { phase_tab[t][i].us = 0; phase_tab[t][i].n = 0; }
    }
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
int xv_scene_thread_on_helper(void);
int xv_scene_thread_active(const void *guest_thread) { return depth && guest_thread == scene_guest && xv_scene_thread_on_helper(); }

/* ---- increment C (XV_SCENE_OVERLAP=1): the owner continues after dispatch ------------------------------
 * The helper runs the body without ever entering the guest scheduler (xv_preempt and xk_yield return at
 * once on it: the census showed the scene's own thread takes only preemption yields, never a wait), the
 * owner emulates the body's `ret 8` and goes on; the next dispatch and the Present HLE join the scene in
 * flight (backpressure). The owner-side D3D census lists the HLE calls made outside the scene. */
#define SCENE_STACK_BYTES (256u * 1024u)
static uint32_t scene_stack;   /* private guest stack for the overlapped body (see dispatch) */
static int overlap, in_flight_overlapped; static unsigned owner_blocked_services, in_flight, overlaps, joins_present, joins_dispatch, joins_d3d, proxy_waits, suppressed_yields, suppressed_waits; static uint32_t suppressed_eip;
#define OWNER_D3D 48
static struct { const char *name; unsigned n; } owner_d3d[OWNER_D3D]; static unsigned owner_d3d_n, owner_d3d_over;
int xv_scene_thread_on_helper(void);
void xv_scene_thread_d3d_call(const char *name)
{
    if (enabled <= 0 || xv_scene_thread_on_helper()) return;
    for (unsigned i = 0; i < owner_d3d_n; ++i) if (owner_d3d[i].name == name) { owner_d3d[i].n++; return; }
    if (owner_d3d_n < OWNER_D3D) { owner_d3d[owner_d3d_n].name = name; owner_d3d[owner_d3d_n].n = 1; owner_d3d_n++; } else owner_d3d_over++;
}
static int gameplay_active(void) { uint32_t gg = X_M32(0x2F8CA0u); return gg && X_M8(gg) && X_M8(gg + 1u); }   /* game_globals: loaded, active */
int xv_scene_thread_no_yield(void) { return in_flight_overlapped && xv_scene_thread_on_helper(); }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { suppressed_yields++; if (blocking) { suppressed_waits++; suppressed_eip = eip; } }
static void overlap_report(void)
{
    { extern unsigned xv_log_helper_dropped(void) __attribute__((weak)); if (xv_log_helper_dropped) { unsigned d = xv_log_helper_dropped(); if (d) XK_LOG("[scene-thread] helper log lines dropped: %u\n", d); } }
    if (overlap) XK_LOG("[scene-overlap] dispatched-without-wait %u, joins at present %u / next dispatch %u / owner d3d %u, proxy waits %u, suppressed helper yields %u (blocking %u, last eip %X)\n",
                        overlaps, joins_present, joins_dispatch, joins_d3d, proxy_waits, suppressed_yields, suppressed_waits, suppressed_eip);
    overlaps = joins_present = joins_dispatch = joins_d3d = proxy_waits = suppressed_yields = suppressed_waits = 0;
    if (owner_d3d_n) {
        char line[400]; int ln = snprintf(line, sizeof line, "[scene-owner-d3d] owner-side calls:");
        for (unsigned i = 0; i < owner_d3d_n; ++i) { if (ln > 330) { XK_LOG("%s\n", line); ln = snprintf(line, sizeof line, "[scene-owner-d3d]  "); } ln += snprintf(line + ln, sizeof line - ln, " %s %u", owner_d3d[i].name, owner_d3d[i].n); }
        if (owner_d3d_over) ln += snprintf(line + ln, sizeof line - ln, " (+%u unlisted)", owner_d3d_over);
        XK_LOG("%s\n", line); owner_d3d_n = 0; owner_d3d_over = 0;
    }
}
/* Proxy wait: a guest wait (WaitForSingleObject...) inside an overlapped scene must not enter the scheduler from the
 * helper. The helper posts the request and parks on a host semaphore; the owner, idle in join(), performs the wait on
 * its own guest thread record (the scene *is* thread 8), stores the result and releases the helper. Other fibers run
 * while the owner is parked in the wait, so I/O completions the scene waits for still arrive (perf87: the first
 * loading-screen scene waited on an event the immediate-timeout answer never satisfied; the owner joined forever). */
static struct { xk_obj **objs; int n, wait_all; const int64_t *timeout; uint32_t result; } proxy; static volatile int proxy_pending;

/* ---- kernel-call proxy (the general fix for "two threads inside the single-threaded guest kernel") -------------
 * Every kernel import the scene body issues on the helper (mutex acquire/release for the cache-file request table,
 * events, file requests...) is executed by the OWNER on the scene's own guest thread record: the helper posts
 * {ctx, fn}, parks on a host semaphore, and the owner runs fn(ctx) from join() or from xv_preempt when its current
 * fiber is the scene's thread (mode 2). Read-only time queries run directly. The census counts calls by name. */
#define PROXY_NAMES 40
static struct { const char *name; unsigned n; } proxy_names[PROXY_NAMES]; static unsigned proxy_names_n, proxy_calls, proxy_direct;
static xctx *volatile proxy_call_ctx; static void (*volatile proxy_call_fn)(xctx *); static const char *volatile proxy_call_name;
extern const char *const xv_kernel_names[];
static int proxy_direct_ok(const char *name)
{
    return name && (!strcmp(name, "KeQueryPerformanceCounter") || !strcmp(name, "KeQueryPerformanceFrequency") || !strcmp(name, "KeQuerySystemTime"));
}
static void proxy_note(const char *name)
{
    for (unsigned i = 0; i < proxy_names_n; ++i) if (proxy_names[i].name == name) { proxy_names[i].n++; return; }
    if (proxy_names_n < PROXY_NAMES) { proxy_names[proxy_names_n].name = name; proxy_names[proxy_names_n].n = 1; proxy_names_n++; }
}
static void proxy_report(void)
{
    if (!proxy_calls && !proxy_direct) return;
    char line[400]; int ln = snprintf(line, sizeof line, "[scene-proxy] kernel calls from the helper: proxied %u direct %u:", proxy_calls, proxy_direct);
    for (unsigned i = 0; i < proxy_names_n && ln < 340; ++i) ln += snprintf(line + ln, sizeof line - ln, " %s %u", proxy_names[i].name ? proxy_names[i].name : "?", proxy_names[i].n);
    XK_LOG("%s\n", line); proxy_calls = proxy_direct = 0; proxy_names_n = 0;
}
uint32_t xk_wait(xk_obj **objs, int n, int wait_all, int alertable, const int64_t *timeout);
static SceUID proxy_done = -1;
/* "Am I the helper?" is asked twice per D3D HLE call (xd3d_count) - ~3,800 calls per frame in the corridor - and
 * sceKernelGetThreadId is a syscall: compare the stack pointer with the helper's 1 MiB stack instead (set once in
 * helper_main); the syscall remains the fallback until the helper has started. */
static uintptr_t helper_sp_lo, helper_sp_hi;
int xv_scene_thread_on_helper(void)
{
    if (helper_sp_hi) { uintptr_t sp = (uintptr_t)__builtin_frame_address(0); return sp >= helper_sp_lo && sp < helper_sp_hi; }
    return helper >= 0 && sceKernelGetThreadId() == helper;
}
uint32_t xv_scene_thread_proxy_wait(xk_obj **objs, int n, int wait_all, const int64_t *timeout)
{
    proxy.objs = objs; proxy.n = n; proxy.wait_all = wait_all; proxy.timeout = timeout; proxy.result = 0;
    __atomic_store_n(&proxy_pending, 1, __ATOMIC_RELEASE);
    sceKernelWaitSema(proxy_done, 1, NULL);
    return proxy.result;
}
int xv_scene_thread_proxy_call(xctx *c, void (*fn)(xctx *), unsigned ord)
{
    if (!(enabled > 0 && in_flight && xv_scene_thread_on_helper())) return 0;   /* every helper scene, overlapped or not: the owner's join poll sleeps through the guest scheduler, so a direct kernel call from the helper races it (Vita core 13:35: helper in xk_NtReleaseMutant via the CRT critical section from 50560) */
    const char *name = ord < 367 ? xv_kernel_names[ord] : NULL; proxy_note(name);
    if (proxy_direct_ok(name)) { proxy_direct++; return 0; }
    if (name && strcmp(name, "NtYieldExecution") == 0) { c->r[0] = 0; c->r[4] += 4u; proxy_direct++; return 1; }
    proxy_call_ctx = c; proxy_call_name = name; __atomic_store_n(&proxy_call_fn, fn, __ATOMIC_RELEASE);
    sceKernelWaitSema(proxy_done, 1, NULL);
    return 1;
}
int xv_scene_thread_proxy_hle(xctx *c, void (*fn)(xctx *), const char *name)
{
    if (!(enabled > 0 && in_flight && xv_scene_thread_on_helper())) return 0;   /* every helper scene, overlapped or not: the owner's join poll sleeps through the guest scheduler, so a direct kernel call from the helper races it (Vita core 13:35: helper in xk_NtReleaseMutant via the CRT critical section from 50560) */
    proxy_note(name);
    if (proxy_direct_ok(name + 3)) { proxy_direct++; return 0; }   /* "xk_KeQuery..." -> the time queries stay direct */
    if (strcmp(name, "xk_NtYieldExecution") == 0) { c->r[0] = 0; c->r[4] += 4u; proxy_direct++; return 1; }   /* a yield on the helper's behalf is meaningless (and re-enters xk_yield from the yield-path servicer): STATUS_SUCCESS, ret 0 */
    proxy_call_ctx = c; proxy_call_name = name; __atomic_store_n(&proxy_call_fn, fn, __ATOMIC_RELEASE);
    sceKernelWaitSema(proxy_done, 1, NULL);
    return 1;
}
static void proxy_service_call_only(void)
{
    { void (*fn)(xctx *) = proxy_call_fn; if (fn) { xctx *cc = proxy_call_ctx; proxy_call_fn = NULL; fn(cc); proxy_calls++; sceKernelSignalSema(proxy_done, 1); } }
}
static void proxy_service(void)
{
    { void (*fn)(xctx *) = proxy_call_fn; if (fn) { xctx *cc = proxy_call_ctx; proxy_call_fn = NULL; fn(cc); proxy_calls++; sceKernelSignalSema(proxy_done, 1); } }
    if (!__atomic_load_n(&proxy_pending, __ATOMIC_ACQUIRE)) return;
    proxy.result = xk_wait(proxy.objs, proxy.n, proxy.wait_all, 0, proxy.timeout); proxy_waits++;
    __atomic_store_n(&proxy_pending, 0, __ATOMIC_RELEASE); sceKernelSignalSema(proxy_done, 1);
}
/* The proxy fiber: a kernel-internal guest thread (like the vblank thread) that executes the helper's proxied kernel
 * calls and waits AS ITSELF, so a call that blocks parks this fiber, not the owner's: the owner's tick keeps running
 * and can signal whatever the blocking call waits for. Lock identity: locks the scene takes belong to this fiber. */
static xk_thread *proxy_fiber;
static void proxy_fiber_main(xctx *c, void *arg)
{
    (void)c; (void)arg;
    /* 2 ms, not 50 us: a fiber whose sleep deadline is always the nearest starves every other sleeper - the scheduler kept
     * picking it and the owner sat 40 ms overdue (Vita perf113 froze at the menu in a 6,000/s yield storm) */
    for (;;) { if (__atomic_load_n(&proxy_call_fn, __ATOMIC_ACQUIRE) || __atomic_load_n(&proxy_pending, __ATOMIC_ACQUIRE)) proxy_service(); else xk_sleep_us(2000); }
}
static void proxy_fiber_start(void)
{
    if (proxy_fiber) return;
    { const char *e = getenv("XV_SCENE_PROXY_FIBER"); if (!(e && atoi(e))) return; }   /* off by default: on the Vita (perf113/114) the object pass stopped running with it (jobs 0/window, objects not updated, NPC flicker); diagnose on the Pi */
    extern xk_thread *xk_thread_create_host(void (*)(xctx *, void *), void *);
    proxy_fiber = xk_thread_create_host(proxy_fiber_main, NULL);
    XK_LOG("[scene-thread] proxy fiber %s\n", proxy_fiber ? "started" : "FAILED (proxied calls will run on the owner)");
}
static xk_thread *proxy_fiber;   /* tentative; defined with the proxy fiber below */
void xv_scene_thread_service(void) { if (!proxy_fiber && enabled > 0 && in_flight_overlapped && xk_cur == scene_guest && !xv_scene_thread_on_helper()) proxy_service(); }   /* owner service point (xv_preempt) when no proxy fiber */   /* proxied calls run on the proxy fiber (proxy_fiber_main), never on the owner's fiber: a blocking one (a critical section held by a streaming thread that waits for the owner's tick) deadlocked the owner (Vita perf112 17:14) */   /* owner service point (xv_preempt) */
static uint64_t stuck_logged_at;
static void stuck_check(uint64_t t0)   /* the scene has not finished for 3 s: log the helper's guest state once (a poor man's backtrace: return-address candidates on its stack) and let the render view's watchdog restore the live mapping */
{
    uint64_t now = xk_os_monotonic_us(); if (now - t0 < 3000000u || stuck_logged_at == t0) return;
    stuck_logged_at = t0;
    XK_LOG("[scene-thread] STUCK %llu ms: helper ctx eax %08X ecx %08X edx %08X ebx %08X esp %08X ebp %08X esi %08X edi %08X preempt %d proxy fn %p pending %d\n", (unsigned long long)((now - t0) / 1000u),
           ctx.r[0], ctx.r[1], ctx.r[2], ctx.r[3], ctx.r[4], ctx.r[5], ctx.r[6], ctx.r[7], (int)ctx.preempt, (void *)proxy_call_fn, (int)proxy_pending);
    { char line[400]; int ln = snprintf(line, sizeof line, "[scene-thread]   stack code words:"); uint32_t sp = ctx.r[4];
      for (unsigned i = 0; i < 256 && ln < 360; ++i) { uint32_t w = X_M32(sp + 4u * i); if (w >= 0x10000u && w < 0x3B5000u) ln += snprintf(line + ln, sizeof line - ln, " %X", w); }
      XK_LOG("%s\n", line); }
    { extern void xv_render_view_watchdog(void) __attribute__((weak)); if (xv_render_view_watchdog) xv_render_view_watchdog(); }
}
/* Serviced from xk_yield too (xv_scene_thread_service_yield): while the owner's main thread spins in the game's own
 * "wait for the cache request" loop (f_00056670, Vita perf116/117 froze at the load->cinematic transition), the
 * streaming thread waits for an event the SCENE sets through the proxy, and the join loop that services the proxy is
 * not running. Only calls without lock identity (events, yields) are run from an arbitrary yielding thread. */
static int proxy_name_lockless(const char *nm)
{
    if (!nm) return 0; if (strncmp(nm, "xk_", 3) == 0) nm += 3;
    return strcmp(nm, "NtSetEvent") == 0 || strcmp(nm, "NtPulseEvent") == 0 || strcmp(nm, "NtClearEvent") == 0 || strcmp(nm, "KeSetEvent") == 0;   /* no yields: they re-enter xk_yield */
}
/* The owner blocked in a host wait of its own (object-jobs owner_wake/dones): a worker may be waiting for the math
 * guard the HELPER holds while the helper waits for the owner to run its proxied kernel call - three-way deadlock
 * (Pi mode4d 18:53: owner in xv_object_jobs_join/owner_wake, workers in xv_object_math_lock, helper in proxy_hle).
 * Full service (calls and the pending wait), exactly what the dispatch join loop runs; owner thread only. */
void xv_scene_thread_service_owner_blocked(void)
{
    if (enabled <= 0 || !in_flight || proxy_fiber || xv_scene_thread_on_helper()) return;
    if (sceKernelGetThreadId() != xv_scene_owner_alias) return;
    static int in_service; if (in_service) return; in_service = 1;
    proxy_service(); owner_blocked_services++;
    in_service = 0;
}
void xv_scene_thread_service_yield(void)
{
    if (enabled <= 0 || !in_flight || proxy_fiber || xv_scene_thread_on_helper()) return;
    if (sceKernelGetThreadId() != xv_scene_owner_alias) return;   /* only the dispatching guest thread itself: an HLE run from an object-job worker or another fiber corrupts the scheduler (host wedge at the first scene) */
    static int in_service; if (in_service) return; in_service = 1;
    void (*fn)(xctx *) = proxy_call_fn; if (fn && proxy_name_lockless(proxy_call_name)) proxy_service_call_only();   /* never the proxied WAIT from a random yielding thread */
    in_service = 0;
}
static void join(unsigned *counter)
{
    if (!in_flight) return;
    uint64_t t0 = xk_os_monotonic_us();
    /* The owner is a guest fiber holding the single runner: a blocking host wait here starves the streaming and sound
     * fibers the scene may be waiting on (lockstep runs deadlocked at the a10 load). Poll, and park in the guest
     * scheduler between polls so those fibers run. */
    while (sceKernelPollSema(done, 1) < 0) { if (!proxy_fiber) proxy_service(); xk_sleep_us(200); stuck_check(t0); }   /* the proxy fiber services the helper's kernel calls; the owner only parks so the scheduler runs it and the streaming fibers */   /* 200 us (proxied kernel calls wait here): a 100 us poll kept the owner core at ~95% and starved the runtime's remote thread (perf88) */
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    in_flight = 0; in_flight_overlapped = 0; depth = 0; (*counter)++;
    { extern void xd3d_present_flush(void) __attribute__((weak)); if (xd3d_present_flush) xd3d_present_flush(); }   /* mode 2: the deferred device present */
}
void xv_scene_thread_join(void) { if (enabled > 0 && overlap) join(&joins_present); }
void xv_scene_thread_join_owner(void) { if (enabled > 0 && in_flight && !xv_scene_thread_on_helper()) join(&joins_d3d); }   /* an owner-side D3D HLE while a scene is in flight waits: the Vita runtime (GXM) is single-threaded (menu/loading draws crashed perf83 run 1) */
int xv_scene_thread_present_policy(void) { return enabled > 0 && in_flight ? overlap : 0; }

static int helper_main(SceSize args, void *argp)
{
    (void)args; (void)argp;
    { char m; helper_sp_hi = (uintptr_t)&m + 4096u; helper_sp_lo = helper_sp_hi - (1024u * 1024u + 8192u); }   /* the 1 MiB stack from sceKernelCreateThread, with margins */
    for (;;) {
        if (sceKernelWaitSema(go, 1, NULL) < 0) return -1;
        if (setjmp(scene_abandon_jmp) == 0) { scene_abandon_armed = 1; f_000BCB30(&ctx); }
        scene_abandon_armed = 0;
        sceKernelSignalSema(done, 1);
    }
}
static void configure(void)
{
    configured = 1;
    const char *e = getenv("XV_SCENE_THREAD"); enabled = e ? atoi(e) != 0 : XV_SCENE_THREAD_DEFAULT;
    { const char *o = getenv("XV_SCENE_OVERLAP"); overlap = o ? atoi(o) : 0; if (overlap < 0 || overlap > 2) overlap = 0; }   /* 1: Present joins; 2: Present deferred to the next dispatch */
    /* kernel-owned, above the game heap: taking 256 KiB from the game's pool at the first menu frame left the a10 tag-cache
     * contiguous allocation short on the Vita (perf85/86: the load never completed under the overlap; fine with it off) */
    if (overlap) { scene_stack = xk_mem_alloc_high(SCENE_STACK_BYTES, 4096); if (!scene_stack) { XK_LOG("[scene-thread] no guest stack for the overlap; overlap off\n"); overlap = 0; } else XK_LOG("[scene-thread] overlap %d: private scene stack at %08X (kernel region)\n", overlap, scene_stack); }
    if (!enabled) { XK_LOG("[scene-thread] process-start disabled\n"); return; }
    go = sceKernelCreateSema("xv_scene_go", 0, 0, 1, NULL); done = sceKernelCreateSema("xv_scene_done", 0, 0, 1, NULL);
    /* Core 1 only: the owner presents on core 2 and the runtime's capture/upload/texture workers and the remote server
     * live on core 0. The helper polls its capture-completion waits every 50 us at the owner's priority; on core 0 it
     * starved the worker it was waiting for (loads never finished) and the remote server (perf85-89). */
    int core = 1; { const char *ce = getenv("XV_SCENE_THREAD_CORE"); if (ce && atoi(ce) >= 0 && atoi(ce) <= 2) core = atoi(ce); }
    int mask = core == 0 ? SCE_KERNEL_CPU_MASK_USER_0 : core == 2 ? SCE_KERNEL_CPU_MASK_USER_2 : SCE_KERNEL_CPU_MASK_USER_1;
    helper = go >= 0 && done >= 0 ? sceKernelCreateThread("xv_scene", helper_main, sceKernelGetThreadCurrentPriority(), 1024 * 1024, 0, mask, NULL) : -1;
    proxy_done = sceKernelCreateSema("xv_scene_proxy", 0, 0, 1, NULL);
    if (helper < 0 || sceKernelStartThread(helper, 0, NULL) < 0) { XK_LOG("[scene-thread] helper thread failed; disabled\n"); enabled = 0; return; }
    xv_scene_helper_thread = helper;
    XK_LOG("[scene-thread] process-start enabled: BCB30 runs on helper thread %08x (core %d), owner waits\n", (unsigned)helper, core);
    proxy_fiber_start();
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
    int ov = overlap && gameplay_active(); in_flight_overlapped = ov;
    if (ov) { uint32_t top = scene_stack + SCENE_STACK_BYTES - 64u; for (unsigned i = 0; i < 4; ++i) X_W32(top + 4u * i) = X_M32(c->r[4] + 4u * i); ctx.r[4] = top; }   /* body frame on the private stack: return address + 8-byte argument copied */
    if (ov) { extern void xv_render_view_prepare(void) __attribute__((weak)); if (xv_render_view_prepare) xv_render_view_prepare(); }   /* snapshot before the tick resumes */
    sceKernelSignalSema(go, 1);
    if (ov) { in_flight = 1; overlaps++; dispatched++; c->r[4] += 12; return 1; }   /* the body's `ret 8`: the owner continues */
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
    scene_census_report(); overlap_report(); proxy_report(); phase_report(frames); { extern void xv_hle_time_report(unsigned) __attribute__((weak)); if (xv_hle_time_report) xv_hle_time_report(frames); }
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
static int overlap, in_flight_overlapped; static unsigned owner_blocked_services, in_flight, overlaps, joins_present, joins_dispatch, joins_d3d, proxy_waits, suppressed_yields, suppressed_waits; static uint32_t suppressed_eip;
#define OWNER_D3D 48
static struct { const char *name; unsigned n; } owner_d3d[OWNER_D3D]; static unsigned owner_d3d_n, owner_d3d_over;
int xv_scene_thread_on_helper(void);
void xv_scene_thread_d3d_call(const char *name)
{
    if (enabled <= 0 || xv_scene_thread_on_helper()) return;
    for (unsigned i = 0; i < owner_d3d_n; ++i) if (owner_d3d[i].name == name) { owner_d3d[i].n++; return; }
    if (owner_d3d_n < OWNER_D3D) { owner_d3d[owner_d3d_n].name = name; owner_d3d[owner_d3d_n].n = 1; owner_d3d_n++; } else owner_d3d_over++;
}
static int gameplay_active(void) { uint32_t gg = X_M32(0x2F8CA0u); return gg && X_M8(gg) && X_M8(gg + 1u); }   /* game_globals: loaded, active */
int xv_scene_thread_no_yield(void) { return in_flight_overlapped && xv_scene_thread_on_helper(); }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { suppressed_yields++; if (blocking) { suppressed_waits++; suppressed_eip = eip; } }
static void overlap_report(void)
{
    { extern unsigned xv_log_helper_dropped(void) __attribute__((weak)); if (xv_log_helper_dropped) { unsigned d = xv_log_helper_dropped(); if (d) XK_LOG("[scene-thread] helper log lines dropped: %u\n", d); } }
    if (overlap) XK_LOG("[scene-overlap] dispatched-without-wait %u, joins at present %u / next dispatch %u / owner d3d %u, proxy waits %u, suppressed helper yields %u (blocking %u, last eip %X)\n",
                        overlaps, joins_present, joins_dispatch, joins_d3d, proxy_waits, suppressed_yields, suppressed_waits, suppressed_eip);
    overlaps = joins_present = joins_dispatch = joins_d3d = proxy_waits = suppressed_yields = suppressed_waits = 0;
    if (owner_d3d_n) {
        char line[400]; int ln = snprintf(line, sizeof line, "[scene-owner-d3d] owner-side calls:");
        for (unsigned i = 0; i < owner_d3d_n; ++i) { if (ln > 330) { XK_LOG("%s\n", line); ln = snprintf(line, sizeof line, "[scene-owner-d3d]  "); } ln += snprintf(line + ln, sizeof line - ln, " %s %u", owner_d3d[i].name, owner_d3d[i].n); }
        if (owner_d3d_over) ln += snprintf(line + ln, sizeof line - ln, " (+%u unlisted)", owner_d3d_over);
        XK_LOG("%s\n", line); owner_d3d_n = 0; owner_d3d_over = 0;
    }
}
/* Proxy wait: a guest wait (WaitForSingleObject...) inside an overlapped scene must not enter the scheduler from the
 * helper. The helper posts the request and parks on a host semaphore; the owner, idle in join(), performs the wait on
 * its own guest thread record (the scene *is* thread 8), stores the result and releases the helper. Other fibers run
 * while the owner is parked in the wait, so I/O completions the scene waits for still arrive (perf87: the first
 * loading-screen scene waited on an event the immediate-timeout answer never satisfied; the owner joined forever). */
static struct { xk_obj **objs; int n, wait_all; const int64_t *timeout; uint32_t result; } proxy; static volatile int proxy_pending;

/* ---- kernel-call proxy (the general fix for "two threads inside the single-threaded guest kernel") -------------
 * Every kernel import the scene body issues on the helper (mutex acquire/release for the cache-file request table,
 * events, file requests...) is executed by the OWNER on the scene's own guest thread record: the helper posts
 * {ctx, fn}, parks on a host semaphore, and the owner runs fn(ctx) from join() or from xv_preempt when its current
 * fiber is the scene's thread (mode 2). Read-only time queries run directly. The census counts calls by name. */
#define PROXY_NAMES 40
static struct { const char *name; unsigned n; } proxy_names[PROXY_NAMES]; static unsigned proxy_names_n, proxy_calls, proxy_direct;
static xctx *volatile proxy_call_ctx; static void (*volatile proxy_call_fn)(xctx *); static const char *volatile proxy_call_name;
extern const char *const xv_kernel_names[];
static int proxy_direct_ok(const char *name)
{
    return name && (!strcmp(name, "KeQueryPerformanceCounter") || !strcmp(name, "KeQueryPerformanceFrequency") || !strcmp(name, "KeQuerySystemTime"));
}
static void proxy_note(const char *name)
{
    for (unsigned i = 0; i < proxy_names_n; ++i) if (proxy_names[i].name == name) { proxy_names[i].n++; return; }
    if (proxy_names_n < PROXY_NAMES) { proxy_names[proxy_names_n].name = name; proxy_names[proxy_names_n].n = 1; proxy_names_n++; }
}
static void proxy_report(void)
{
    if (!proxy_calls && !proxy_direct) return;
    char line[400]; int ln = snprintf(line, sizeof line, "[scene-proxy] kernel calls from the helper: proxied %u direct %u:", proxy_calls, proxy_direct);
    for (unsigned i = 0; i < proxy_names_n && ln < 340; ++i) ln += snprintf(line + ln, sizeof line - ln, " %s %u", proxy_names[i].name ? proxy_names[i].name : "?", proxy_names[i].n);
    XK_LOG("%s\n", line); proxy_calls = proxy_direct = 0; proxy_names_n = 0;
}
uint32_t xk_wait(xk_obj **objs, int n, int wait_all, int alertable, const int64_t *timeout);
static sem_t proxy_done;
int xv_scene_thread_on_helper(void) { return helper_valid && pthread_equal(pthread_self(), helper); }
uint32_t xv_scene_thread_proxy_wait(xk_obj **objs, int n, int wait_all, const int64_t *timeout)
{
    proxy.objs = objs; proxy.n = n; proxy.wait_all = wait_all; proxy.timeout = timeout; proxy.result = 0;
    __atomic_store_n(&proxy_pending, 1, __ATOMIC_RELEASE);
    while (sem_wait(&proxy_done) < 0 && errno == EINTR) {}
    return proxy.result;
}
int xv_scene_thread_proxy_call(xctx *c, void (*fn)(xctx *), unsigned ord)
{
    if (!(enabled > 0 && in_flight && xv_scene_thread_on_helper())) return 0;   /* every helper scene, overlapped or not: the owner's join poll sleeps through the guest scheduler, so a direct kernel call from the helper races it (Vita core 13:35: helper in xk_NtReleaseMutant via the CRT critical section from 50560) */
    const char *name = ord < 367 ? xv_kernel_names[ord] : NULL; proxy_note(name);
    if (proxy_direct_ok(name)) { proxy_direct++; return 0; }
    if (name && strcmp(name, "NtYieldExecution") == 0) { c->r[0] = 0; c->r[4] += 4u; proxy_direct++; return 1; }
    proxy_call_ctx = c; proxy_call_name = name; __atomic_store_n(&proxy_call_fn, fn, __ATOMIC_RELEASE);
    while (sem_wait(&proxy_done) < 0 && errno == EINTR) {}
    return 1;
}
int xv_scene_thread_proxy_hle(xctx *c, void (*fn)(xctx *), const char *name)
{
    if (!(enabled > 0 && in_flight && xv_scene_thread_on_helper())) return 0;   /* every helper scene, overlapped or not: the owner's join poll sleeps through the guest scheduler, so a direct kernel call from the helper races it (Vita core 13:35: helper in xk_NtReleaseMutant via the CRT critical section from 50560) */
    proxy_note(name);
    if (proxy_direct_ok(name + 3)) { proxy_direct++; return 0; }   /* "xk_KeQuery..." -> the time queries stay direct */
    if (strcmp(name, "xk_NtYieldExecution") == 0) { c->r[0] = 0; c->r[4] += 4u; proxy_direct++; return 1; }   /* a yield on the helper's behalf is meaningless (and re-enters xk_yield from the yield-path servicer): STATUS_SUCCESS, ret 0 */
    proxy_call_ctx = c; proxy_call_name = name; __atomic_store_n(&proxy_call_fn, fn, __ATOMIC_RELEASE);
    while (sem_wait(&proxy_done) < 0 && errno == EINTR) {}
    return 1;
}
static void proxy_service_call_only(void)
{
    { void (*fn)(xctx *) = proxy_call_fn; if (fn) { xctx *cc = proxy_call_ctx; proxy_call_fn = NULL; fn(cc); proxy_calls++; sem_post(&proxy_done); } }
}
static void proxy_service(void)
{
    { void (*fn)(xctx *) = proxy_call_fn; if (fn) { xctx *cc = proxy_call_ctx; proxy_call_fn = NULL; fn(cc); proxy_calls++; sem_post(&proxy_done); } }
    if (!__atomic_load_n(&proxy_pending, __ATOMIC_ACQUIRE)) return;
    proxy.result = xk_wait(proxy.objs, proxy.n, proxy.wait_all, 0, proxy.timeout); proxy_waits++;
    __atomic_store_n(&proxy_pending, 0, __ATOMIC_RELEASE); sem_post(&proxy_done);
}
/* The proxy fiber: a kernel-internal guest thread (like the vblank thread) that executes the helper's proxied kernel
 * calls and waits AS ITSELF, so a call that blocks parks this fiber, not the owner's: the owner's tick keeps running
 * and can signal whatever the blocking call waits for. Lock identity: locks the scene takes belong to this fiber. */
static xk_thread *proxy_fiber;
static void proxy_fiber_main(xctx *c, void *arg)
{
    (void)c; (void)arg;
    /* 2 ms, not 50 us: a fiber whose sleep deadline is always the nearest starves every other sleeper - the scheduler kept
     * picking it and the owner sat 40 ms overdue (Vita perf113 froze at the menu in a 6,000/s yield storm) */
    for (;;) { if (__atomic_load_n(&proxy_call_fn, __ATOMIC_ACQUIRE) || __atomic_load_n(&proxy_pending, __ATOMIC_ACQUIRE)) proxy_service(); else xk_sleep_us(2000); }
}
static void proxy_fiber_start(void)
{
    if (proxy_fiber) return;
    { const char *e = getenv("XV_SCENE_PROXY_FIBER"); if (!(e && atoi(e))) return; }   /* off by default: on the Vita (perf113/114) the object pass stopped running with it (jobs 0/window, objects not updated, NPC flicker); diagnose on the Pi */
    extern xk_thread *xk_thread_create_host(void (*)(xctx *, void *), void *);
    proxy_fiber = xk_thread_create_host(proxy_fiber_main, NULL);
    XK_LOG("[scene-thread] proxy fiber %s\n", proxy_fiber ? "started" : "FAILED (proxied calls will run on the owner)");
}
static pthread_t owner_thread_self;   /* the thread that configured (owner); yield-path proxy service only from it */
static xk_thread *proxy_fiber;   /* tentative; defined with the proxy fiber below */
void xv_scene_thread_service(void) { if (!proxy_fiber && enabled > 0 && in_flight_overlapped && xk_cur == scene_guest && !xv_scene_thread_on_helper()) proxy_service(); }   /* owner service point (xv_preempt) when no proxy fiber */   /* proxied calls run on the proxy fiber (proxy_fiber_main), never on the owner's fiber: a blocking one (a critical section held by a streaming thread that waits for the owner's tick) deadlocked the owner (Vita perf112 17:14) */
static uint64_t stuck_logged_at;
static void stuck_check(uint64_t t0)   /* the scene has not finished for 3 s: log the helper's guest state once (a poor man's backtrace: return-address candidates on its stack) and let the render view's watchdog restore the live mapping */
{
    uint64_t now = xk_os_monotonic_us(); if (now - t0 < 3000000u || stuck_logged_at == t0) return;
    stuck_logged_at = t0;
    XK_LOG("[scene-thread] STUCK %llu ms: helper ctx eax %08X ecx %08X edx %08X ebx %08X esp %08X ebp %08X esi %08X edi %08X preempt %d proxy fn %p pending %d\n", (unsigned long long)((now - t0) / 1000u),
           ctx.r[0], ctx.r[1], ctx.r[2], ctx.r[3], ctx.r[4], ctx.r[5], ctx.r[6], ctx.r[7], (int)ctx.preempt, (void *)proxy_call_fn, (int)proxy_pending);
    { char line[400]; int ln = snprintf(line, sizeof line, "[scene-thread]   stack code words:"); uint32_t sp = ctx.r[4];
      for (unsigned i = 0; i < 256 && ln < 360; ++i) { uint32_t w = X_M32(sp + 4u * i); if (w >= 0x10000u && w < 0x3B5000u) ln += snprintf(line + ln, sizeof line - ln, " %X", w); }
      XK_LOG("%s\n", line); }
    { extern void xv_render_view_watchdog(void) __attribute__((weak)); if (xv_render_view_watchdog) xv_render_view_watchdog(); }
}
/* Serviced from xk_yield too (xv_scene_thread_service_yield): while the owner's main thread spins in the game's own
 * "wait for the cache request" loop (f_00056670, Vita perf116/117 froze at the load->cinematic transition), the
 * streaming thread waits for an event the SCENE sets through the proxy, and the join loop that services the proxy is
 * not running. Only calls without lock identity (events, yields) are run from an arbitrary yielding thread. */
static int proxy_name_lockless(const char *nm)
{
    if (!nm) return 0; if (strncmp(nm, "xk_", 3) == 0) nm += 3;
    return strcmp(nm, "NtSetEvent") == 0 || strcmp(nm, "NtPulseEvent") == 0 || strcmp(nm, "NtClearEvent") == 0 || strcmp(nm, "KeSetEvent") == 0;   /* no yields: they re-enter xk_yield */
}
/* The owner blocked in a host wait of its own (object-jobs owner_wake/dones): a worker may be waiting for the math
 * guard the HELPER holds while the helper waits for the owner to run its proxied kernel call - three-way deadlock
 * (Pi mode4d 18:53: owner in xv_object_jobs_join/owner_wake, workers in xv_object_math_lock, helper in proxy_hle).
 * Full service (calls and the pending wait), exactly what the dispatch join loop runs; owner thread only. */
void xv_scene_thread_service_owner_blocked(void)
{
    if (enabled <= 0 || !in_flight || proxy_fiber || xv_scene_thread_on_helper()) return;
    if (!pthread_equal(pthread_self(), owner_thread_self)) return;
    static int in_service; if (in_service) return; in_service = 1;
    proxy_service(); owner_blocked_services++;
    in_service = 0;
}
void xv_scene_thread_service_yield(void)
{
    if (enabled <= 0 || !in_flight || proxy_fiber || xv_scene_thread_on_helper()) return;
    if (!pthread_equal(pthread_self(), owner_thread_self)) return;   /* only the owner's own thread: an HLE run from an object-job worker corrupts the scheduler (host wedge at the first scene) */
    static int in_service; if (in_service) return; in_service = 1;
    void (*fn)(xctx *) = proxy_call_fn; if (fn && proxy_name_lockless(proxy_call_name)) proxy_service_call_only();   /* never the proxied WAIT from a random yielding thread */
    in_service = 0;
}
static void join(unsigned *counter)
{
    if (!in_flight) return;
    uint64_t t0 = xk_os_monotonic_us();
    /* see the Vita port: poll and park in the guest scheduler so the other fibers run while the owner waits */
    while (sem_trywait(&done) < 0) { if (!proxy_fiber) proxy_service(); xk_sleep_us(200); stuck_check(t0); }
    uint64_t dt = xk_os_monotonic_us() - t0; wait_us += dt; if (dt > wait_max_us) wait_max_us = dt;
    in_flight = 0; in_flight_overlapped = 0; __atomic_store_n(&depth, 0, __ATOMIC_RELEASE); (*counter)++;
    { extern void xd3d_present_flush(void) __attribute__((weak)); if (xd3d_present_flush) xd3d_present_flush(); }   /* mode 2: the deferred device present */
}
void xv_scene_thread_join(void) { if (enabled > 0 && overlap) join(&joins_present); }
void xv_scene_thread_join_owner(void) { if (enabled > 0 && in_flight && !xv_scene_thread_on_helper()) join(&joins_d3d); }   /* an owner-side D3D HLE while a scene is in flight waits: the Vita runtime (GXM) is single-threaded (menu/loading draws crashed perf83 run 1) */
int xv_scene_thread_present_policy(void) { return enabled > 0 && in_flight ? overlap : 0; }

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
        if (setjmp(scene_abandon_jmp) == 0) { scene_abandon_armed = 1; f_000BCB30(&ctx); }
        scene_abandon_armed = 0;
        sem_post(&done);
    }
    return 0;
}
static void configure(void)
{
    configured = 1;
    const char *e = getenv("XV_SCENE_THREAD"); enabled = e ? atoi(e) != 0 : XV_SCENE_THREAD_DEFAULT;
    { const char *o = getenv("XV_SCENE_OVERLAP"); overlap = o ? atoi(o) : 0; if (overlap < 0 || overlap > 2) overlap = 0; }   /* 1: Present joins; 2: Present deferred to the next dispatch */
    /* kernel-owned, above the game heap: taking 256 KiB from the game's pool at the first menu frame left the a10 tag-cache
     * contiguous allocation short on the Vita (perf85/86: the load never completed under the overlap; fine with it off) */
    if (overlap) { scene_stack = xk_mem_alloc_high(SCENE_STACK_BYTES, 4096); if (!scene_stack) { XK_LOG("[scene-thread] no guest stack for the overlap; overlap off\n"); overlap = 0; } else XK_LOG("[scene-thread] overlap %d: private scene stack at %08X (kernel region)\n", overlap, scene_stack); }
    if (!enabled) { XK_LOG("[scene-thread] process-start disabled\n"); return; }
    if (sem_init(&go, 0, 0) || sem_init(&done, 0, 0) || sem_init(&proxy_done, 0, 0) || pthread_create(&helper, NULL, helper_main, NULL)) { XK_LOG("[scene-thread] helper thread failed; disabled\n"); enabled = 0; return; }
    helper_valid = 1; owner_thread_self = pthread_self();
    XK_LOG("[scene-thread] process-start enabled (host pthread): BCB30 runs on a helper thread, owner waits\n");
    proxy_fiber_start();
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
    int ov = overlap && gameplay_active(); in_flight_overlapped = ov;
    if (ov) { uint32_t top = scene_stack + SCENE_STACK_BYTES - 64u; for (unsigned i = 0; i < 4; ++i) X_W32(top + 4u * i) = X_M32(c->r[4] + 4u * i); ctx.r[4] = top; }   /* body frame on the private stack: return address + 8-byte argument copied */
    if (ov) { extern void xv_render_view_prepare(void) __attribute__((weak)); if (xv_render_view_prepare) xv_render_view_prepare(); }   /* snapshot before the tick resumes */
    sem_post(&go);
    if (ov) { in_flight = 1; overlaps++; dispatched++; c->r[4] += 12; return 1; }   /* the body's `ret 8`: the owner continues */
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
    scene_census_report(); overlap_report(); proxy_report(); phase_report(frames); { extern void xv_hle_time_report(unsigned) __attribute__((weak)); if (xv_hle_time_report) xv_hle_time_report(frames); }
}
#else
int xv_scene_thread_active(const void *guest_thread) { (void)guest_thread; return 0; }
int xv_scene_thread_on_helper(void) { return 0; }
int xv_scene_thread_no_yield(void) { return 0; }
void xv_scene_thread_join(void) {}
void xv_scene_thread_join_owner(void) {}
int xv_scene_thread_proxy_call(void *c, void (*fn)(void *), unsigned ord) { (void)c; (void)fn; (void)ord; return 0; }
int xv_scene_thread_proxy_hle(void *c, void (*fn)(void *), const char *name) { (void)c; (void)fn; (void)name; return 0; }
void xv_scene_thread_service(void) {}
uint32_t xv_scene_thread_proxy_wait(void *objs, int n, int wait_all, const void *timeout) { (void)objs; (void)n; (void)wait_all; (void)timeout; return 0x102; }
int xv_scene_thread_present_policy(void) { return 0; }
void xv_scene_thread_d3d_call(const char *name) { (void)name; }
void xv_scene_thread_note_suppressed_yield(uint32_t eip, int blocking) { (void)eip; (void)blocking; }
int xv_scene_thread_run(void *context) { (void)context; return 0; }
void xv_scene_thread_report(unsigned frames) { (void)frames; }
#if !defined(__vita__)
#include <pthread.h>
pthread_t xv_owner_pthread_self(void) { return pthread_self(); }   /* no helper: identity */
#endif
#endif
