/* xk_thread.c - objects, handles, threads (fibers), waits, events/mutants/semaphores/timers, Ke time exports. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xk.h"

void xv_phase_suspend(void *context) __attribute__((weak));
void xv_phase_resume(void *context) __attribute__((weak));
void xv_phase_forget(void *context) __attribute__((weak));

/* ---- objects & handles ------------------------------------------------------------------- */
static xk_obj *g_handles[XK_MAX_HANDLES];
static uint32_t g_next_handle = 4;
void xk_file_release(xk_obj *o);

xk_obj *xk_obj_new(xk_objtype t) { xk_obj *o = calloc(1, sizeof *o); o->type = t; o->refs = 1; return o; }
void xk_obj_ref(xk_obj *o) { if (o) o->refs++; }
void xk_obj_deref(xk_obj *o)
{
    if (!o || --o->refs > 0) return;
    if (o->type == XO_FILE || o->type == XO_DIRECTORY) xk_file_release(o);
    if (o->type == XO_SYMLINK) free(o->u.symlink.target);
    if (o->type == XO_THREAD) return;              /* threads outlive handles (freed at exit) */
    free(o);
}
uint32_t xk_handle_create(xk_obj *o)
{
    for (unsigned i = 0; i < XK_MAX_HANDLES; ++i) {
        uint32_t idx = (g_next_handle / 4 + i) % XK_MAX_HANDLES; if (idx == 0) continue;
        if (!g_handles[idx]) { g_handles[idx] = o; xk_obj_ref(o); g_next_handle = (idx + 1) * 4; return idx * 4; }
    }
    XK_LOG("out of handles\n"); return 0;
}
xk_obj *xk_handle_get(uint32_t h)
{
    if (h == 0xFFFFFFFEu) return xk_cur ? xk_cur->obj : NULL;     /* NtCurrentThread() */
    uint32_t idx = h / 4; return (h & 3) || idx >= XK_MAX_HANDLES ? NULL : g_handles[idx];
}
xk_obj *xk_handle_get_type(uint32_t h, xk_objtype t) { xk_obj *o = xk_handle_get(h); return o && o->type == t ? o : NULL; }
void xk_handle_close(uint32_t h) { uint32_t idx = h / 4; if (idx && idx < XK_MAX_HANDLES && g_handles[idx]) { xk_obj *o = g_handles[idx]; g_handles[idx] = NULL; xk_obj_deref(o); } }

/* guest-visible dispatcher objects (KEVENT etc. embedded in game memory) are tracked by address */
typedef struct { uint32_t guest; xk_obj *obj; } gmap_t;
static gmap_t g_gmap[1024]; static int g_ngmap;
xk_obj *xk_obj_from_guest(uint32_t g)
{
    for (int i = 0; i < g_ngmap; ++i) if (g_gmap[i].guest == g) return g_gmap[i].obj;
    return NULL;
}
static xk_obj *guest_obj(uint32_t g, xk_objtype t)
{
    xk_obj *o = xk_obj_from_guest(g);
    if (o) return o;
    o = xk_obj_new(t); o->guest = g;
    /* DISPATCHER_HEADER { UCHAR Type; UCHAR Absolute; UCHAR Size; UCHAR Inserted; LONG SignalState; LIST_ENTRY WaitListHead } */
    if (t == XO_EVENT) { o->u.event.manual = X_M8(g) == 0; o->u.event.signaled = (int32_t)X_M32(g + 4) != 0; }
    if (g_ngmap < 1024) g_gmap[g_ngmap++] = (gmap_t){ g, o };
    return o;
}
static void sync_guest(xk_obj *o)   /* mirror SignalState into the guest header for code that peeks at it */
{
    if (!o->guest) return;
    if (o->type == XO_EVENT) X_M32(o->guest + 4) = o->u.event.signaled;
    else if (o->type == XO_SEMAPHORE) X_M32(o->guest + 4) = o->u.sem.count;
    else if (o->type == XO_MUTANT) X_M32(o->guest + 4) = o->u.mutant.owner ? 0 : 1;
    else if (o->type == XO_TIMER) X_M32(o->guest + 4) = o->u.timer.signaled;
}

/* ---- time ------------------------------------------------------------------------------------ */
static uint64_t g_boot_us;
uint64_t xk_time_100ns(void) { return xk_os_time_100ns(); }
uint64_t xk_uptime_100ns(void) { return (xk_os_monotonic_us() - g_boot_us) * 10; }
uint32_t xk_tick_count(void) { return (uint32_t)((xk_os_monotonic_us() - g_boot_us) / 1000); }
uint32_t xk_var_KeTickCount, xk_var_XboxHardwareInfo, xk_var_LaunchDataPage, xk_var_XboxKrnlVersion, xk_var_HalDiskCachePartitionCount;

/* ---- threads ---------------------------------------------------------------------------------- */
xk_thread *xk_cur;
static xk_thread *g_threads;          /* all live threads */
static int g_next_tid = 4;
static uint32_t g_tls_dir;            /* IMAGE_TLS_DIRECTORY in guest memory (0 = none) */
extern uint32_t xv_game_tls_dir;      /* from xv_fn_table.c (0 if unknown) */

static void thread_entry(void *arg)
{
    xk_thread *t = arg; xk_cur = t; xctx *c = &t->ctx;
    if (t->host_entry) { t->host_entry(c, t->host_arg); xk_thread_exit(0); }
    /* the kernel calls SystemRoutine(StartRoutine, StartContext); if none, StartRoutine(StartContext) */
    if (t->system_routine) { X_PUSH32(t->start_context); X_PUSH32(t->start_routine); X_PUSH32(0xDEAD0001u); xv_call(c, t->system_routine); }
    else { X_PUSH32(t->start_context); X_PUSH32(0xDEAD0001u); xv_call(c, t->start_routine); }
    xk_thread_exit(c->r[0]);
}

xk_thread *xk_thread_create_host(void (*entry)(xctx *c, void *arg), void *arg)
{
    xk_thread *t = xk_thread_create(0x8000, 0, 0, 0, 0, 1);
    if (!t) return NULL;
    t->host_entry = entry; t->host_arg = arg;
    t->suspend_count = 0; t->state = 0;
    return t;
}

void xk_sleep_us(uint64_t us)
{
    xk_thread *t = xk_cur;
    t->wait_n = 0; t->wait_until = xk_uptime_100ns() + us * 10; t->state = 1;
    xk_yield(); t->state = 0; t->wait_until = 0;
}

int xk_wait_u32(const uint32_t *word, uint32_t value, uint64_t timeout_us)
{
    if (__atomic_load_n(word,__ATOMIC_ACQUIRE)==value) return 1;
    if (!xk_os_scheduler_prepare() && timeout_us>1000) timeout_us=1000;
    xk_thread *t=xk_cur;
    t->wait_n=0; t->wait_word=word; t->wait_value=value;
    t->wait_until=xk_uptime_100ns()+timeout_us*10; t->state=1;
    xk_yield();
    t->state=0; t->wait_word=NULL; t->wait_until=0;
    return __atomic_load_n(word,__ATOMIC_ACQUIRE)==value;
}

xk_thread *xk_thread_create(uint32_t stack_size, uint32_t tls_size, uint32_t start_routine, uint32_t start_context, uint32_t system_routine, int suspended)
{
    xk_thread *t = calloc(1, sizeof *t);
    t->id = g_next_tid; g_next_tid += 4;
    if (stack_size < 0x10000) stack_size = 0x10000;
    stack_size = (stack_size + 0xFFF) & ~0xFFFu;
    /* TLS: the kernel only RESERVES the block at the top of the stack; XAPI fills it.  The layout is
     * pinned by the game itself: mainCRTStartup sets _tls_index = -(round16(raw+zero) + 4) / 4 so that
     * the compiler's  [fs:4 + _tls_index*4]  (StackBase + index*4) lands on a pointer slot, and
     * XapiThreadStartup does  slot = KTHREAD.TlsData; *slot = slot + 4; copy raw; zero-fill.  So:
     *   StackBase = top,  TlsData = top - (round16(size) + 4),  block = TlsData+4 .. top,  esp < TlsData.
     * (Previously the block sat at the very top with our own pointer slot 16 below it: XAPI's copy then
     * ran 4 bytes past the allocation and every TLS access read stack garbage - the first thread to exit
     * freed pointers out of that garbage and died in RtlFreeHeap.) */
    uint32_t tls_raw = 0, tls_zero = 0, tls_start = 0;
    if (g_tls_dir) { tls_start = X_M32(g_tls_dir); tls_raw = X_M32(g_tls_dir + 4) - tls_start; tls_zero = X_M32(g_tls_dir + 16); }
    (void)tls_start;
    /* The TlsDataSize the caller passes is XAPI's round16(raw+zero)+4 - slot included - and the kernel
     * reserves exactly that below StackBase.  Only threads created without one (our boot thread) get
     * the size computed here. */
    if (!tls_size) tls_size = ((tls_raw + tls_zero + 15) & ~15u) + 4;
    tls_size = (tls_size + 3) & ~3u;
    uint32_t total = (stack_size + tls_size + 64 + 15) & ~15u;
    t->stack_alloc = xk_mem_alloc(total, 0, 0, 0, 1);
    if (!t->stack_alloc) { XK_LOG("thread: no memory for stack\n"); free(t); return NULL; }
    uint32_t top = t->stack_alloc + total;
    t->tls = top - tls_size;                              /* KTHREAD.TlsData: pointer slot, block follows */
    X_M32(t->tls) = t->tls + 4;                           /* XAPI rewrites this; harmless for kernel-only threads */
    uint32_t slot = t->tls;
    t->stack_base = top; t->stack_limit = t->stack_alloc;
    /* KTHREAD + KPCR */
    t->kthread = xk_kalloc(XK_KTHREAD_SIZE); t->kpcr = xk_kalloc(XK_KPCR_SIZE);
    X_M8(t->kthread) = 6; X_M8(t->kthread + 2) = XK_KTHREAD_SIZE / 4;   /* dispatcher header: ThreadObject */
    X_M32(t->kthread + KTHREAD_TLSDATA) = t->tls;
    X_M8(t->kthread + KTHREAD_PRIORITY) = 8; X_M8(t->kthread + KTHREAD_BASEPRIORITY) = 8;
    X_M32(t->kthread + KTHREAD_UNIQUE_ID) = (uint32_t)t->id;
    X_M32(t->kpcr + KPCR_TIB_EXCEPTIONLIST) = 0xFFFFFFFFu;
    X_M32(t->kpcr + KPCR_TIB_STACKBASE) = t->stack_base; X_M32(t->kpcr + KPCR_TIB_STACKLIMIT) = t->stack_limit;
    X_M32(t->kpcr + KPCR_TIB_SELF) = t->kpcr; X_M32(t->kpcr + KPCR_SELFPCR) = t->kpcr; X_M32(t->kpcr + KPCR_PRCB) = t->kpcr + KPCR_PRCBDATA;
    X_M32(t->kpcr + KPCR_PRCBDATA) = t->kthread;
    /* context */
    x87_init(&t->ctx); t->ctx.fs_base = t->kpcr; t->ctx.r[4] = (slot - 32) & ~15u;   /* initial esp: below the TLS reservation */ t->ctx.fiber = NULL;
    t->start_routine = start_routine; t->start_context = start_context; t->system_routine = system_routine;
    t->state = suspended ? 2 : 0; t->suspend_count = suspended ? 1 : 0;
    t->obj = xk_obj_new(XO_THREAD); t->obj->u.thread = t; t->obj->guest = t->kthread;
    if (g_ngmap < 1024) g_gmap[g_ngmap++] = (gmap_t){ t->kthread, t->obj };
    t->fiber = xk_os_fiber_create(thread_entry, t, 512 * 1024);
    t->next = g_threads; g_threads = t;
    XK_LOG("thread %d created: start %08X ctx %08X sys %08X stack %08X..%08X tls %08X kthread %08X\n", t->id, start_routine, start_context, system_routine, t->stack_limit, t->stack_base, t->tls, t->kthread);
    return t;
}

void xk_thread_exit(uint32_t status)
{
    xk_thread *t = xk_cur;
    if (xv_phase_forget) xv_phase_forget(&t->ctx);
    XK_LOG("thread %d exited (%08X)\n", t->id, status);
    t->state = 3; t->exit_status = status; X_M32(t->kthread + KTHREAD_EXITSTATUS) = status; X_M8(t->kthread + KTHREAD_SIGNALSTATE) = 1;   /* GetExitCodeThread: SignalState ? ExitStatus : STILL_ACTIVE */
    xk_signal_check();
    xk_os_fiber_switch(xk_os_fiber_main());       /* never returns */
    for (;;) ;
}

/* ---- scheduler ---------------------------------------------------------------------------- */
static uint64_t now100(void) { return xk_uptime_100ns(); }

int xk_obj_signaled(xk_obj *o)
{
    switch (o->type) {
    case XO_EVENT: return o->u.event.signaled;
    case XO_MUTANT: return o->u.mutant.owner == NULL || o->u.mutant.owner == xk_cur;
    case XO_SEMAPHORE: return o->u.sem.count > 0;
    case XO_THREAD: return o->u.thread->state == 3;
    case XO_TIMER: if (!o->u.timer.signaled && o->u.timer.due && now100() >= o->u.timer.due) { o->u.timer.signaled = 1; if (o->u.timer.period) o->u.timer.due += (uint64_t)o->u.timer.period; } return o->u.timer.signaled;
    default: return 1;
    }
}
void xk_obj_consume(xk_obj *o, xk_thread *t)
{
    switch (o->type) {
    case XO_EVENT: if (!o->u.event.manual) o->u.event.signaled = 0; break;
    case XO_MUTANT: o->u.mutant.owner = t; o->u.mutant.count++; break;
    case XO_SEMAPHORE: o->u.sem.count--; break;
    case XO_TIMER: if (o->u.timer.period == 0) o->u.timer.signaled = 0; else o->u.timer.signaled = 0; break;
    default: break;
    }
    sync_guest(o);
}

static int try_satisfy(xk_thread *t)
{
    if (t->wait_word) {
        if (__atomic_load_n(t->wait_word,__ATOMIC_ACQUIRE)!=t->wait_value) return 0;
        t->wait_result=0; return 1;
    }
    /* A sleep has no dispatcher objects. A previous WaitAll flag must not
     * make that empty list immediately signaled and bypass its deadline. */
    if (!t->wait_n) return 0;
    if (t->wait_all) {
        for (int i = 0; i < t->wait_n; ++i) if (!xk_obj_signaled(t->wait_objs[i])) return 0;
        for (int i = 0; i < t->wait_n; ++i) xk_obj_consume(t->wait_objs[i], t);
        t->wait_result = 0; return 1;
    }
    for (int i = 0; i < t->wait_n; ++i) if (xk_obj_signaled(t->wait_objs[i])) { xk_obj_consume(t->wait_objs[i], t); t->wait_result = i; return 1; }
    return 0;
}

void xk_signal_check(void)
{
    for (xk_thread *t = g_threads; t; t = t->next)
        if (t->state == 1 && try_satisfy(t)) t->state = 0;
}

/* A kicked thread (see xk_thread_kick) runs on the very next switch: used to deliver Halo's vblank
 * callback on the vblank thread the moment the game's frame-pacing loop sleeps waiting for it, instead
 * of running the callback inline on the sleeping thread (that deadlocked the map-list loader) or
 * waiting for the 16.7 ms timer tick. */
static xk_thread *g_boost;
/* A runnable guest can do the scheduler's selection before handing back the
 * baton. Preserve that choice: probing a semaphore/event may consume it. */
static xk_thread *g_yield_next;
static unsigned g_yield_calls, g_yield_same, g_yield_handoffs;
static unsigned g_wait_dump_requested;
void xk_wait_stats_request(void)
{ __atomic_store_n(&g_wait_dump_requested,1,__ATOMIC_RELEASE); }
void xk_wait_stats_dump(void);
void xk_thread_kick(xk_thread *t)
{
    if (!t || t->state == 3) return;
    if (t->state == 1 && t->wait_n == 0 && !t->wait_word && t->wait_until) t->wait_until = now100();   /* its sleep expires now */
    g_boost = t;
}
static xk_thread *pick_next(xk_thread *after)
{
    if (g_boost) {
        xk_thread *b = g_boost; g_boost = NULL;
        if (b->state == 1 && b->wait_n == 0 && !b->wait_word && b->wait_until && now100() >= b->wait_until) { b->wait_result = -1; b->state = 0; }
        if (b->state == 0) return b;
    }
    /* round robin over ready threads, starting after `after` */
    xk_thread *start = after ? after->next : g_threads;
    for (int pass = 0; pass < 2; ++pass) {
        for (xk_thread *t = pass == 0 ? start : g_threads; t; t = t->next) {
            if (pass == 1 && t == start) break;
            if (t->state == 1 && (try_satisfy(t) || (t->wait_until && now100() >= t->wait_until && (t->wait_result = -1, 1)))) t->state = 0;
            if (t->state == 0) return t;
        }
    }
    return NULL;
}

void xd3d_ds_check(const char *where, uint32_t eip) __attribute__((weak));
void xk_yield(void)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    extern void xv_object_jobs_join(void);
    xv_object_jobs_join(); /* No outstanding object jobs when another fiber resumes. */
#endif
    xk_thread *me = xk_cur;
    if (__atomic_load_n(&g_wait_dump_requested,__ATOMIC_RELAXED) &&
        __atomic_exchange_n(&g_wait_dump_requested,0,__ATOMIC_ACQ_REL))
        xk_wait_stats_dump(); /* Guest-owned counters are never read/reset by the profiler thread. */
    me->ctx.eip_hint = X_M32(me->ctx.r[4]);          /* return address of the kernel call we are inside */
    if (xd3d_ds_check) xd3d_ds_check("yield", me->ctx.eip_hint);
    { static uint64_t last; static unsigned n; n++; uint64_t t = xk_os_monotonic_us();
      if (t - last > 3000000) { if (last && n > 2000) { XK_LOG("yield storm: %u yields in 3 s\n", n); xk_dump_threads(); } last = t; n = 0; } }
    X_M32(xk_var_KeTickCount) = xk_tick_count();
    g_yield_calls++;
    static int fast = -1;
    if (fast < 0) { const char *e=getenv("XV_FAST_YIELD"); fast=!e||atoi(e)!=0; }
    if (fast && me->state == 0) {
        xk_thread *next = pick_next(me);
        if (next == me) { g_yield_same++; return; }
        g_yield_next = next;
    }
    g_yield_handoffs++;
    /* the sampling profiler reads a global "current guest function": keep it per thread across switches,
     * otherwise time after a resume is charged to whatever the other thread last entered */
    extern volatile uint32_t xv_cur_fn __attribute__((weak));
    uint32_t saved_fn = &xv_cur_fn ? xv_cur_fn : 0;
    if (xv_phase_suspend) xv_phase_suspend(&me->ctx);
    xk_os_fiber_switch(xk_os_fiber_main());           /* back to the scheduler loop */
    xk_cur = me;
    if (xv_phase_resume) xv_phase_resume(&me->ctx);
    if (&xv_cur_fn) xv_cur_fn = saved_fn;
}

void xk_dump_threads(void)
{
    static const char *tn[] = { "none", "file", "dir", "event", "mutant", "sem", "thread", "symlink", "timer" };
    for (xk_thread *t = g_threads; t; t = t->next) {
        char w[256] = ""; int n = 0;
        for (int i = 0; i < t->wait_n; ++i) n += snprintf(w + n, sizeof w - n, "%s%s%s@%08X", i ? "," : "", tn[t->wait_objs[i]->type], t->wait_objs[i]->type == XO_EVENT ? (t->wait_objs[i]->u.event.signaled ? "(S)" : "(-)") : "", t->wait_objs[i]->guest);
        XK_LOG("  thread %d state %d start %08X eip~%08X wait[%s]%s%s deadline %+lld ms\n", t->id, t->state, t->start_routine, t->ctx.eip_hint, w,
               t->wait_all ? " all" : "", t->wait_n == 0 && t->state == 1 ? " (sleep)" : "", t->wait_until ? (long long)((int64_t)t->wait_until - (int64_t)now100()) / 10000 : 0LL);
        if (t->state == 0) {
            char d[400]; int k = 0; uint32_t sp = t->ctx.r[4];
            for (int i = 0; i < 20; ++i) k += snprintf(d + k, sizeof d - k, " %08X", X_M32(sp + 4u * i));
            XK_LOG("    stack @%08X:%s\n", sp, d);
        }
    }
}

static uint64_t g_idle_us; static unsigned g_idle_n;
void xk_run_until_idle(void)
{
    xk_thread *last = NULL; int idle_spins = 0;
    for (;;) {
        X_M32(xk_var_KeTickCount) = xk_tick_count();
        xk_thread *t = g_yield_next;
        g_yield_next = NULL;
        if (!t) t = pick_next(last);
        if (!t) {
            /* everyone blocked: sleep until the earliest timeout, or quit if nothing can ever wake */
            uint64_t earliest = 0; int any = 0, alive = 0;
            for (xk_thread *x = g_threads; x; x = x->next) { if (x->state != 3) alive++; if (x->state == 1 && x->wait_until && (!earliest || x->wait_until < earliest)) { earliest = x->wait_until; any = 1; } }
            if (!alive) { XK_LOG("all threads exited\n"); return; }
            if (!any) { if (++idle_spins > 3) { XK_LOG("deadlock: %d threads blocked forever\n", alive); xk_dump_threads(); return; } xk_os_scheduler_wait(1000); continue; }
            uint64_t n = now100();
            if (earliest > n + 1000000) { static unsigned dumps; if (dumps++ < 6) { XK_LOG("scheduler idle for %llu ms:\n", (unsigned long long)(earliest - n) / 10000); xk_dump_threads(); } }
            if (earliest > n) {
                uint64_t us = (earliest - n) / 10 + 1;
                uint64_t started = xk_os_monotonic_us();
                xk_os_scheduler_wait(us);
                g_idle_us += xk_os_monotonic_us() - started; g_idle_n++;
            }
            continue;
        }
        idle_spins = 0;
        xk_cur = t; last = t;
        xk_os_fiber_switch(t->fiber);
        xk_cur = NULL;
        if (t->state == 3) { xk_os_fiber_destroy(t->fiber); t->fiber = NULL; }
    }
}

void xk_apc_queue(xk_thread *t, uint32_t routine, uint32_t a1, uint32_t a2, uint32_t a3)
{
    if (!t || t->napc >= 16) { XK_LOG("APC queue full\n"); return; }
    t->apc[t->napc].routine = routine; t->apc[t->napc].a1 = a1; t->apc[t->napc].a2 = a2; t->apc[t->napc].a3 = a3; t->napc++;
    { static unsigned n; if (n++ < 400) XK_LOG("APC queued on thread %d: %08X(%08X, %08X, %08X)\n", t->id, routine, a1, a2, a3); }
    if (t->state == 1 && t->alertable) t->state = 0;              /* wake an alertable waiter */
}
int xk_apc_deliver(xctx *c)
{
    xk_thread *t = xk_cur; int n = 0;
    while (t->napc) {
        uint32_t r = t->apc[0].routine, a1 = t->apc[0].a1, a2 = t->apc[0].a2, a3 = t->apc[0].a3;
        memmove(&t->apc[0], &t->apc[1], (size_t)(t->napc - 1) * sizeof t->apc[0]); t->napc--;
        X_PUSH32(a3); X_PUSH32(a2); X_PUSH32(a1); X_PUSH32(0xDEAD0004u);
        xv_call(c, r); n++;
    }
    return n;
}

/* ---- blocked-time accounting (dumped with the sampling profiler): who waits, on what, for how long ---- */
static struct { int tid; int kind; uint32_t key; uint32_t eip; unsigned n; uint64_t us; } g_ws[64]; static unsigned g_nws;
static void ws_add(int tid, int kind, uint32_t key, uint32_t eip, uint64_t us)
{
    for (unsigned i = 0; i < g_nws; ++i) if (g_ws[i].tid == tid && g_ws[i].kind == kind && g_ws[i].key == key) { g_ws[i].n++; g_ws[i].us += us; return; }
    if (g_nws < 64) { g_ws[g_nws].tid = tid; g_ws[g_nws].kind = kind; g_ws[g_nws].key = key; g_ws[g_nws].eip = eip; g_ws[g_nws].n = 1; g_ws[g_nws].us = us; g_nws++; }
}
void xk_wait_stats_dump(void)
{
    static const char *kn[] = { "?", "event", "mutant", "sem", "thread", "timer", "delay", "obj" };
    for (unsigned i = 0; i < g_nws; ++i) for (unsigned j = i + 1; j < g_nws; ++j) if (g_ws[j].us > g_ws[i].us) { typeof(g_ws[0]) tmp = g_ws[i]; g_ws[i] = g_ws[j]; g_ws[j] = tmp; }
    char line[240]; int ln = 0;
    ln += snprintf(line + ln, sizeof line - ln, "[wait] idle %llu ms (%u sleeps);", (unsigned long long)(g_idle_us / 1000), g_idle_n);
    for (unsigned i = 0; i < g_nws && i < 8; ++i) {
        ln += snprintf(line + ln, sizeof line - ln, " t%d %s@%X<-%X %ums/%u", g_ws[i].tid, kn[g_ws[i].kind], g_ws[i].key, g_ws[i].eip, (unsigned)(g_ws[i].us / 1000), g_ws[i].n);
        if (ln > 180) { XK_LOG("%s\n", line); ln = 0; }
    }
    if (ln) XK_LOG("%s\n", line);
    XK_LOG("[guest-yield] calls %u same-thread %u scheduler-handoffs %u\n",
        g_yield_calls,g_yield_same,g_yield_handoffs);
    g_yield_calls=g_yield_same=g_yield_handoffs=0;
    g_nws = 0; g_idle_us = 0; g_idle_n = 0;
}
/* NTSTATUS-style wait: returns STATUS_WAIT_n / STATUS_TIMEOUT.  timeout: NULL = infinite, negative = relative 100ns, positive = absolute. */
uint32_t xk_wait(xk_obj **objs, int n, int wait_all, int alertable, const int64_t *timeout)
{
    xk_thread *t = xk_cur;
    if (alertable && t->napc) { xk_apc_deliver(&t->ctx); return STATUS_USER_APC; }
    t->wait_n = n; t->wait_all = wait_all; t->alertable = alertable;
    for (int i = 0; i < n; ++i) t->wait_objs[i] = objs[i];
    static unsigned wlog; int dbg = (t->id == 24 || (n == 1 && objs[0]->type == XO_MUTANT)) && wlog < 80;
    if (try_satisfy(t)) { if (dbg) { wlog++; XK_LOG("[wait] t%d %s@%08X/obj %p immediate -> %d (owner t%d cnt %d)\n", t->id, objs[0]->type == XO_MUTANT ? "mutant" : "obj", objs[0]->guest, (void *)objs[0], t->wait_result, objs[0]->type == XO_MUTANT && objs[0]->u.mutant.owner ? objs[0]->u.mutant.owner->id : -1, objs[0]->type == XO_MUTANT ? objs[0]->u.mutant.count : 0); } return (uint32_t)t->wait_result; }
    if (timeout && *timeout == 0) { if (dbg) { wlog++; XK_LOG("[wait] t%d poll timeout on %s@%08X/obj %p type %d (event signaled %d manual %d, mutant owner t%d)\n", t->id, objs[0]->type == XO_MUTANT ? "mutant" : "obj", objs[0]->guest, (void *)objs[0], objs[0]->type, objs[0]->type == XO_EVENT ? objs[0]->u.event.signaled : -1, objs[0]->type == XO_EVENT ? objs[0]->u.event.manual : -1, objs[0]->type == XO_MUTANT && objs[0]->u.mutant.owner ? objs[0]->u.mutant.owner->id : -1); } return STATUS_TIMEOUT; }
    t->wait_until = 0;
    if (timeout) t->wait_until = *timeout < 0 ? now100() + (uint64_t)(-*timeout) : (uint64_t)*timeout - (xk_time_100ns() - now100());
    if (dbg) { wlog++; XK_LOG("[wait] t%d blocks on %s@%08X type %d timeout %lld (owner t%d)\n", t->id, objs[0]->type == XO_MUTANT ? "mutant" : "obj", objs[0]->guest, objs[0]->type, timeout ? (long long)*timeout : 0LL, objs[0]->type == XO_MUTANT && objs[0]->u.mutant.owner ? objs[0]->u.mutant.owner->id : -1); }
    t->state = 1;
    { uint64_t t0 = xk_os_monotonic_us(); uint32_t eip = X_M32(t->ctx.r[4]);
      xk_yield();
      { xk_obj *o = objs[0]; int k = o->type == XO_EVENT ? 1 : o->type == XO_MUTANT ? 2 : o->type == XO_SEMAPHORE ? 3 : o->type == XO_THREAD ? 4 : o->type == XO_TIMER ? 5 : 7;
        ws_add(t->id, k, o->guest ? o->guest : (uint32_t)(uintptr_t)o, eip, xk_os_monotonic_us() - t0); } }
    t->state = 0; t->wait_n = 0;
    if (dbg) XK_LOG("[wait] t%d woke result %d\n", t->id, t->wait_result);
    if (alertable && t->napc) { t->alertable = 0; xk_apc_deliver(&t->ctx); return STATUS_USER_APC; }
    return t->wait_result < 0 ? STATUS_TIMEOUT : (uint32_t)t->wait_result;
}

static xk_obj *sync_from_handle(uint32_t h) { xk_obj *o = xk_handle_get(h); return o; }
static int64_t *timeout_ptr(uint32_t p, int64_t *store) { if (!p) return NULL; *store = (int64_t)LI64(p); return store; }

/* ---- Ps / thread exports ------------------------------------------------------------------------ */
/* NTSTATUS PsCreateSystemThreadEx(PHANDLE, ULONG ThreadExtensionSize, ULONG KernelStackSize, ULONG TlsDataSize, PHANDLE ThreadId,
   PKSTART_ROUTINE StartRoutine, PVOID StartContext, BOOLEAN CreateSuspended, BOOLEAN DebuggerThread, PKSYSTEM_ROUTINE SystemRoutine) */
void xk_PsCreateSystemThreadEx(xctx *c)
{
    xk_thread *t = xk_thread_create(X_ARG(2), X_ARG(3), X_ARG(5), X_ARG(6), X_ARG(9), X_ARG(7) & 0xFF);
    if (!t) { c->r[0] = STATUS_NO_MEMORY; X_RET(10); }
    XK_LOG("  (suspended=%u debugger=%u)\n", X_ARG(7) & 0xFF, X_ARG(8) & 0xFF);
    X_M32(X_ARG(0)) = xk_handle_create(t->obj);
    if (X_ARG(4)) X_M32(X_ARG(4)) = (uint32_t)t->id;
    c->r[0] = STATUS_SUCCESS; X_RET(10);
}
void xk_PsCreateSystemThread(xctx *c)
{
    xk_thread *t = xk_thread_create(X_ARG(2), 0, X_ARG(4), X_ARG(5), 0, X_ARG(6) & 0xFF);
    if (!t) { c->r[0] = STATUS_NO_MEMORY; X_RET(7); }
    X_M32(X_ARG(0)) = xk_handle_create(t->obj); if (X_ARG(3)) X_M32(X_ARG(3)) = (uint32_t)t->id;
    c->r[0] = STATUS_SUCCESS; X_RET(7);
}
void xk_PsTerminateSystemThread(xctx *c) { xk_thread_exit(X_ARG(0)); }
void xk_KeGetCurrentThread(xctx *c) { c->r[0] = xk_cur->kthread; X_RET(0); }
void xk_NtResumeThread(xctx *c)
{
    xk_obj *o = xk_handle_get_type(X_ARG(0), XO_THREAD);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(2); }
    xk_thread *t = o->u.thread; if (X_ARG(1)) X_M32(X_ARG(1)) = (uint32_t)t->suspend_count;
    XK_LOG("NtResumeThread(thread %d, count %d)\n", t->id, t->suspend_count);
    if (t->suspend_count > 0 && --t->suspend_count == 0 && t->state == 2) t->state = 0;
    c->r[0] = STATUS_SUCCESS; X_RET(2);
}
void xk_NtSuspendThread(xctx *c)
{
    xk_obj *o = xk_handle_get_type(X_ARG(0), XO_THREAD);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(2); }
    xk_thread *t = o->u.thread; if (X_ARG(1)) X_M32(X_ARG(1)) = (uint32_t)t->suspend_count;
    XK_LOG("NtSuspendThread(thread %d)\n", t->id);
    t->suspend_count++; if (t->state == 0) t->state = 2;
    c->r[0] = STATUS_SUCCESS;
    if (t == xk_cur) xk_yield();
    X_RET(2);
}
void xk_KeSuspendThread(xctx *c) { XK_LOG("KeSuspendThread(%08X)\n", X_ARG(0)); xk_obj *o = xk_obj_from_guest(X_ARG(0)); if (o) { o->u.thread->suspend_count++; if (o->u.thread->state == 0) o->u.thread->state = 2; } c->r[0] = 0; X_RET(1); }
void xk_KeResumeThread(xctx *c) { XK_LOG("KeResumeThread(%08X)\n", X_ARG(0)); xk_obj *o = xk_obj_from_guest(X_ARG(0)); if (o && o->u.thread->suspend_count > 0 && --o->u.thread->suspend_count == 0) o->u.thread->state = 0; c->r[0] = 0; X_RET(1); }
int xd3d_vblank_kick(xctx *c, uint32_t eip) __attribute__((weak));
void xk_NtYieldExecution(xctx *c) { { static unsigned n; if (n++ < 30) XK_LOG("[wait] t%d NtYieldExecution\n", xk_cur ? xk_cur->id : -1); }
    if (xd3d_vblank_kick) xd3d_vblank_kick(c, X_M32(c->r[4]));      /* advance the vblank if this is the frame-pacing loop, then still yield */
    xk_yield(); c->r[0] = STATUS_SUCCESS; X_RET(0); }
void xk_KeSetBasePriorityThread(xctx *c) { int old = (int8_t)X_M8(X_ARG(0) + KTHREAD_BASEPRIORITY); X_M8(X_ARG(0) + KTHREAD_BASEPRIORITY) = (uint8_t)(8 + (int32_t)X_ARG(1)); c->r[0] = (uint32_t)(old - 8); X_RET(2); }
void xk_KeSetPriorityThread(xctx *c) { int old = X_M8(X_ARG(0) + KTHREAD_PRIORITY); X_M8(X_ARG(0) + KTHREAD_PRIORITY) = (uint8_t)X_ARG(1); c->r[0] = (uint32_t)old; X_RET(2); }
void xk_KeQueryBasePriorityThread(xctx *c) { c->r[0] = (uint32_t)((int8_t)X_M8(X_ARG(0) + KTHREAD_BASEPRIORITY) - 8); X_RET(1); }
void xk_KeSetDisableBoostThread(xctx *c) { c->r[0] = 0; X_RET(2); }
void xk_KeBoostPriorityThread(xctx *c) { X_RET(2); }
void xk_KeAlertThread(xctx *c) { c->r[0] = 0; X_RET(2); }
void xk_KeTestAlertThread(xctx *c) { c->r[0] = 0; X_RET(1); }
void xk_KeEnterCriticalRegion(xctx *c) { X_RET(0); }
void xk_KeLeaveCriticalRegion(xctx *c) { X_RET(0); }
void xk_KfRaiseIrql(xctx *c) { c->r[0] = 0; X_RET(0); }            /* fastcall: new irql in ecx; returns old */
void xk_KfLowerIrql(xctx *c) { X_RET(0); }
void xk_KeRaiseIrqlToDpcLevel(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_KeRaiseIrqlToSynchLevel(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_KeGetCurrentIrql(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_KeIsExecutingDpc(xctx *c) { c->r[0] = 0; X_RET(0); }
void xk_KeSaveFloatingPointState(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(1); }
void xk_KeRestoreFloatingPointState(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(1); }
void xk_KeBugCheck(xctx *c) { XK_LOG("KeBugCheck(%08X)\n", X_ARG(0)); xv_trap(c, 0); }
void xk_KeBugCheckEx(xctx *c) { XK_LOG("KeBugCheckEx(%08X)\n", X_ARG(0)); xv_trap(c, 0); }

/* ---- delays / time ---------------------------------------------------------------------------- */
void xk_KeDelayExecutionThread(xctx *c)
{
    int64_t to = (int64_t)LI64(X_ARG(2)); int alertable = X_ARG(1) & 0xFF;
    xk_thread *t = xk_cur;
    { static unsigned n; if (n++ < 30) XK_LOG("[wait] t%d KeDelayExecutionThread %lld\n", t->id, (long long)to); }
    if (alertable && t->napc) { xk_apc_deliver(c); c->r[0] = STATUS_USER_APC; X_RET(3); }
    /* Halo's vblank wait is a Sleep(1) loop (0xBB060 -> Sleep 0x12EBB -> SleepEx -> here with -10000) */
    /* frame-pacing Sleep(1): advance the vblank now and turn the sleep into a plain yield (other guest
     * threads must still get to run - returning without yielding starved them and froze the UI) */
    if ((to == 0 || (to < 0 && to >= -30000)) && xd3d_vblank_kick && xd3d_vblank_kick(c, X_M32(c->r[4]))) { xk_yield(); c->r[0] = STATUS_SUCCESS; X_RET(3); }
    t->wait_n = 0; t->alertable = alertable;
    t->wait_until = to < 0 ? now100() + (uint64_t)(-to) : (to == 0 ? now100() : (uint64_t)to - (xk_time_100ns() - now100()));
    { uint64_t t0 = xk_os_monotonic_us(); uint32_t eip = X_M32(c->r[4]);
      t->state = 1; xk_yield(); t->state = 0; t->alertable = 0;
      ws_add(t->id, 6, (uint32_t)(to < 0 ? -to / 10000 : 0), eip, xk_os_monotonic_us() - t0); }
    if (alertable && t->napc) { xk_apc_deliver(c); c->r[0] = STATUS_USER_APC; X_RET(3); }
    c->r[0] = STATUS_SUCCESS; X_RET(3);
}
void xk_KeStallExecutionProcessor(xctx *c) { xk_os_sleep_us(X_ARG(0)); X_RET(1); }
void xk_KeQuerySystemTime(xctx *c) { LI64(X_ARG(0)) = xk_time_100ns(); X_RET(1); }
void xk_KeQueryInterruptTime(xctx *c) { uint64_t t = xk_uptime_100ns(); c->r[0] = (uint32_t)t; c->r[2] = (uint32_t)(t >> 32); X_RET(0); }
void xk_KeQueryPerformanceCounter(xctx *c) { uint64_t t = x_rdtsc(); c->r[0] = (uint32_t)t; c->r[2] = (uint32_t)(t >> 32); X_RET(0); }
void xk_KeQueryPerformanceFrequency(xctx *c) { c->r[0] = 733333333u; c->r[2] = 0; X_RET(0); }
void xk_NtSetSystemTime(xctx *c) { c->r[0] = STATUS_SUCCESS; X_RET(2); }

/* ---- events ------------------------------------------------------------------------------------ */
/* NTSTATUS NtCreateEvent(PHANDLE, POBJECT_ATTRIBUTES, EVENT_TYPE (0 notification/manual, 1 synchronization/auto), BOOLEAN InitialState) */
void xk_NtCreateEvent(xctx *c)
{
    xk_obj *o = xk_obj_new(XO_EVENT); o->u.event.manual = X_ARG(2) == 0; o->u.event.signaled = X_ARG(3) & 0xFF;
    X_M32(X_ARG(0)) = xk_handle_create(o); xk_obj_deref(o);
    c->r[0] = STATUS_SUCCESS; X_RET(4);
}
void xk_NtSetEvent(xctx *c) { xk_obj *o = xk_handle_get_type(X_ARG(0), XO_EVENT); if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(2); } if (X_ARG(1)) X_M32(X_ARG(1)) = o->u.event.signaled; o->u.event.signaled = 1; xk_signal_check(); c->r[0] = STATUS_SUCCESS; X_RET(2); }
void xk_NtClearEvent(xctx *c) { xk_obj *o = xk_handle_get_type(X_ARG(0), XO_EVENT); if (o) o->u.event.signaled = 0; c->r[0] = o ? STATUS_SUCCESS : STATUS_INVALID_HANDLE; X_RET(1); }
void xk_NtPulseEvent(xctx *c) { xk_obj *o = xk_handle_get_type(X_ARG(0), XO_EVENT); if (o) { o->u.event.signaled = 1; xk_signal_check(); o->u.event.signaled = 0; } c->r[0] = o ? STATUS_SUCCESS : STATUS_INVALID_HANDLE; X_RET(2); }
void xk_NtQueryEvent(xctx *c) { xk_obj *o = xk_handle_get_type(X_ARG(0), XO_EVENT); if (o) { X_M32(X_ARG(1)) = o->u.event.manual ? 0 : 1; X_M32(X_ARG(1) + 4) = o->u.event.signaled; } c->r[0] = o ? STATUS_SUCCESS : STATUS_INVALID_HANDLE; X_RET(2); }
/* KEVENT in guest memory: KeInitializeEvent(PKEVENT, EVENT_TYPE, BOOLEAN State) */
void xk_KeInitializeEvent(xctx *c)
{
    uint32_t e = X_ARG(0); X_M8(e) = (uint8_t)X_ARG(1); X_M8(e + 2) = 4; X_M32(e + 4) = X_ARG(2) & 0xFF;
    xk_obj *o = xk_obj_from_guest(e); if (o) { o->u.event.manual = X_ARG(1) == 0; o->u.event.signaled = X_ARG(2) & 0xFF; } else guest_obj(e, XO_EVENT);
    X_RET(3);
}
void xk_KeSetEvent(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_EVENT); c->r[0] = o->u.event.signaled; o->u.event.signaled = 1; sync_guest(o); xk_signal_check(); X_RET(3); }
void xk_KeResetEvent(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_EVENT); c->r[0] = o->u.event.signaled; o->u.event.signaled = 0; sync_guest(o); X_RET(1); }
void xk_KePulseEvent(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_EVENT); c->r[0] = o->u.event.signaled; o->u.event.signaled = 1; xk_signal_check(); o->u.event.signaled = 0; sync_guest(o); X_RET(3); }
void xk_KeSetEventBoostPriority(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_EVENT); o->u.event.signaled = 1; sync_guest(o); xk_signal_check(); X_RET(2); }

/* ---- mutants / semaphores ---------------------------------------------------------------------- */
void xk_NtCreateMutant(xctx *c) { xk_obj *o = xk_obj_new(XO_MUTANT); if (X_ARG(2) & 0xFF) { o->u.mutant.owner = xk_cur; o->u.mutant.count = 1; } X_M32(X_ARG(0)) = xk_handle_create(o); xk_obj_deref(o); c->r[0] = STATUS_SUCCESS; X_RET(3); }
void xk_NtReleaseMutant(xctx *c)
{
    xk_obj *o = xk_handle_get_type(X_ARG(0), XO_MUTANT); if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(2); }
    { static unsigned n; if (n++ < 60) XK_LOG("[wait] t%d NtReleaseMutant h=%X (owner t%d cnt %d)\n", xk_cur->id, X_ARG(0), o->u.mutant.owner ? o->u.mutant.owner->id : -1, o->u.mutant.count); }
    if (X_ARG(1)) X_M32(X_ARG(1)) = (uint32_t)(1 - o->u.mutant.count);
    if (o->u.mutant.owner == xk_cur && --o->u.mutant.count == 0) { o->u.mutant.owner = NULL; xk_signal_check(); }
    c->r[0] = STATUS_SUCCESS; X_RET(2);
}
void xk_KeInitializeMutant(xctx *c) { uint32_t m = X_ARG(0); X_M8(m) = 2; X_M32(m + 4) = X_ARG(1) & 0xFF ? 0 : 1; xk_obj *o = guest_obj(m, XO_MUTANT); if (X_ARG(1) & 0xFF) { o->u.mutant.owner = xk_cur; o->u.mutant.count = 1; } X_RET(2); }
void xk_KeReleaseMutant(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_MUTANT); c->r[0] = (uint32_t)(1 - o->u.mutant.count); if (o->u.mutant.owner == xk_cur && --o->u.mutant.count == 0) { o->u.mutant.owner = NULL; xk_signal_check(); } sync_guest(o); X_RET(4); }
void xk_NtCreateSemaphore(xctx *c) { xk_obj *o = xk_obj_new(XO_SEMAPHORE); o->u.sem.count = (int)X_ARG(2); o->u.sem.limit = (int)X_ARG(3); X_M32(X_ARG(0)) = xk_handle_create(o); xk_obj_deref(o); c->r[0] = STATUS_SUCCESS; X_RET(4); }
void xk_NtReleaseSemaphore(xctx *c) { xk_obj *o = xk_handle_get_type(X_ARG(0), XO_SEMAPHORE); if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(3); } if (X_ARG(2)) X_M32(X_ARG(2)) = (uint32_t)o->u.sem.count; o->u.sem.count += (int)X_ARG(1); xk_signal_check(); c->r[0] = STATUS_SUCCESS; X_RET(3); }
void xk_KeInitializeSemaphore(xctx *c) { uint32_t s = X_ARG(0); X_M8(s) = 5; X_M32(s + 4) = X_ARG(1); xk_obj *o = guest_obj(s, XO_SEMAPHORE); o->u.sem.count = (int)X_ARG(1); o->u.sem.limit = (int)X_ARG(2); X_RET(3); }
void xk_KeReleaseSemaphore(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_SEMAPHORE); c->r[0] = (uint32_t)o->u.sem.count; o->u.sem.count += (int)X_ARG(2); sync_guest(o); xk_signal_check(); X_RET(4); }

/* ---- timers / DPCs (minimal) ------------------------------------------------------------------ */
void xk_KeInitializeTimerEx(xctx *c) { uint32_t t = X_ARG(0); X_M8(t) = 8 + (X_ARG(1) & 1); X_M32(t + 4) = 0; guest_obj(t, XO_TIMER); X_RET(2); }
void xk_KeSetTimerEx(xctx *c)
{
    xk_obj *o = guest_obj(X_ARG(0), XO_TIMER); int64_t due = (int64_t)LI64(X_ARG(1)); int32_t period_ms = (int32_t)X_ARG(3);
    c->r[0] = o->u.timer.due != 0;
    o->u.timer.due = due < 0 ? now100() + (uint64_t)(-due) : (due ? (uint64_t)due - (xk_time_100ns() - now100()) : now100());
    o->u.timer.period = (int64_t)period_ms * 10000; o->u.timer.signaled = 0;
    if (X_ARG(4)) XK_LOG("KeSetTimerEx with DPC %08X (DPCs are not dispatched)\n", X_ARG(4));
    X_RET(5);
}
void xk_KeSetTimer(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_TIMER); int64_t due = (int64_t)LI64(X_ARG(1)); c->r[0] = o->u.timer.due != 0; o->u.timer.due = due < 0 ? now100() + (uint64_t)(-due) : now100(); o->u.timer.period = 0; o->u.timer.signaled = 0; X_RET(3); }
void xk_KeCancelTimer(xctx *c) { xk_obj *o = guest_obj(X_ARG(0), XO_TIMER); c->r[0] = o->u.timer.due != 0; o->u.timer.due = 0; X_RET(1); }
void xk_KeInitializeDpc(xctx *c) { X_M32(X_ARG(0) + 4) = X_ARG(1); X_M32(X_ARG(0) + 8) = X_ARG(2); X_RET(3); }
void xk_KeInsertQueueDpc(xctx *c) { XK_LOG("KeInsertQueueDpc: running DPC %08X inline\n", X_M32(X_ARG(0) + 4)); uint32_t dpc = X_ARG(0); X_PUSH32(X_ARG(2)); X_PUSH32(X_ARG(1)); X_PUSH32(X_M32(dpc + 8)); X_PUSH32(dpc); X_PUSH32(0xDEAD0002u); xv_call(c, X_M32(dpc + 4)); c->r[0] = 1; X_RET(3); }
void xk_KeRemoveQueueDpc(xctx *c) { c->r[0] = 0; X_RET(1); }
void xk_KeConnectInterrupt(xctx *c) { c->r[0] = 1; X_RET(1); }
void xk_KeDisconnectInterrupt(xctx *c) { c->r[0] = 1; X_RET(1); }
void xk_KeInitializeInterrupt(xctx *c) { X_RET(7); }
void xk_KeSynchronizeExecution(xctx *c) { X_PUSH32(X_ARG(2)); X_PUSH32(0xDEAD0003u); xv_call(c, X_ARG(1)); X_RET(3); }

/* ---- waits ------------------------------------------------------------------------------------ */
void xk_NtWaitForSingleObject(xctx *c)
{
    xk_obj *o = sync_from_handle(X_ARG(0)); int64_t st; int64_t *to = timeout_ptr(X_ARG(2), &st);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(3); }
    c->r[0] = xk_wait(&o, 1, 0, X_ARG(1) & 0xFF, to); X_RET(3);
}
void xk_NtWaitForSingleObjectEx(xctx *c)
{
    xk_obj *o = sync_from_handle(X_ARG(0)); int64_t st; int64_t *to = timeout_ptr(X_ARG(3), &st);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(4); }
    c->r[0] = xk_wait(&o, 1, 0, X_ARG(2) & 0xFF, to); X_RET(4);
}
/* NTSTATUS NtWaitForMultipleObjectsEx(ULONG Count, PHANDLE Handles, WAIT_TYPE (0 all,1 any), KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER) */
void xk_NtWaitForMultipleObjectsEx(xctx *c)
{
    uint32_t n = X_ARG(0), hs = X_ARG(1); xk_obj *objs[16]; if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; ++i) { objs[i] = sync_from_handle(X_M32(hs + i * 4)); if (!objs[i]) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(6); } }
    int64_t st; int64_t *to = timeout_ptr(X_ARG(5), &st);
    c->r[0] = xk_wait(objs, (int)n, X_ARG(2) == 0, X_ARG(4) & 0xFF, to); X_RET(6);
}
/* NTSTATUS KeWaitForSingleObject(PVOID Object, KWAIT_REASON, KPROCESSOR_MODE, BOOLEAN Alertable, PLARGE_INTEGER Timeout) */
void xk_KeWaitForSingleObject(xctx *c)
{
    xk_obj *o = xk_obj_from_guest(X_ARG(0)); if (!o) o = guest_obj(X_ARG(0), X_M8(X_ARG(0)) == 6 ? XO_THREAD : X_M8(X_ARG(0)) == 2 ? XO_MUTANT : X_M8(X_ARG(0)) == 5 ? XO_SEMAPHORE : X_M8(X_ARG(0)) >= 8 ? XO_TIMER : XO_EVENT);
    int64_t st; int64_t *to = timeout_ptr(X_ARG(4), &st);
    c->r[0] = xk_wait(&o, 1, 0, X_ARG(3) & 0xFF, to); X_RET(5);
}
/* KeWaitForMultipleObjects(Count, Object[], WaitType, Reason, Mode, Alertable, Timeout, WaitBlockArray) */
void xk_KeWaitForMultipleObjects(xctx *c)
{
    uint32_t n = X_ARG(0), ps = X_ARG(1); xk_obj *objs[16]; if (n > 16) n = 16;
    for (uint32_t i = 0; i < n; ++i) { uint32_t g = X_M32(ps + i * 4); objs[i] = xk_obj_from_guest(g); if (!objs[i]) objs[i] = guest_obj(g, XO_EVENT); }
    int64_t st; int64_t *to = timeout_ptr(X_ARG(6), &st);
    c->r[0] = xk_wait(objs, (int)n, X_ARG(2) == 0, X_ARG(5) & 0xFF, to); X_RET(8);
}
void xk_NtSignalAndWaitForSingleObjectEx(xctx *c)
{
    xk_obj *s = sync_from_handle(X_ARG(0)), *w = sync_from_handle(X_ARG(1));
    if (s) { if (s->type == XO_EVENT) s->u.event.signaled = 1; else if (s->type == XO_MUTANT && s->u.mutant.owner == xk_cur && --s->u.mutant.count == 0) s->u.mutant.owner = NULL; else if (s->type == XO_SEMAPHORE) s->u.sem.count++; xk_signal_check(); }
    int64_t st; int64_t *to = timeout_ptr(X_ARG(4), &st);
    c->r[0] = w ? xk_wait(&w, 1, 0, X_ARG(3) & 0xFF, to) : STATUS_INVALID_HANDLE; X_RET(5);
}

/* ---- Ob ----------------------------------------------------------------------------------------- */
static uint32_t obj_guest_body(xk_obj *o)
{
    if (o->guest) return o->guest;
    /* materialise a real dispatcher header so Ke* calls on the returned pointer reach this object */
    o->guest = xk_kalloc(16);
    uint8_t type = o->type == XO_EVENT ? (o->u.event.manual ? 0 : 1) : o->type == XO_MUTANT ? 2 : o->type == XO_SEMAPHORE ? 5 : o->type == XO_TIMER ? 8 : 6;
    X_M8(o->guest) = type; X_M8(o->guest + 2) = 4;
    X_M32(o->guest + 4) = o->type == XO_EVENT ? (uint32_t)o->u.event.signaled : o->type == XO_SEMAPHORE ? (uint32_t)o->u.sem.count : 0;
    if (g_ngmap < 1024) g_gmap[g_ngmap++] = (gmap_t){ o->guest, o };
    return o->guest;
}
void xk_ObReferenceObjectByHandle(xctx *c) { xk_obj *o = xk_handle_get(X_ARG(0)); if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(3); } xk_obj_ref(o); X_M32(X_ARG(2)) = obj_guest_body(o); c->r[0] = STATUS_SUCCESS; X_RET(3); }
void xk_ObfDereferenceObject(xctx *c) { xk_obj *o = xk_obj_from_guest(c->r[1]); if (o) xk_obj_deref(o); X_RET(0); }
void xk_ObfReferenceObject(xctx *c) { xk_obj *o = xk_obj_from_guest(c->r[1]); if (o) xk_obj_ref(o); X_RET(0); }
void xk_NtDuplicateObject(xctx *c) { xk_obj *o = xk_handle_get(X_ARG(0)); if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(3); } X_M32(X_ARG(1)) = xk_handle_create(o); c->r[0] = STATUS_SUCCESS; X_RET(3); }

void xk_NtQueueApcThread(xctx *c)
{
    xk_obj *o = xk_handle_get_type(X_ARG(0), XO_THREAD);
    if (!o) { c->r[0] = STATUS_INVALID_HANDLE; X_RET(5); }
    xk_apc_queue(o->u.thread, X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(4));
    c->r[0] = STATUS_SUCCESS; X_RET(5);
}
/* VOID NtUserIoApcDispatcher(PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, ULONG Reserved): the kernel trampoline
   XAPI's ReadFileEx/WriteFileEx pass as ApcRoutine; ApcContext is the user's completion routine and IoStatusBlock the
   OVERLAPPED.  Calls routine(dwErrorCode, dwBytesTransferred, lpOverlapped). */
void xk_RtlNtStatusToDosError(xctx *c);
void xk_NtUserIoApcDispatcher(xctx *c)
{
    uint32_t routine = X_ARG(0) & ~3u, iosb = X_ARG(1);
    uint32_t status = IOSB_STATUS(iosb), err = 0;
    { static unsigned n; if (n++ < 400) XK_LOG("NtUserIoApcDispatcher -> %08X(status %08X, bytes %u, ovl %08X)\n", routine, status, IOSB_INFO(iosb), iosb); }
    if (status & 0x80000000u) { X_PUSH32(status); X_PUSH32(0); xk_RtlNtStatusToDosError(c); err = c->r[0]; }
    X_PUSH32(iosb); X_PUSH32(IOSB_INFO(iosb)); X_PUSH32(err); X_PUSH32(0xDEAD0005u);
    xv_call(c, routine);
    X_RET(3);
}

/* ---- init -------------------------------------------------------------------------------------- */
void xk_threads_init(uint32_t tls_dir)
{
    g_boot_us = xk_os_monotonic_us();
    g_tls_dir = tls_dir;
}
