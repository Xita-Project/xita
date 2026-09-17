/* Diagnostic only: completed/partial outer elapsed on the presenting owner.
 * Includes blocking, callbacks, scheduling and nested work, never CPU self time.
 * Generated call sites are only FA920 and BCB30; no guest state is modified. */
#include "xk.h"
#include "xk_owner_phase.h"
#include "xk_object_jobs.h"
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
typedef SceUID phase_thread;
static phase_thread current_thread(void) { return sceKernelGetThreadId(); }
#else
#include <pthread.h>
typedef pthread_t phase_thread;
static phase_thread current_thread(void) { return pthread_self(); }
#endif
#ifndef XV_OWNER_PHASE_DEFAULT
#define XV_OWNER_PHASE_DEFAULT 0
#endif
#if XV_OWNER_PHASE_DEFAULT != 0 && XV_OWNER_PHASE_DEFAULT != 1
#error XV_OWNER_PHASE_DEFAULT must be 0 or 1
#endif
extern int xv_object_is_worker_thread(void) __attribute__((weak));
int xv_owner_phase_enabled;
static int configured;
static uintptr_t owner_context;
static phase_thread owner_thread;
static unsigned owner_valid, generation, generation_exhausted;
static struct { unsigned depth; uint64_t start, entries, completed, recursive, elapsed; } phases[XV_OWNER_PHASES];
static uint64_t clock_reads;
static unsigned warmup, foreign, invalid, rebinds, abandoned, stale;

static int worker(void)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(!xv_object_is_worker_thread)return 1; /* missing admission API fails closed */
#endif
    return xv_object_is_worker_thread && xv_object_is_worker_thread();
}
static int same_thread(void)
{
    if(!__atomic_load_n(&owner_valid,__ATOMIC_ACQUIRE))return 0;
    phase_thread expected=__atomic_load_n(&owner_thread,__ATOMIC_ACQUIRE);
#ifdef __vita__
    return current_thread()==expected;
#else
    return pthread_equal(current_thread(),expected);
#endif
}
static int marked(const xctx *c)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    return xv_is_object_job(c);
#else
    (void)c;return 0;
#endif
}
static int live(void *context)
{
    xctx *c=context;
    return c && xk_cur && c==&xk_cur->ctx && !marked(c) &&
        xk_cur->fiber && xk_os_fiber_current()==xk_cur->fiber && xk_cur->state==0;
}
static uint64_t now(void) { clock_reads++;return xk_os_monotonic_us(); }
static void account(unsigned phase,uint64_t end)
{
    if(end<phases[phase].start)__atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);
    else phases[phase].elapsed+=end-phases[phase].start;
    phases[phase].start=end;
}
void xv_owner_phase_configure(void)
{
    if(configured)return;
    const char *e=getenv("XV_OWNER_PHASE");
    xv_owner_phase_enabled=e?atoi(e)!=0:XV_OWNER_PHASE_DEFAULT;
    configured=1;
    XK_LOG("[owner-phase] process-start %d; FA920/BCB30 outer elapsed only; first Present binds owner, no phase/worker control\n",xv_owner_phase_enabled);
}
void xv_owner_phase_present(void *context)
{
    if(!xv_owner_phase_enabled)return;
    /* Called only at real guest Present/Swap. Worker rejection precedes any
     * owner-only scheduler access; these HLEs already require guest ownership. */
    if(worker()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(!live(context)) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    if(generation_exhausted)return;
    if(same_thread() && __atomic_load_n(&owner_context,__ATOMIC_ACQUIRE)==(uintptr_t)context)return;
    /* A different presenting fiber starts a new observation generation. A
     * suspended old scope is explicitly abandoned, never dereferenced/reused. */
    __atomic_store_n(&owner_valid,0,__ATOMIC_RELEASE);
    for(unsigned i=0;i<XV_OWNER_PHASES;i++) {
        if(phases[i].depth)abandoned++;
        phases[i].depth=0;
    }
    /* Never let an ancient suspended token become valid again after wrap. */
    if(generation==UINT32_MAX) {
        generation_exhausted=1;
        XK_LOG("[owner-phase] owner generation exhausted; further scopes declined\n");
        return;
    }
    generation++;
    rebinds++;
    __atomic_store_n(&owner_context,(uintptr_t)context,__ATOMIC_RELEASE);
    __atomic_store_n(&owner_thread,current_thread(),__ATOMIC_RELEASE);
    __atomic_store_n(&owner_valid,1,__ATOMIC_RELEASE);
}
void xv_owner_phase_begin(xv_owner_phase_scope *scope,void *context,unsigned phase)
{
    if(!xv_owner_phase_enabled)return;
    if(worker()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if(!__atomic_load_n(&owner_valid,__ATOMIC_ACQUIRE)) { __atomic_fetch_add(&warmup,1,__ATOMIC_RELAXED);return; }
    if(!same_thread() || __atomic_load_n(&owner_context,__ATOMIC_ACQUIRE)!=(uintptr_t)context) {
        __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return;
    }
    if(phase>=XV_OWNER_PHASES || !live(context) || phases[phase].depth==UINT32_MAX) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    *scope=((uint64_t)generation<<32)|(phase+1u);
    if(phases[phase].depth++)phases[phase].recursive++;
    else { phases[phase].entries++;phases[phase].start=now(); }
}
void xv_owner_phase_end(xv_owner_phase_scope *scope)
{
    uint64_t token=*scope;
    if(!token)return;
    *scope=0;
    if(!same_thread()) { __atomic_fetch_add(&foreign,1,__ATOMIC_RELAXED);return; }
    if((unsigned)(token>>32)!=generation) { stale++;return; }
    unsigned phase=(unsigned)token-1;
    if(phase>=XV_OWNER_PHASES || !phases[phase].depth) { __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return; }
    if(!--phases[phase].depth) { account(phase,now());phases[phase].completed++; }
}
void xv_owner_phase_report(unsigned frames)
{
    if(!xv_owner_phase_enabled || !frames || !same_thread())return;
    if(!live((void *)__atomic_load_n(&owner_context,__ATOMIC_ACQUIRE))) {
        __atomic_fetch_add(&invalid,1,__ATOMIC_RELAXED);return;
    }
    /* Split a still-open scope at this report boundary. It can contain
     * Present, yielding, or another selected scope; none is called CPU self. */
    if(phases[0].depth || phases[1].depth) {
        uint64_t end=now();
        for(unsigned i=0;i<XV_OWNER_PHASES;i++)if(phases[i].depth)account(i,end);
    }
    for(unsigned i=0;i<XV_OWNER_PHASES;i++) {
        XK_LOG("[owner-phase] %u frames %s: entries %llu completed %llu recursive %llu open %u elapsed-us %llu; nested/waits included, not CPU self\n",
            frames,i?"BCB30":"FA920",(unsigned long long)phases[i].entries,
            (unsigned long long)phases[i].completed,(unsigned long long)phases[i].recursive,
            phases[i].depth,(unsigned long long)phases[i].elapsed);
        phases[i].entries=phases[i].completed=phases[i].recursive=phases[i].elapsed=0;
    }
    XK_LOG("[owner-phase-status] clocks %llu warmup %u foreign %u invalid %u rebinds %u abandoned %u stale %u; native owner/context only, clock calls exclude report formatting\n",
        (unsigned long long)clock_reads,__atomic_exchange_n(&warmup,0,__ATOMIC_RELAXED),
        __atomic_exchange_n(&foreign,0,__ATOMIC_RELAXED),__atomic_exchange_n(&invalid,0,__ATOMIC_RELAXED),rebinds,abandoned,stale);
    clock_reads=0;rebinds=abandoned=stale=0;
}
