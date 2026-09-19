/* Experimental Halo 3925 second-pass object jobs.
 *
 * This intentionally relaxes object update ordering. Private CPU/guest stacks,
 * task ownership and joins are implemented; independence of the objects' shared
 * game state is NOT established. Keep this out of ordinary builds. Unsupported
 * worker HLE stops the experiment before entering the single-owner kernel or
 * graphics recorder. It must not silently pretend that the call succeeded.
 */
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
#include "xk.h"
#include "xk_object_jobs.h"
#include "xk_light_census.h"
#include "xk_worker_query.h"
#include "xk_cluster_runtime.h"
#include "xk_object_mutex.h"
#include "xk_object_solver.h"
#include "../xv_phase.h"
#if XV_NATIVE_VISIBILITY_JOBS
#ifndef XV_LIGHT_QUERY_CENSUS
#error Native visibility jobs require registered owner admission
#endif
#include "xk_visibility_jobs.h"
#include <fenv.h>
#if defined(__GLIBC__) && !defined(__vita__)
extern int fegetexcept(void);
#endif
#endif
#include <stdlib.h>
#include <stdio.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/error.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#endif

enum { WORKERS=2, LANES=3, CAPACITY=128, STACK_BYTES=XV_OBJECT_JOB_STACK_BYTES };
const char xv_object_job_marker=0;
static xctx jobs[CAPACITY], contexts[LANES];
static unsigned count, next, running, stopping, active_workers=WORKERS;
static uint32_t stacks[LANES];
static uint32_t stack_pages[LANES][STACK_BYTES/4096];
#ifdef XV_TYPED_CLUSTER_QUERY
static uint32_t query_stack_low[LANES],query_stack_high[LANES];
#endif
static unsigned stack_peak[LANES];
static uint32_t active_objects[LANES], indirect_stack[LANES][32];
static unsigned indirect_depth[LANES];
/* A worker publishes one request, then parks until the guest owner replies.
 * States: 0 executing, 1 service request, 2 completed, 3 completion consumed,
 * 4 parked (including an event reply held during an I/O handoff). */
static unsigned service_state[WORKERS], services, owner_notice, pause_workers, io_yields, resource_queries, resource_registers;
static unsigned vertex_locks;
static unsigned audio_pumps, audio_volumes, audio_commits, audio_stops;
static unsigned audio_status, audio_packets, audio_start_commits;
static unsigned audio_frequencies, audio_parameters;
/* Only the owner may execute nested stream services during a quiescent pump.
 * Keep the job marker and private stack: arbitrary kernel/scheduler calls still
 * stop instead of masquerading as the owner's guest fiber. */
static xctx *audio_service_context;
static unsigned service_address[WORKERS];
static xv_fn_t service_fn[WORKERS];
static int initialized, override=-1, configured=-1;
static xctx *owner;
static unsigned passes, batches, submitted, executed[LANES], rejected;
static uint64_t work_us[LANES], batch_us;
/* A worker's recursive scopes stay on its native thread, including while it
 * parks for an owner service. Only the outer scope needs the OS mutex. */
static unsigned math_depth[WORKERS], math_fast_path=1, math_idle_calls;
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
/* No environment/default enable. Only the drained owner may select this
 * research boundary. It changes inter-object ordering, not solver arithmetic. */
static unsigned solver_enabled, solver_active[WORKERS];
#ifdef __vita__
static SceUID solver_owner_thread;
#else
static pthread_t solver_owner_thread;
#endif
#endif
#ifdef XV_WORKER_QUERY
static unsigned query_enabled;
static unsigned query_admission[WORKERS][4]; /* accepted, disabled, nested, stack */
#endif
#ifdef XV_LIGHT_QUERY_CENSUS
#ifdef __vita__
static SceUID census_owner_thread;
#else
static pthread_t census_owner_thread;
#endif
static unsigned census_owner_scopes;
static int census_is_owner(void)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
#ifdef __vita__
    return sceKernelGetThreadId()==census_owner_thread;
#else
    return pthread_equal(pthread_self(),census_owner_thread);
#endif
}
/* Register/queue/fiber inspection is legal only after native owner identity.
 * A borrowed worker xctx in an owner service is never the current guest xctx. */
unsigned xv_object_census_admit(const xctx *c)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return XV_LC_UNINITIALIZED;
    if(xv_object_is_worker_thread())return XV_LC_WORKER;
    if(!census_is_owner())return XV_LC_NATIVE_OWNER;
    for(unsigned i=0;i<LANES;i++)if(c==&contexts[i]&&xv_is_object_job(c))return XV_LC_MARKED;
    if(!xk_cur||c!=&xk_cur->ctx)return XV_LC_CONTEXT;
    if(xv_is_object_job(c))return XV_LC_MARKED;
    if(!xk_cur->fiber||xk_os_fiber_current()!=xk_cur->fiber||xk_cur->state!=0)
        return XV_LC_CONTEXT;
    if(count||__atomic_load_n(&running,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&pause_workers,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&owner_notice,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE))return XV_LC_QUEUE;
    if(census_owner_scopes)return XV_LC_GUARD;
    return XV_LC_OK;
}
int xv_object_census_is_owner(void) {return census_is_owner();}
unsigned xv_object_census_boundary(const xctx *c)
{
    unsigned r=xv_object_census_admit(c);
    return r?r:owner?XV_LC_QUEUE:XV_LC_OK;
}
int xv_object_census_scope_begin(void)
{
    if(!census_is_owner())return 0;
    census_owner_scopes++;return 1;
}
void xv_object_census_scope_end(int *token)
{
    if(!*token)return;
    if(!census_is_owner()||!census_owner_scopes)abort();
    census_owner_scopes--;*token=0;
}
#endif

#ifdef XV_OBJECT_POSE_EXPERIMENT
/* No environment/default enable: only the drained guest owner may opt in.
 * Each worker owns its scope accounting until the joined report boundary. */
static unsigned pose_enabled, pose_depth[WORKERS];
static struct __attribute__((aligned(64))) {
    unsigned entered, enclosing, finished, cleanup, recursive_locks;
} pose_stats[WORKERS];
#ifdef __vita__
static SceUID pose_owner_thread;
#else
static pthread_t pose_owner_thread;
#endif
#endif
static struct __attribute__((aligned(64))) {
    unsigned acquired, nested, contended;
    uint64_t wait_us;
} math_stats[LANES];
/* Optional, bounded attribution of elapsed mutex waits to the requesting
 * native call site. There is no clock read or table search for uncontended
 * locks. Each worker owns its table; the owner reports only after a join.
 * This identifies waiters, not the helper holding the mutex during the wait. */
enum { WAIT_SITES=32 };
static unsigned math_profile;
static unsigned math_wait_enabled;
static int math_wait_override=-1;
static unsigned point_private_enabled;
static int point_private_override=-1;
#ifdef XV_OBJECT_QUAT_EXPERIMENT
static unsigned quat_private_enabled;
static int quat_private_override=-1;
static struct __attribute__((aligned(64))) {
    unsigned checks, admitted, nested, shared_input, shared_output, constants;
} quat_private_stats[WORKERS];
#endif
static struct __attribute__((aligned(64))) {
    unsigned checks, admitted, nested, shared_input, shared_output;
} point_private_stats[WORKERS];
enum { POINT_SITES=16 };
static struct {
    unsigned count, varied;
    uint32_t pc, output, matrix, vector, object, callback;
} point_sites[WORKERS][POINT_SITES+1];
#ifdef XV_OBJECT_QUAT_PROFILE
enum { QUAT_SITES=16 };
static struct {
    unsigned count, private_input;
    uint32_t pc, input, output;
} quat_sites[WORKERS][QUAT_SITES+1];
#endif
static struct __attribute__((aligned(64))) {
    unsigned attempts, acquired, timeouts;
} math_wait_stats[WORKERS];
static unsigned math_private_enabled=1;
static int math_private_override=-1;
static struct __attribute__((aligned(64))) {
    unsigned attempted, released, nested, shared, disabled;
} private_stats[WORKERS][4];
static struct {
    uintptr_t pc;
    unsigned count;
    uint64_t us, max_us;
} wait_sites[WORKERS][WAIT_SITES+1];

static void record_math_wait(unsigned lane,uintptr_t pc,uint64_t elapsed)
{
    math_stats[lane].wait_us+=elapsed;
    if(!math_profile)return;
    unsigned site;
    for(site=0;site<WAIT_SITES;site++)
        if(!wait_sites[lane][site].count||wait_sites[lane][site].pc==pc)break;
    /* The last bucket retains overflow totals without allocating memory. */
    if(site<WAIT_SITES)wait_sites[lane][site].pc=pc;
    wait_sites[lane][site].count++;
    wait_sites[lane][site].us+=elapsed;
    if(elapsed>wait_sites[lane][site].max_us)wait_sites[lane][site].max_us=elapsed;
}
#ifdef __vita__
static SceUID threads[WORKERS]={-1,-1}, wakes[WORKERS]={-1,-1}, dones[WORKERS]={-1,-1};
static SceUID owner_wake=-1, replies[WORKERS]={-1,-1};
#else
static pthread_t threads[WORKERS];
static sem_t wakes[WORKERS], dones[WORKERS];
static sem_t owner_wake, replies[WORKERS];
#endif

static xv_object_mutex math_mutex;
static int worker_lane(void);


#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING != 0 && XV_OBJECT_PASS_TIMING != 1
#error XV_OBJECT_PASS_TIMING must be 0 or 1
#endif
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
#include "xk_owner_phase.h"
/* Passive owner-only accounting. Existing begin/finish/report/shutdown callers
 * retain their guest-owner precondition. No worker writes these records. The
 * observer additionally validates native identity, live context and generation;
 * an unknown ancestry is never silently treated as outside FA920. */
extern int xv_owner_phase_active(void *,unsigned,uint32_t *) __attribute__((weak));
static struct {
    xctx *context;
    uint32_t generation;
    unsigned open, tick, begun, completed, interrupted, unknown, invalid, clocks;
    uint64_t start, batch_start;
    struct { unsigned count; uint64_t elapsed, joined; } scope[2];
#ifdef __vita__
    SceUID thread;
#else
    pthread_t thread;
#endif
} pass_timing;
static struct pass_clock_sample {
    int32_t id;
    unsigned valid, errors;
    uint64_t raw, at;
} pass_clocks[WORKERS];
static int pass_timing_thread(void)
{
    if(!pass_timing.context)return 0;
#ifdef __vita__
    return sceKernelGetThreadId()==pass_timing.thread;
#else
    return pthread_equal(pthread_self(),pass_timing.thread);
#endif
}
static int pass_timing_active(xctx *c,uint32_t *generation)
{ return xv_owner_phase_active ? xv_owner_phase_active(c,XV_OWNER_TICK,generation) : -1; }
static void pass_timing_begin(xctx *c)
{
    uint32_t generation=0;
    int tick=pass_timing_active(c,&generation);
    if(tick<0) { pass_timing.unknown++;return; }
    if(pass_timing.open)pass_timing.interrupted++;
    pass_timing.context=c;pass_timing.generation=generation;
#ifdef __vita__
    pass_timing.thread=sceKernelGetThreadId();
#else
    pass_timing.thread=pthread_self();
#endif
    pass_timing.open=1;pass_timing.tick=(unsigned)tick;
    pass_timing.batch_start=batch_us;
    pass_timing.start=xk_os_monotonic_us();pass_timing.clocks++;
    pass_timing.begun++;
}
static void pass_timing_finish(xctx *c)
{
    if(!pass_timing_thread()||!pass_timing.open||pass_timing.context!=c)return;
    uint32_t generation=pass_timing.generation;
    int tick=pass_timing_active(c,&generation);
    pass_timing.open=0;
    if(tick<0||(unsigned)tick!=pass_timing.tick) {
        pass_timing.interrupted++;pass_timing.invalid++;return;
    }
    uint64_t now=xk_os_monotonic_us();pass_timing.clocks++;
    if(now<pass_timing.start||batch_us<pass_timing.batch_start||
       batch_us-pass_timing.batch_start>now-pass_timing.start) {
        pass_timing.interrupted++;pass_timing.invalid++;return;
    }
    unsigned i=pass_timing.tick;
    pass_timing.scope[i].count++;
    pass_timing.scope[i].elapsed+=now-pass_timing.start;
    pass_timing.scope[i].joined+=batch_us-pass_timing.batch_start;
    pass_timing.completed++;
}
/* Observation timestamps follow each independent GetThreadInfo call. Deltas
 * are raw SDK runClocks, NOT cycles, microseconds or a CPU percentage. A failed
 * read, reused UID, or backwards value starts a new baseline instead of wrapping.
 * Kept separate from the platform read so these transitions can be tested. */
static void pass_clock_record(unsigned lane,int32_t id,int rc,int populated,
                              uint64_t raw,uint64_t at,unsigned frames)
{
    struct pass_clock_sample *s=&pass_clocks[lane];
    unsigned reason=0,valid=0;uint64_t delta=0,wall=0;
    if(rc<0||!populated||id<0) { reason=1;s->errors++;s->valid=0; }
    else {
        if(!s->valid)reason=2;
        else if(s->id!=id)reason=3;
        else if(raw<s->raw||at<=s->at)reason=4;
        else {valid=1;delta=raw-s->raw;wall=at-s->at;}
        s->id=id;s->raw=raw;s->at=at;s->valid=1;
    }
    XK_LOG("[object-worker-clock] %u frames lane %u active %u id %08X rc %08X at-us %llu raw %llu delta %llu wall-us %llu valid %u reason %u errors %u; raw SDK units, sequential CPU observations\n",
        frames,lane,lane<active_workers,(unsigned)id,(unsigned)rc,
        (unsigned long long)at,(unsigned long long)raw,(unsigned long long)delta,
        (unsigned long long)wall,valid,reason,s->errors);
}
static void pass_timing_report(unsigned frames)
{
    /* The enclosing report already proved no owner/pass, queued or running work.
     * Do not sample another guest fiber's report using a stale saved context. */
    uint32_t generation=pass_timing.generation;
    int admitted=pass_timing_thread()&&pass_timing_active(pass_timing.context,&generation)>=0;
    if(pass_timing.open) { pass_timing.open=0;pass_timing.interrupted++; }
    XK_LOG("[object-pass] %u frames begun %u completed %u interrupted %u open %u unknown %u invalid %u clocks %u owner-valid %u; accepted begin-to-finish elapsed includes joins/yields/preemption, excludes 90314 tail\n",
        frames,pass_timing.begun,pass_timing.completed,pass_timing.interrupted,
        pass_timing.open,pass_timing.unknown,pass_timing.invalid,pass_timing.clocks,admitted);
    for(unsigned i=0;i<2;i++)
        XK_LOG("[object-pass-scope] %u frames FA920 %u completed %u elapsed-us %llu same-pass-batch-us %llu; nested inclusive elapsed, not CPU self\n",
            frames,i,pass_timing.scope[i].count,(unsigned long long)pass_timing.scope[i].elapsed,
            (unsigned long long)pass_timing.scope[i].joined);
#ifdef __vita__
    if(admitted)for(unsigned lane=0;lane<WORKERS;lane++) {
        SceKernelThreadInfo info;
        memset(&info,0,sizeof info);info.size=sizeof info;
        int rc=sceKernelGetThreadInfo(threads[lane],&info);
        pass_clock_record(lane,threads[lane],rc,info.name[0]!=0,
                          info.runClocks,xk_os_monotonic_us(),frames);
    }
    else XK_LOG("[object-worker-clock] %u frames skipped: live owner admission unknown\n",frames);
#else
    XK_LOG("[object-worker-clock] %u frames unavailable on host; no synthetic CPU units\n",frames);
#endif
    pass_timing.begun=pass_timing.completed=pass_timing.interrupted=0;
    pass_timing.unknown=pass_timing.invalid=pass_timing.clocks=0;
    memset(pass_timing.scope,0,sizeof pass_timing.scope);
}
static void pass_timing_cancel(void)
{
    /* Shutdown already joined all work. A shutdown is not an original successful
     * finish: retain an explicit incomplete count and never manufacture elapsed. */
    if(pass_timing.open) {pass_timing.open=0;pass_timing.interrupted++;}
    XK_LOG("[object-pass-stop] completed %u interrupted %u unknown %u open %u; unreported window, shutdown is not finish\n",
        pass_timing.completed,pass_timing.interrupted,pass_timing.unknown,pass_timing.open);
    memset(&pass_timing,0,sizeof pass_timing);
    memset(pass_clocks,0,sizeof pass_clocks);
}
#endif

#ifdef XV_OBJECT_HOLD_PROFILE
/* Research-only sampled OUTER worker scopes. A lane owns its records until
 * join. Sampling is decorrelated from periodic call order. Query samples read
 * only validated private-stack metadata. Elapsed includes owner-service parks.
 * Table accounting happens after release, not while retaining the mutex. */
enum { HOLD_SITES=32, HOLD_SAMPLE_MASK=63, MOTION_SITES=9 };
static int private_stack_span(unsigned lane,uint32_t address,unsigned bytes);
static unsigned hold_enabled;
/* Read only during a joined batch; the drained owner changes this together
 * with hold_enabled. Generated call sites skip both helper calls when OFF. */
unsigned xv_object_hold_children_enabled;
static struct __attribute__((aligned(64))) {
    uint32_t random;
    unsigned observed, samples, active;
    uintptr_t pc;
    uint64_t start;
    struct { uintptr_t pc; unsigned samples; uint64_t us,max_us; } sites[HOLD_SITES+1];
    unsigned child_active;
    uint64_t child_start;
    struct { unsigned samples; uint64_t us,max_us; } children[23];
    unsigned motion_active;
    uint64_t motion_start[MOTION_SITES];
    struct { unsigned samples; uint64_t us,max_us; } motion[MOTION_SITES];
    unsigned query_origin;
    uint32_t query_output;
    uint8_t *query_arena;
    uint32_t *query_pages;
    struct { unsigned samples; uint64_t us,max_us; } queries[4];
    struct {
        unsigned valid,unreadable,invalid;
        struct { uint64_t sum; unsigned max,bins[6]; } list[3];
    } results[4];
} hold_lanes[WORKERS];
/* Stable IDs of the direct children of the signature-checked 4C980 callback.
 * No nested attribution: a child includes all its descendants and owner parks.
 * These calls never release the existing callback guard. */
static const uint32_t hold_children[23]={
    0x11120,0x3A8B0,0x3D190,0x41B40,0x425D0,0x428F0,0x43AF0,0x478D0,
    0x48090,0x48E10,0x49280,0x493E0,0x4A9F0,0x4B000,0x4B170,0x4B3A0,
    0x4B410,0x4B580,0x4B9D0,0xBDF10,0xBE050,0xBF870,0xD8B70
};
static const uint32_t motion_sites[MOTION_SITES]={
    0x478D0,0x49600,0x172BF0,0x171F10,0x170C10,0x1721B0,
    0x88110,0x868F0,0x1716F0
};
/* Nested inclusive timing only inside an already sampled direct child.
 * It retains the lock and never modifies guest state. Recursion stays
 * included in its outer instance. Owner services borrowing c are excluded. */
unsigned xv_object_motion_begin(xctx *c,unsigned site)
{
    if(!hold_enabled||site>=MOTION_SITES)return 0;
    int lane=worker_lane();
    if(lane<0||c!=&contexts[lane]||!math_depth[lane]||
       !hold_lanes[lane].active||!hold_lanes[lane].child_active||
       (hold_lanes[lane].motion_active&(1u<<site)))return 0;
    hold_lanes[lane].motion_active|=1u<<site;
    hold_lanes[lane].motion_start[site]=xk_os_monotonic_us();
    if(site==6) {
        /* Observe only the live worker's validated private return word. This
         * classifies sampled work; it never selects an optimized call path. */
        unsigned origin=3;
        if(private_stack_span((unsigned)lane,c->r[4],4)) {
            uint32_t pc=X_M32(c->r[4]);
            origin=pc==0x171f99u?0:pc==0x173020u?1:2;
        }
        hold_lanes[lane].query_origin=origin;
        /* ESI is the full 88110 result buffer at entry. Capture before the
         * guest can change registers; revalidate mapping and roots at return. */
        hold_lanes[lane].query_output=private_stack_span((unsigned)lane,c->r[6],0x1010u)?c->r[6]:0;
        hold_lanes[lane].query_arena=g_xram;
        hold_lanes[lane].query_pages=g_xpt;
    }
    return ((site+1)<<8)|(lane+1);
}
void xv_object_motion_end(unsigned *token)
{
    if(!*token)return;
    unsigned lane=(*token&255)-1,site=(*token>>8)-1;
    if(lane>=WORKERS||site>=MOTION_SITES||worker_lane()!=(int)lane||
       !math_depth[lane]||!hold_lanes[lane].active||!hold_lanes[lane].child_active||
       !(hold_lanes[lane].motion_active&(1u<<site)))abort();
    uint64_t elapsed=xk_os_monotonic_us()-hold_lanes[lane].motion_start[site];
    hold_lanes[lane].motion_active&=~(1u<<site);
    hold_lanes[lane].motion[site].samples++;
    hold_lanes[lane].motion[site].us+=elapsed;
    if(elapsed>hold_lanes[lane].motion[site].max_us)
        hold_lanes[lane].motion[site].max_us=elapsed;
    if(site==6) {
        unsigned origin=hold_lanes[lane].query_origin;
        hold_lanes[lane].queries[origin].samples++;
        hold_lanes[lane].queries[origin].us+=elapsed;
        if(elapsed>hold_lanes[lane].queries[origin].max_us)
            hold_lanes[lane].queries[origin].max_us=elapsed;
        typeof(hold_lanes[lane].results[origin]) *r=&hold_lanes[lane].results[origin];
        uint32_t output=hold_lanes[lane].query_output;
        if(!output||g_xram!=hold_lanes[lane].query_arena||g_xpt!=hold_lanes[lane].query_pages||
           !private_stack_span(lane,output,0x1010u))r->unreadable++;
        else {
            /* Observed final list lengths, not actual comparison counts.
             * Read after stopping the existing timer; no per-loop changes. */
            uint32_t n[3]={X_M32(output),X_M32(output+0x404u),X_M32(output+0x808u)};
            if(n[0]>256u||n[1]>256u||n[2]>256u)r->invalid++;
            else {
                r->valid++;
                for(unsigned i=0;i<3;i++) {
                    unsigned b=n[i]==0?0:n[i]<=8?1:n[i]<=32?2:n[i]<=64?3:n[i]<=128?4:5;
                    r->list[i].sum+=n[i];
                    if(n[i]>r->list[i].max)r->list[i].max=n[i];
                    r->list[i].bins[b]++;
                }
            }
        }
        hold_lanes[lane].query_output=0;
    }
    *token=0;
}
unsigned xv_object_hold_child_begin(int guard)
{
    if(!hold_enabled||guard<2||guard>=WORKERS+2)return 0;
    unsigned lane=(unsigned)guard-2;
    if(!hold_lanes[lane].active||math_depth[lane]!=1||hold_lanes[lane].child_active)return 0;
    hold_lanes[lane].child_active=1;
    hold_lanes[lane].child_start=xk_os_monotonic_us();
    return lane+1;
}
void xv_object_hold_child_end(unsigned token,unsigned child)
{
    if(!token)return;
    unsigned lane=token-1;
    if(lane>=WORKERS||child>=23||hold_lanes[lane].motion_active||!hold_lanes[lane].child_active||
       !hold_lanes[lane].active||math_depth[lane]!=1)abort();
    uint64_t elapsed=xk_os_monotonic_us()-hold_lanes[lane].child_start;
    hold_lanes[lane].child_active=0;
    hold_lanes[lane].children[child].samples++;
    hold_lanes[lane].children[child].us+=elapsed;
    if(elapsed>hold_lanes[lane].children[child].max_us)
        hold_lanes[lane].children[child].max_us=elapsed;
}
static void hold_begin(unsigned lane,uintptr_t pc)
{
    if(!hold_enabled)return;
    if(hold_lanes[lane].active)abort();
    hold_lanes[lane].observed++;
    uint32_t r=hold_lanes[lane].random;
    if(!r)r=0x9e3779b9u^(lane*0x85ebca6bu);
    r^=r<<13;r^=r>>17;r^=r<<5;hold_lanes[lane].random=r;
    if(r&HOLD_SAMPLE_MASK)return;
    hold_lanes[lane].pc=pc;hold_lanes[lane].active=1;
    hold_lanes[lane].start=xk_os_monotonic_us();
}
static void hold_release(unsigned lane)
{
    if(hold_lanes[lane].child_active||hold_lanes[lane].motion_active)abort();
    if(!hold_lanes[lane].active) {xv_object_mutex_release(&math_mutex);return;}
    uint64_t elapsed=xk_os_monotonic_us()-hold_lanes[lane].start;
    uintptr_t pc=hold_lanes[lane].pc;hold_lanes[lane].active=0;
    xv_object_mutex_release(&math_mutex);
    unsigned site;
    for(site=0;site<HOLD_SITES;site++)
        if(!hold_lanes[lane].sites[site].samples||hold_lanes[lane].sites[site].pc==pc)break;
    if(site<HOLD_SITES)hold_lanes[lane].sites[site].pc=pc;
    hold_lanes[lane].sites[site].samples++;hold_lanes[lane].samples++;
    hold_lanes[lane].sites[site].us+=elapsed;
    if(elapsed>hold_lanes[lane].sites[site].max_us)hold_lanes[lane].sites[site].max_us=elapsed;
}
static void hold_report(unsigned frames)
{
    for(unsigned lane=0;lane<WORKERS;lane++) {
        if(hold_lanes[lane].active||hold_lanes[lane].child_active||hold_lanes[lane].motion_active)abort();
        XK_LOG("[object-holds] %u frames lane %u enabled %u outer %u samples %u denominator 64; sampled elapsed includes scheduling/parks, not total hold time\n",
            frames,lane,hold_enabled,hold_lanes[lane].observed,hold_lanes[lane].samples);
        for(unsigned site=0;site<=HOLD_SITES;site++)if(hold_lanes[lane].sites[site].samples)
            XK_LOG("[object-hold-site] lane %u pc %llX samples %u elapsed-us %llu max-us %llu overflow %u\n",
                lane,(unsigned long long)hold_lanes[lane].sites[site].pc,
                hold_lanes[lane].sites[site].samples,(unsigned long long)hold_lanes[lane].sites[site].us,
                (unsigned long long)hold_lanes[lane].sites[site].max_us,site==HOLD_SITES);
        for(unsigned child=0;child<23;child++)if(hold_lanes[lane].children[child].samples)
            XK_LOG("[object-hold-child] lane %u parent 0004C980 child %08X samples %u elapsed-us %llu max-us %llu; inclusive within sampled outer hold\n",
                lane,hold_children[child],hold_lanes[lane].children[child].samples,
                (unsigned long long)hold_lanes[lane].children[child].us,
                (unsigned long long)hold_lanes[lane].children[child].max_us);
        for(unsigned site=0;site<MOTION_SITES;site++)if(hold_lanes[lane].motion[site].samples)
            XK_LOG("[object-motion] lane %u function %08X samples %u elapsed-us %llu max-us %llu; nested inclusive within sampled child, do not sum\n",
                lane,motion_sites[site],hold_lanes[lane].motion[site].samples,
                (unsigned long long)hold_lanes[lane].motion[site].us,
                (unsigned long long)hold_lanes[lane].motion[site].max_us);
        for(unsigned origin=0;origin<4;origin++)if(hold_lanes[lane].queries[origin].samples) {
            const char *route=(const char *const[]){"world-171f94","object-17301b","other","unreadable"}[origin];
            XK_LOG("[object-query-origin] lane %u route %s samples %u elapsed-us %llu max-us %llu; partitions sampled 88110, not total query time\n",
                lane,route,
                hold_lanes[lane].queries[origin].samples,
                (unsigned long long)hold_lanes[lane].queries[origin].us,
                (unsigned long long)hold_lanes[lane].queries[origin].max_us);
            typeof(hold_lanes[lane].results[origin]) *r=&hold_lanes[lane].results[origin];
            XK_LOG("[object-query-results] lane %u route %s valid %u unreadable %u invalid %u; final counts only\n",
                lane,route,r->valid,r->unreadable,r->invalid);
            if(r->valid)for(unsigned i=0;i<3;i++)
                XK_LOG("[object-query-list] lane %u route %s list %s sum %llu max %u bins %u/%u/%u/%u/%u/%u; 0,1-8,9-32,33-64,65-128,129-256\n",
                    lane,route,(const char *const[]){"surfaces","edges","vertices"}[i],
                    (unsigned long long)r->list[i].sum,r->list[i].max,
                    r->list[i].bins[0],r->list[i].bins[1],r->list[i].bins[2],
                    r->list[i].bins[3],r->list[i].bins[4],r->list[i].bins[5]);
        }
        uint32_t random=hold_lanes[lane].random;
        memset(&hold_lanes[lane],0,sizeof hold_lanes[lane]);hold_lanes[lane].random=random;
    }
}
#endif
int xv_object_holds_enabled(void)
{
#ifdef XV_OBJECT_HOLD_PROFILE
    return hold_enabled;
#else
    return 0;
#endif
}
int xv_object_holds_available(void)
{
#ifdef XV_OBJECT_HOLD_PROFILE
    return initialized==1&&active_workers&&math_fast_path&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
#else
    return 0;
#endif
}
void xv_object_holds_override(int value)
{
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
#ifdef XV_OBJECT_HOLD_PROFILE
    for(unsigned lane=0;lane<WORKERS;lane++)if(math_depth[lane]||hold_lanes[lane].active)abort();
    hold_enabled=value>0;
    xv_object_hold_children_enabled=hold_enabled;
#else
    (void)value;
#endif
}

#ifndef __vita__
static void wait_sem(sem_t *s) { while(sem_wait(s))if(errno!=EINTR)abort(); }
#endif
static void notify_owner(void)
{
    if(__atomic_exchange_n(&owner_notice,1,__ATOMIC_ACQ_REL))return;
#ifdef __vita__
    if(sceKernelSignalSema(owner_wake,1)<0)abort();
#else
    if(sem_post(&owner_wake))abort();
#endif
}
static void reply_worker(unsigned i)
{
    __atomic_store_n(&service_state[i],0,__ATOMIC_RELEASE);
#ifdef __vita__
    if(sceKernelSignalSema(replies[i],1)<0)abort();
#else
    if(sem_post(&replies[i]))abort();
#endif
}
static void wait_reply(unsigned i)
{
#ifdef __vita__
    if(sceKernelWaitSema(replies[i],1,NULL)<0)abort();
#else
    wait_sem(&replies[i]);
#endif
}
/* Called only by a worker at a job boundary or before acquiring a native lock.
 * It may retain a recursive callback lock: the admitted file fiber never enters
 * an object/math callback. All workers acknowledge before that fiber runs. */
static void park_worker(unsigned i)
{
    if(!__atomic_load_n(&pause_workers,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&service_state[i],4,__ATOMIC_RELEASE);notify_owner();
    wait_reply(i);
}
static int worker_lane(void)
{
#ifdef __vita__
    SceUID id=sceKernelGetThreadId();
    for(unsigned i=0;i<active_workers;i++)if(id==threads[i])return (int)i;
#else
    pthread_t id=pthread_self();
    for(unsigned i=0;i<active_workers;i++)if(pthread_equal(id,threads[i]))return (int)i;
#endif
    return -1;
}
#if XV_QUERY_WORLD_RUN
unsigned xv_object_world_run_admit(xctx *c)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||!math_fast_path||
       !__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;
    int lane=worker_lane();
    return lane>=0&&c==&contexts[lane]&&xv_is_object_job(c)&&math_depth[lane]>0;
}
#endif
#ifdef XV_LIGHT_QUERY_CENSUS
unsigned xv_object_query_work_lane(const xctx *c,int guard,unsigned *depth)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
    int lane=worker_lane();
    if(lane>=0){
        if(!__atomic_load_n(&running,__ATOMIC_ACQUIRE)||c!=&contexts[lane]||!xv_is_object_job(c))return 0;
        if(math_fast_path){
            if(guard!=lane+2||!math_depth[lane])return 0;
            *depth=math_depth[lane];
        }else{if(guard!=1)return 0;*depth=0;}
        return (unsigned)lane+2;
    }
    /* Reject borrowed worker contexts and foreign/native service callers before
     * reading owner fiber state. Logical scopes include the idle lock bypass. */
    if(!census_is_owner()||!xk_cur||c!=&xk_cur->ctx||xv_is_object_job(c)||
       !xk_cur->fiber||xk_os_fiber_current()!=xk_cur->fiber||xk_cur->state!=0||
       !census_owner_scopes)return 0;
    *depth=census_owner_scopes;return 1;
}
#endif
int xv_object_is_worker_thread(void)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
#ifdef __vita__
    SceUID id=sceKernelGetThreadId();
    for(unsigned i=0;i<WORKERS;i++)if(id==threads[i])return 1;
#else
    pthread_t id=pthread_self();
    for(unsigned i=0;i<WORKERS;i++)if(pthread_equal(id,threads[i]))return 1;
#endif
    return 0;
}
/* Audited stream-property calls in object sound update 297B0 and its 296D0
 * helper. All use existing non-callback handlers on the guest owner. */
static unsigned sound_parameter_return(unsigned address)
{
    switch(address) {
    case 0x194470u:return 0x29898u; /* frequency */
    case 0x193D9Bu:return 0x298E7u; /* maximum distance */
    case 0x193DB3u:return 0x2992Au; /* minimum distance */
    case 0x193D68u:return 0x2999Du; /* cone angles */
    case 0x193D96u:return 0x299F0u; /* cone outside volume */
    case 0x193E22u:return 0x2979Au; /* I3DL2 source */
    default:return 0;
    }
}
static int needs_quiescence(unsigned address)
{
    return address==0x1D6640u||address==0x184AB0u||address==0x1858D0u||address==0x193E27u||address==0x193D4Fu||address==0x193C1Bu||address==0x19C5FFu||address==0x19384Fu||address==0x193884u||sound_parameter_return(address);
}
static void service_audio(xctx *c,xv_fn_t fn,unsigned address)
{
    if(__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE))abort();
    __atomic_store_n(&audio_service_context,c,__ATOMIC_RELEASE);
    fn(c);
    __atomic_store_n(&audio_service_context,NULL,__ATOMIC_RELEASE);
    if(address==0x19384Fu)audio_status++;
    else if(address==0x193884u)audio_packets++;
    else audio_pumps++;
}
static void service_audio_commit(xctx *c,xv_fn_t fn)
{
    unsigned startup=X_M32(c->r[4])==0x28BB6u;
    fn(c);audio_commits++;audio_start_commits+=startup;
}
static void service_owner(void)
{
    unsigned completed=0;
    while(completed<active_workers) {
#ifdef __vita__
        if(sceKernelWaitSema(owner_wake,1,NULL)<0)abort();
#else
        wait_sem(&owner_wake);
#endif
        /* Consume the coalesced publication before scanning worker predicates.
         * A release-only store allows the following loads to happen before the
         * reset is visible. A worker can then publish after its state was read,
         * see the old notice=1 and omit its signal: all lanes sleep forever.
         * The exchange acquires even coalesced notifications and orders the
         * reset before the scan. Notifications after it post a new wake. */
        (void)__atomic_exchange_n(&owner_notice,0,__ATOMIC_SEQ_CST);
        unsigned yielding=0,quiescent=0,quiet=1;
        for(unsigned i=0;i<active_workers;i++) {
            unsigned state=__atomic_load_n(&service_state[i],__ATOMIC_ACQUIRE);
            if(state==0)quiet=0;
            else if(state==1) {
                if(needs_quiescence(service_address[i])) {
                    quiescent++;yielding+=service_address[i]==0x1D6640u;continue;
                }
#ifdef XV_TYPED_CLUSTER_QUERY
                xv_cluster_runtime_invalidate(service_address[i]);
#endif
                service_fn[i](&contexts[i]);
                if(service_address[i]==0x184A20u)resource_queries++;else services++;
                if(__atomic_load_n(&pause_workers,__ATOMIC_ACQUIRE))
                    __atomic_store_n(&service_state[i],4,__ATOMIC_RELEASE);
                else {reply_worker(i);quiet=0;}
            } else if(state==2) {
                __atomic_store_n(&service_state[i],3,__ATOMIC_RELEASE);completed++;
            }
        }
        if(quiescent&&quiet) {
#ifdef XV_TYPED_CLUSTER_QUERY
            xv_cluster_runtime_invalidate(yielding?0x1d6640u:0xffffffffu);
#endif
            /* Every lane is parked or completed. Run only the original cache
             * file fiber, preserving its stack, TLS, real reads and APCs. */
            extern int xk_object_io_step(void);
            if(yielding&&xk_object_io_step()<0)abort();
            for(unsigned i=0;i<active_workers;i++)
                if(__atomic_load_n(&service_state[i],__ATOMIC_ACQUIRE)==1) {
                    if(service_address[i]==0x1D6640u) {
                        contexts[i].r[0]=STATUS_SUCCESS;contexts[i].r[4]+=4;io_yields++;
                    } else if(service_address[i]==0x193E27u||service_address[i]==0x19384Fu||service_address[i]==0x193884u) {
                        /* Preserve real packet completion and original callback
                         * execution, with every other object lane parked. */
                        service_audio(&contexts[i],service_fn[i],service_address[i]);
                    } else if(service_address[i]==0x193D4Fu) {
                        /* Original stream volume update; no guest callbacks. */
                        service_fn[i](&contexts[i]);audio_volumes++;
                    } else if(service_address[i]==0x193C1Bu) {
                        /* Keep the existing deferred-settings handler on its
                         * owner, including its one-argument return convention. */
                        service_audio_commit(&contexts[i],service_fn[i]);
                    } else if(service_address[i]==0x19C5FFu) {
                        /* Retire the voice's reporting state on the audio
                         * owner; the existing handler dispatches no callbacks. */
                        service_fn[i](&contexts[i]);audio_stops++;
                    } else if(sound_parameter_return(service_address[i])) {
                        service_fn[i](&contexts[i]);
                        if(service_address[i]==0x194470u)audio_frequencies++;
                        else audio_parameters++;
                    } else {
                        /* Header fixup or vertex-storage pointer lookup only;
                         * no allocation, draw submission or scheduler entry.
                         * The guest impact transaction holds the shared guard
                         * across its allocation, pointer lookup and writes. */
                        service_fn[i](&contexts[i]);
                        if(service_address[i]==0x184AB0u)resource_registers++;
                        else vertex_locks++;
                    }
                    __atomic_store_n(&service_state[i],4,__ATOMIC_RELEASE);
                }
            __atomic_store_n(&pause_workers,0,__ATOMIC_RELEASE);
            for(unsigned i=0;i<active_workers;i++)
                if(__atomic_load_n(&service_state[i],__ATOMIC_ACQUIRE)==4)reply_worker(i);
        }
    }
}

/* Keep the captured return address at the guarded caller, including LTO builds. */
__attribute__((noinline)) int xv_object_math_lock(void)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
    /* running is published before waking workers and cleared only after every
     * done semaphore is consumed. Outside that interval the guest owner is the
     * sole caller of these helpers; retain normal locks during owner services. */
    if(math_fast_path&&!__atomic_load_n(&running,__ATOMIC_ACQUIRE)) {
        math_idle_calls++;return 0;
    }
    int lane=worker_lane();
    uint64_t waiting=0;
    if(lane>=0)for(;;) {
        /* Do not bypass this acknowledgement for recursive scopes: a parked
         * worker may hold the mutex while the owner services a cache request. */
        park_worker((unsigned)lane);
        if(math_fast_path&&math_depth[lane]) {
#ifdef XV_OBJECT_POSE_EXPERIMENT
            if(pose_depth[lane])pose_stats[lane].recursive_locks++;
#endif
            math_depth[lane]++;math_stats[lane].nested++;return lane+2;
        }
        int acquired=!xv_object_mutex_try(&math_mutex);
        if(!acquired) {
            if(!waiting) { waiting=xk_os_monotonic_us();math_stats[lane].contended++; }
            if(math_wait_override<0?math_wait_enabled:(unsigned)math_wait_override) {
                math_wait_stats[lane].attempts++;
                acquired=!xv_object_mutex_wait_bounded(&math_mutex,50);
                if(acquired)math_wait_stats[lane].acquired++;
                else math_wait_stats[lane].timeouts++;
            }
        }
        if(acquired) {
            math_stats[lane].acquired++;
            if(waiting)record_math_wait((unsigned)lane,(uintptr_t)__builtin_return_address(0),xk_os_monotonic_us()-waiting);
            if(math_fast_path) {
                math_depth[lane]=1;
#ifdef XV_OBJECT_HOLD_PROFILE
                hold_begin((unsigned)lane,(uintptr_t)__builtin_return_address(0));
#endif
                return lane+2;
            }
            return 1;
        }
        /* A timed-out lock already waited. Return directly to the park check;
         * adding the old sleep here would defer the service acknowledgement. */
        if(math_wait_override<0?math_wait_enabled:(unsigned)math_wait_override)continue;
#ifdef __vita__
        sceKernelDelayThread(50);
#else
        struct timespec delay={0,50000};nanosleep(&delay,NULL);
#endif
    }
    xv_object_mutex_wait(&math_mutex);
    math_stats[2].acquired++;
    return 1;
}

void xv_object_math_unlock(int *locked)
{
    if(!*locked)return;
    if(*locked>=2) {
        unsigned lane=(unsigned)*locked-2;
        if(lane>=WORKERS||!math_depth[lane])abort();
        if(--math_depth[lane])return;
#ifdef XV_OBJECT_HOLD_PROFILE
        hold_release(lane);return;
#endif
    }
    xv_object_mutex_release(&math_mutex);
}
#ifdef XV_OBJECT_POSE_EXPERIMENT
static int pose_is_owner(void)
{
#ifdef __vita__
    return sceKernelGetThreadId()==pose_owner_thread;
#else
    return pthread_equal(pthread_self(),pose_owner_thread);
#endif
}
static int pose_backend_available(void)
{
#ifdef __vita__
    return math_mutex.use_light;
#else
    return math_mutex.ready; /* Host fixture uses the recursive pthread adapter. */
#endif
}
int xv_object_pose_available(void)
{
    return __atomic_load_n(&initialized,__ATOMIC_ACQUIRE)==1&&pose_is_owner()&&
        active_workers&&math_fast_path&&pose_backend_available()&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
}
void xv_object_pose_override(int enabled)
{
    xv_object_math_report_check();
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||!pose_is_owner())abort();
    pose_enabled=enabled>0;
}
int xv_object_pose_begin(xctx *c)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||
       !__atomic_load_n(&running,__ATOMIC_ACQUIRE)||!pose_enabled||!math_fast_path||
       !pose_backend_available())return 0;
    int lane=worker_lane();
    /* Owner audio services retain a worker context: verify the native thread as
     * well as the exact live context and job marker. No guest span is borrowed. */
    if(lane<0||c!=&contexts[lane]||!xv_is_object_job(c))return 0;
    unsigned enclosing=math_depth[lane]!=0;
    int token=xv_object_math_lock();
    if(token!=lane+2)abort();
    pose_depth[lane]++;pose_stats[lane].entered++;
    pose_stats[lane].enclosing+=enclosing;
    return token;
}
static void pose_close(int *token,int cleanup)
{
    if(*token<=0) {*token=0;return;}
    int lane=worker_lane();
    if(lane<0||*token!=lane+2||!pose_depth[lane])abort();
    pose_depth[lane]--;
    if(cleanup)pose_stats[lane].cleanup++;else pose_stats[lane].finished++;
    xv_object_math_unlock(token);*token=0;
}
void xv_object_pose_finish(int *token) {pose_close(token,0);}
void xv_object_pose_cleanup(int *token) {pose_close(token,1);}
#endif
int xv_object_lock_available(void)
{
    return initialized==1&&active_workers&&math_mutex.light_ready&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
}
void xv_object_lock_override(int value)
{
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
    xv_object_mutex_select(&math_mutex,value);
}
int xv_object_wait_available(void)
{
    return initialized==1&&active_workers&&math_mutex.ready&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
}
void xv_object_wait_override(int value)
{
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
    math_wait_override=value<0?-1:!!value;
}
static int private_stack_span(unsigned lane,uint32_t address,unsigned bytes)
{
    if(!bytes)return 1;
    uint32_t base=stacks[lane];
    if(address<base+4u || address-base>=STACK_BYTES || bytes>STACK_BYTES-(address-base))return 0;
    unsigned first=(address-base)/4096,last=(address-base+bytes-1)/4096;
    for(unsigned i=first;i<=last;i++)
        if(g_xpt[(base>>12)+i]!=stack_pages[lane][i])return 0;
    return 1;
}
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
static int solver_is_owner(void)
{
#ifdef __vita__
    return sceKernelGetThreadId()==solver_owner_thread;
#else
    return pthread_equal(pthread_self(),solver_owner_thread);
#endif
}
void xv_object_solver_override(int enabled)
{
    xv_object_math_report_check();
    if(initialized!=1||!solver_is_owner())abort();
    for(unsigned lane=0;lane<WORKERS;lane++)
        if(solver_active[lane]||math_depth[lane])abort();
    solver_enabled=enabled>0;
}
static int solver_constants(void)
{
    /* Original non-writable image constants plus its canonical vector pointer.
     * Map/image replacement is owner-only with object jobs drained. */
    static const struct {uint32_t address,value;} words[]={
        {0x1f0a68,0},{0x1f0a78,0x3f800000},
        {0x1f0af8,0xe0000000},{0x1f0afc,0x3f1a36e2},
        {0x1f0c24,0xb8d1b717},{0x206f9c,0x1eaebc},
        {0x1eaebc,0},{0x1eaec0,0},{0x1eaec4,0x3f800000},
        {0x1eaf30,0x00010002},{0x1eaf34,0x00020001},
        {0x1eaf38,0x00020000},{0x1eaf3c,0x00000002},
        {0x1eaf40,0x00000001},{0x1eaf44,0x00010000}
    };
    for(unsigned i=0;i<sizeof words/sizeof *words;i++) {
        uint32_t a=words[i].address;
        if((uintptr_t)X_G(a)!=(uintptr_t)g_img_base+a||X_M32(a)!=words[i].value)return 0;
    }
    return 1;
}
int xv_object_solver_begin(xctx *c)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||!solver_enabled||
       !math_fast_path||!__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;
    int lane=worker_lane();
    if(lane<0||c!=&contexts[lane]||!xv_is_object_job(c)||
       math_depth[lane]!=1||solver_active[lane]||c->df||c->preempt<65536)return 0;
#ifdef XV_OBJECT_HOLD_PROFILE
    /* Child samples span the outer scope and cannot survive its suspension. */
    if(hold_enabled)return 0;
#endif
    uint32_t sp=c->r[4];
    /* 170C10 plus its deepest math descendant uses less than 1 KiB below ESP.
     * Verify the six stack arguments before reading any pointer from them. */
    if(sp<1024||!private_stack_span(lane,sp-1024,1052)||
       X_M32(sp)!=0x172cbdu||X_M32(sp+20)!=16)return 0;
    struct {uint32_t address,bytes;} spans[]={
        {sp-1024,1052}, {c->r[0],12}, {X_M32(sp+4),12},
        {X_M32(sp+8),0xac08}, {X_M32(sp+12),12},
        {X_M32(sp+16),12}, {X_M32(sp+24),16*44}
    };
    for(unsigned i=1;i<sizeof spans/sizeof *spans;i++) {
        if((spans[i].address&3)||!private_stack_span(lane,spans[i].address,spans[i].bytes))return 0;
        /* Reject aliases into input counts, polygon headers, call arguments,
         * scratch or another output. Original in-place cases retain the lock. */
        for(unsigned j=0;j<i;j++)
            if(spans[i].address<spans[j].address+spans[j].bytes&&
               spans[j].address<spans[i].address+spans[i].bytes)return 0;
    }
    uint32_t packet=spans[3].address;
    for(unsigned kind=0;kind<3;kind++)if(X_M16(packet+2*kind)>256)return 0;
    unsigned polygons=X_M16(packet+4);
    for(unsigned i=0;i<polygons;i++) {
        uint32_t polygon=packet+0x4408+104*i;
        /* +24 is an inline vertex COUNT, not a borrowed world pointer. The
         * polygon's at most eight 2D vertices follow at +28. */
        if(X_M16(polygon+0x20)>2||X_M8(polygon+0x22)>1||
           X_M32(polygon+0x24)>8)return 0;
    }
    if(!solver_constants())return 0;
    solver_active[lane]=1;
    int enclosing=lane+2;
    xv_object_math_unlock(&enclosing);
    return lane+1;
}
void xv_object_solver_end(int *token)
{
    if(!*token)return;
    int lane=worker_lane();
    if(lane<0||*token!=lane+1||!solver_active[lane]||math_depth[lane])abort();
    /* Re-enter through the park-aware path. The outer caller still owns its
     * original cleanup token; restore its depth before returning to it. */
    int enclosing=xv_object_math_lock();
    if(enclosing!=lane+2||math_depth[lane]!=1)abort();
    solver_active[lane]=0;*token=0;
}
#endif
#ifdef XV_OBJECT_QUAT_PROFILE
/* Called under the existing guard after private output/scratch admission.
 * This is an ownership census, not permission to read shared inputs unlocked. */
static void record_quat_site(unsigned lane,xctx *c)
{
    uint32_t pc=private_stack_span(lane,c->r[4],4)?X_M32(c->r[4]):0;
    unsigned site;
    for(site=0;site<QUAT_SITES;site++)
        if(!quat_sites[lane][site].count||quat_sites[lane][site].pc==pc)break;
    if(!quat_sites[lane][site].count) {
        quat_sites[lane][site].pc=site<QUAT_SITES?pc:0;
        quat_sites[lane][site].input=c->r[1];quat_sites[lane][site].output=c->r[2];
    }
    quat_sites[lane][site].count++;
    quat_sites[lane][site].private_input+=private_stack_span(lane,c->r[1],16);
}
#endif
/* Candidate-only bounded caller attribution. Read the return PC only from the
 * current lane's verified stack; other fields are register values, not reads
 * from shared objects. The last bucket records overflow without allocation. */
#ifdef XV_OBJECT_POINT_EXPERIMENT
static void record_point_site(unsigned lane,xctx *c)
{
    uint32_t pc=private_stack_span(lane,c->r[4],4)?X_M32(c->r[4]):0;
    unsigned site;
    for(site=0;site<POINT_SITES;site++)
        if(!point_sites[lane][site].count||point_sites[lane][site].pc==pc)break;
    if(!point_sites[lane][site].count) {
        point_sites[lane][site].pc=site<POINT_SITES?pc:0;
        point_sites[lane][site].output=c->r[0];
        point_sites[lane][site].matrix=c->r[1];
        point_sites[lane][site].vector=c->r[2];
        point_sites[lane][site].object=active_objects[lane];
        point_sites[lane][site].callback=indirect_depth[lane]?indirect_stack[lane][indirect_depth[lane]-1]:0;
    } else if(point_sites[lane][site].output!=c->r[0]||
              point_sites[lane][site].matrix!=c->r[1]||
              point_sites[lane][site].vector!=c->r[2])point_sites[lane][site].varied++;
    point_sites[lane][site].count++;
}
#endif
/* A point helper touches no game-global constants or cache. Only a live worker
 * with all three spans on its unchanged private stack may bypass the guard.
 * The helper keeps configuration immutable and owns separate per-lane counters.
 * Owner audio services retain worker contexts, so context identity alone is not
 * sufficient: verify the actual native thread before accepting the lane. */
int xv_object_private_point(xctx *c)
{
#ifndef XV_OBJECT_POINT_EXPERIMENT
    (void)c;return 0;
#else
    if(!(point_private_override<0?point_private_enabled:(unsigned)point_private_override)||
       !math_fast_path||__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||
       !__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;
    int lane=worker_lane();
    if(lane<0||c!=&contexts[lane]||!xv_is_object_job(c))return 0;
    park_worker((unsigned)lane);
    point_private_stats[lane].checks++;
    record_point_site((unsigned)lane,c);
    if(math_depth[lane]) {point_private_stats[lane].nested++;return 0;}
    if(!private_stack_span((unsigned)lane,c->r[0],12)) {
        point_private_stats[lane].shared_output++;return 0;
    }
    if(!private_stack_span((unsigned)lane,c->r[1],52)||
       !private_stack_span((unsigned)lane,c->r[2],12)) {
        point_private_stats[lane].shared_input++;return 0;
    }
    point_private_stats[lane].admitted++;
    return lane+1;
#endif
}
void xv_object_math_report_check(void)
{
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
}
int xv_object_private_quaternion(xctx *c)
{
#if !defined(XV_OBJECT_QUAT_EXPERIMENT) || defined(XV_QUAT_CACHE)
    (void)c;return 0;
#else
    if(!(quat_private_override<0?quat_private_enabled:(unsigned)quat_private_override)||
       !math_fast_path||__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1||
       !__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;
    int lane=worker_lane();
    if(lane<0||c!=&contexts[lane]||!xv_is_object_job(c))return 0;
    park_worker((unsigned)lane);quat_private_stats[lane].checks++;
    if(math_depth[lane]) {quat_private_stats[lane].nested++;return 0;}
    if(c->r[4]<24u||!private_stack_span(lane,c->r[2],52)||
       !private_stack_span(lane,c->r[4]-24u,28)) {
        quat_private_stats[lane].shared_output++;return 0;
    }
    if(!private_stack_span(lane,c->r[1],16)) {
        quat_private_stats[lane].shared_input++;return 0;
    }
    /* Halo 3925's 0/1/2 constants are in non-writable .rdata (1D6620..1F1250).
     * Require the original image mapping and canonical words. There are no
     * mutable tag/pose reads here; remapped or modified constants retain the
     * guarded path. Map/image replacement occurs with object workers drained. */
    if((uintptr_t)X_G(0x1f0a68u)!=(uintptr_t)g_img_base+0x1f0a68u||
       X_M32(0x1f0a68u)!=0||X_M32(0x1f0a78u)!=0x3f800000u||
       X_M32(0x1f0b04u)!=0x40000000u) {
        quat_private_stats[lane].constants++;return 0;
    }
    quat_private_stats[lane].admitted++;return lane+1;
#endif
}
int xv_object_quat_available(void)
{
#if !defined(XV_OBJECT_QUAT_EXPERIMENT) || defined(XV_QUAT_CACHE)
    return 0;
#else
    const char *math=getenv("XV_NATIVE_MATH");
    return (!math||atoi(math)!=0)&&initialized==1&&active_workers&&math_fast_path&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
#endif
}
void xv_object_quat_override(int value)
{
    xv_object_math_report_check();
#ifdef XV_OBJECT_QUAT_EXPERIMENT
    quat_private_override=value<0?-1:!!value;
#else
    (void)value;
#endif
}
int xv_object_point_available(void)
{
#ifndef XV_OBJECT_POINT_EXPERIMENT
    return 0;
#else
    return initialized==1&&active_workers&&math_fast_path&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
#endif
}
void xv_object_point_override(int value)
{
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
    point_private_override=value<0?-1:!!value;
}
#ifdef XV_WORKER_QUERY
int xv_object_query_lane(xctx *c,int guard,uint32_t base,unsigned bytes)
{
    if(initialized!=1||!math_fast_path||!__atomic_load_n(&running,__ATOMIC_ACQUIRE))return 0;
    int lane=worker_lane();
    if(lane<0||c!=&contexts[lane]||!xv_is_object_job(c)||guard!=lane+2)return 0;
    /* Guard acquisition already acknowledged any pending owner pause. Never
     * park again during capture/validate/publication: there is no guest callback. */
    unsigned reason=!query_enabled?1:math_depth[lane]!=1?2:
        !private_stack_span((unsigned)lane,base,bytes)?3:0;
    query_admission[lane][reason]++;
    return reason?0:lane+1;
}
#endif
#ifdef XV_TYPED_CLUSTER_QUERY
int xv_object_query_private(xctx *c,int guard,uint32_t address,unsigned bytes)
{
    int lane=worker_lane();
    return lane>=0&&c==&contexts[lane]&&xv_is_object_job(c)&&guard==lane+2&&
        math_depth[lane]==1&&private_stack_span((unsigned)lane,address,bytes);
}
int xv_object_query_source_allowed(uintptr_t pointer,unsigned bytes)
{
    /* Snapshot construction runs on the drained owner. A conservative physical
     * envelope also rejects aliases into another worker's private stack. */
    if(!bytes||pointer>UINTPTR_MAX-bytes||initialized!=1)return 0;
    for(unsigned lane=0;lane<LANES;lane++){
        uintptr_t low=(uintptr_t)g_xram+query_stack_low[lane],high=(uintptr_t)g_xram+query_stack_high[lane]+4096;
        if(pointer<high&&low<pointer+bytes)return 0;
    }
    return 1;
}
#endif
int xv_object_math_release_private(xctx *c,int *locked,unsigned kind,
    uint32_t output,unsigned output_bytes,uint32_t scratch,unsigned scratch_bytes)
{
    if(*locked<2||*locked>=2+WORKERS||kind>=4)return 0;
    unsigned lane=(unsigned)*locked-2;
    if(c!=&contexts[lane]||!xv_is_object_job(c))return 0;
    private_stats[lane][kind].attempted++;
    if(!(math_private_override<0?math_private_enabled:(unsigned)math_private_override)) {
        private_stats[lane][kind].disabled++;return 0;
    }
    /* Never shorten an enclosing cache, collision, or object transaction. */
    if(math_depth[lane]!=1) {private_stats[lane][kind].nested++;return 0;}
    if(!output_bytes||!private_stack_span(lane,output,output_bytes)||
       !private_stack_span(lane,scratch,scratch_bytes)) {
        private_stats[lane][kind].shared++;return 0;
    }
    private_stats[lane][kind].released++;
#ifdef XV_OBJECT_QUAT_PROFILE
    if(kind==2)record_quat_site(lane,c);
#endif
    xv_object_math_unlock(locked);*locked=0;
    return 1;
}
int xv_object_math_available(void)
{
    return initialized==1&&active_workers&&math_fast_path&&!xv_phase_enabled&&
        (override<0?configured:override)>0;
}
void xv_object_math_override(int enabled)
{
    /* Only the drained guest frame boundary may change this policy. */
    if(owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))abort();
    math_private_override=enabled<0?-1:!!enabled;
}
#if XV_NATIVE_VISIBILITY_JOBS
/* The owner copies these bounded packets while the existing workers sleep.
 * Native batches never enter execute(), request an owner service, or touch
 * the guest arena. running excludes nested/foreign owner operations until
 * every done semaphore has been consumed. No new native threads are created. */
static struct {
    xv_visibility_input input[XV_VISIBILITY_JOB_CAPACITY];
    xs_bounds_result output[XV_VISIBILITY_JOB_CAPACITY] __attribute__((aligned(64)));
    fenv_t environment;
    unsigned n,lanes;
} visibility_packet __attribute__((aligned(64)));
static unsigned visibility_running;
static struct __attribute__((aligned(64))) { unsigned calls,items; } visibility_lane[LANES];

static int visibility_environment(fenv_t *out)
{
#if defined(__arm__)
    unsigned fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr)::"memory");
    if(fpscr&0x00009f00u)return 0; /* No speculative FP traps. */
#elif defined(__GLIBC__)
    if(fegetexcept())return 0;
#else
    return 0; /* Unqualified host trap-mask interface. */
#endif
    return fegetenv(out)==0;
}
static unsigned visibility_key(unsigned v)
{
    if(!(v&0x7fffffffu))v=0;
    return v&0x80000000u ? ~v : v^0x80000000u;
}
static int visibility_input_valid(const xv_visibility_input *p)
{
    _Static_assert(sizeof(*p)==28*4,"visibility packet layout");
    const unsigned char *bytes=(const unsigned char *)p;
    for(unsigned i=0;i<28;++i) {
        unsigned v;memcpy(&v,bytes+4*i,4);
        if((v&0x7fffffffu)>=0x7f800000u)return 0;
    }
    const xs_box *boxes[]={&p->frustum.enclosing,&p->box};
    for(unsigned j=0;j<2;++j)for(unsigned i=0;i<3;++i) {
        unsigned lo,hi;memcpy(&lo,&boxes[j]->axis[i][0],4);memcpy(&hi,&boxes[j]->axis[i][1],4);
        if(visibility_key(lo)>visibility_key(hi))return 0;
    }
    return 1;
}
static void visibility_execute(unsigned lane,unsigned partition)
{
    fenv_t saved;if(fegetenv(&saved)||fesetenv(&visibility_packet.environment))abort();
    /* Eight results occupy a cache line; adjacent lanes do not share output
     * lines. The final partition takes any remaining elements. */
    unsigned chunks=(visibility_packet.n+7)/8;
    unsigned lo=8*(chunks*partition/visibility_packet.lanes);
    unsigned hi=8*(chunks*(partition+1)/visibility_packet.lanes);
    if(hi>visibility_packet.n)hi=visibility_packet.n;
    for(unsigned i=lo;i<hi;++i) {
        const xv_visibility_input *p=&visibility_packet.input[i];
        visibility_packet.output[i]=xs_bounds(&p->frustum,&p->box);
    }
    visibility_lane[lane].calls++;visibility_lane[lane].items+=hi-lo;
    if(fesetenv(&saved))abort();
}
int xv_visibility_classify_jobs(xctx *c,const xv_visibility_input *input,unsigned n,
                                xs_bounds_result *output)
{
    /* Ownership is checked before even dereferencing the supplied pointers. */
    if(xv_object_census_boundary(c)!=XV_LC_OK || xv_phase_enabled ||
       (override<0?configured:override)==0 || XV_LIGHT_CENSUS_ON())return 0;
    if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE)||
       __atomic_load_n(&visibility_running,__ATOMIC_ACQUIRE))return 0;
    extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));
    if((&xv_watch_n&&xv_watch_n)||(&xv_trace_funcs&&xv_trace_funcs)||
       !input||!output||!n||n>XV_VISIBILITY_JOB_CAPACITY)return 0;
    fenv_t environment;if(!visibility_environment(&environment))return 0;
    memcpy(visibility_packet.input,input,n*sizeof(*input));
    for(unsigned i=0;i<n;++i)if(!visibility_input_valid(&visibility_packet.input[i]))return 0;
    visibility_packet.environment=environment;visibility_packet.n=n;
    /* Small packets keep the typed whole-pass path without a kernel wake. */
    unsigned workers=n<24?0:active_workers;
    visibility_packet.lanes=workers+1;
    __atomic_store_n(&visibility_running,1,__ATOMIC_RELEASE);
    __atomic_store_n(&running,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<workers;++i) {
#ifdef __vita__
        if(sceKernelSignalSema(wakes[i],1)<0)abort();
#else
        if(sem_post(&wakes[i]))abort();
#endif
    }
    visibility_execute(2,workers);
    for(unsigned i=0;i<workers;++i) {
#ifdef __vita__
        if(sceKernelWaitSema(dones[i],1,NULL)<0)abort();
#else
        wait_sem(&dones[i]);
#endif
    }
    __atomic_store_n(&visibility_running,0,__ATOMIC_RELEASE);
    __atomic_store_n(&running,0,__ATOMIC_RELEASE);
    memcpy(output,visibility_packet.output,n*sizeof(*output));return 1;
}
#endif

static void execute(unsigned lane)
{
    extern void f_0008FB70(xctx *);
    unsigned i;
    while((i=__atomic_fetch_add(&next,1,__ATOMIC_RELAXED))<count) {
        if(lane<active_workers)park_worker(lane);
        xctx *c=&contexts[lane]; *c=jobs[i];
        active_objects[lane]=c->r[1];
        c->r[4]=stacks[lane]+STACK_BYTES-256;
        X_M32(c->r[4])=0x90299u;
        c->fiber=(void *)&xv_object_job_marker;
        c->preempt=1000000;
        /* A canary detects runaway stack use independently of object results. */
        X_M32(stacks[lane])=0x584a4f42u;
        uint64_t started=xk_os_monotonic_us();
        f_0008FB70(c);
        if(lane<WORKERS&&math_depth[lane])
            xv_object_job_stop(c,0x8FB70u,"unbalanced shared transaction lock");
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
        if(lane<WORKERS&&solver_active[lane])
            xv_object_job_stop(c,0x8FB70u,"unbalanced private solver scope");
#endif
        work_us[lane]+=xk_os_monotonic_us()-started;
        executed[lane]++;
        if(c->r[4]!=stacks[lane]+STACK_BYTES-252 || X_M32(stacks[lane])!=0x584a4f42u)
            xv_object_job_stop(c,0x8FB70u,"guest stack contract");
    }
}
#ifdef __vita__
static int worker(SceSize size,void *arg)
{
    (void)size; unsigned lane=*(unsigned *)arg;
    extern void xv_cpu_log_thread(const char *);
    xv_cpu_log_thread(lane?"object-jobs-core1":"object-jobs-core0");
    for(;;) {
        if(sceKernelWaitSema(wakes[lane],1,NULL)<0)abort();
        if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return 0;
#if XV_NATIVE_VISIBILITY_JOBS
        if(__atomic_load_n(&visibility_running,__ATOMIC_ACQUIRE)) {
            visibility_execute(lane,lane);
            if(sceKernelSignalSema(dones[lane],1)<0)abort();
            continue;
        }
#endif
        execute(lane);
        __atomic_store_n(&service_state[lane],2,__ATOMIC_RELEASE);notify_owner();
        if(sceKernelSignalSema(dones[lane],1)<0)abort();
    }
}
#else
static void *worker(void *arg)
{
    unsigned lane=(unsigned)(uintptr_t)arg;
    for(;;) {
        wait_sem(&wakes[lane]);
        if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return NULL;
#if XV_NATIVE_VISIBILITY_JOBS
        if(__atomic_load_n(&visibility_running,__ATOMIC_ACQUIRE)) {
            visibility_execute(lane,lane);if(sem_post(&dones[lane]))abort();
            continue;
        }
#endif
        execute(lane);
        __atomic_store_n(&service_state[lane],2,__ATOMIC_RELEASE);notify_owner();
        sem_post(&dones[lane]);
    }
}
#endif
static int initialize(void)
{
    if(initialized)return initialized>0;
    initialized=-1;
#ifdef XV_OBJECT_SOLVER_EXPERIMENT
#ifdef __vita__
    solver_owner_thread=sceKernelGetThreadId();
#else
    solver_owner_thread=pthread_self();
#endif
#endif
#ifdef XV_LIGHT_QUERY_CENSUS
#ifdef __vita__
    census_owner_thread=sceKernelGetThreadId();
#else
    census_owner_thread=pthread_self();
#endif
#endif
#ifdef XV_OBJECT_POSE_EXPERIMENT
#ifdef __vita__
    pose_owner_thread=sceKernelGetThreadId();
#else
    pose_owner_thread=pthread_self();
#endif
#endif
    const char *fast=getenv("XV_OBJECT_LOCK_FAST_PATH");
    math_fast_path=!fast||atoi(fast)!=0;
#ifdef XV_WORKER_QUERY
    const char *query=getenv("XV_WORKER_QUERY");
    query_enabled=query&&!strcmp(query,"1");
#endif
    const char *profile=getenv("XV_OBJECT_LOCK_PROFILE");
    /* Dedicated experimental builds collect contention by default. Set zero
     * for a profiling-overhead comparison; ordinary builds omit this module. */
    math_profile=!profile||atoi(profile)!=0;
#ifdef XV_OBJECT_POINT_EXPERIMENT
    const char *point=getenv("XV_OBJECT_PRIVATE_POINT");
    point_private_enabled=point&&atoi(point)!=0;
#endif
#ifdef XV_OBJECT_QUAT_EXPERIMENT
    const char *quat=getenv("XV_OBJECT_PRIVATE_QUATERNION");
    quat_private_enabled=quat&&atoi(quat)!=0;
#endif
    const char *timed=getenv("XV_OBJECT_TIMED_WAIT");
    math_wait_enabled=timed&&atoi(timed)!=0;
    const char *private_math=getenv("XV_OBJECT_PRIVATE_MATH");
    math_private_enabled=!private_math||atoi(private_math)!=0;
    const char *workers=getenv("XV_OBJECT_JOB_WORKERS");
    if(workers && (!strcmp(workers,"0")||!strcmp(workers,"1")))active_workers=(unsigned)atoi(workers);
    if(active_workers!=WORKERS)XK_LOG("[object-jobs] DIAGNOSTIC: %u active workers; zero selects owner-only execution\n",active_workers);
    for(unsigned i=0;i<LANES;i++) {
        stacks[i]=xk_mem_alloc(STACK_BYTES,4096,0,0,1);
        if(!stacks[i])goto fail;
        for(unsigned p=0;p<STACK_BYTES/4096;p++)stack_pages[i][p]=g_xpt[(stacks[i]>>12)+p];
#ifdef XV_TYPED_CLUSTER_QUERY
        query_stack_low[i]=UINT32_MAX;query_stack_high[i]=0;
        for(unsigned p=0;p<STACK_BYTES/4096;p++){
            uint32_t offset=stack_pages[i][p];
            if(offset<query_stack_low[i])query_stack_low[i]=offset;
            if(offset>query_stack_high[i])query_stack_high[i]=offset;
        }
#endif
    }
    const char *light=getenv("XV_OBJECT_LIGHT_LOCK");
    if(xv_object_mutex_init(&math_mutex,!light||atoi(light)!=0))goto fail;
#ifdef __vita__
    owner_wake=sceKernelCreateSema("xv_object_service",0,0,1,NULL);
    if(owner_wake<0)goto fail;
    for(unsigned i=0;i<WORKERS;i++) {
        wakes[i]=sceKernelCreateSema("xv_object_wake",0,0,1,NULL);
        dones[i]=sceKernelCreateSema("xv_object_done",0,0,1,NULL);
        replies[i]=sceKernelCreateSema("xv_object_reply",0,0,1,NULL);
        if(wakes[i]<0||dones[i]<0||replies[i]<0)goto fail;
        threads[i]=sceKernelCreateThread(i?"xv_objects_c1":"xv_objects_c0",worker,
            sceKernelGetThreadCurrentPriority(),512*1024,0,
            i?SCE_KERNEL_CPU_MASK_USER_1:SCE_KERNEL_CPU_MASK_USER_0,NULL);
        if(threads[i]<0)goto fail;
    }
    for(unsigned i=0;i<WORKERS;i++)
        if(sceKernelStartThread(threads[i],sizeof i,&i)<0)abort();
#else
    sem_init(&owner_wake,0,0);
    for(unsigned i=0;i<WORKERS;i++) {
        sem_init(&wakes[i],0,0);sem_init(&dones[i],0,0);sem_init(&replies[i],0,0);
        if(pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i))abort();
    }
#endif
    __atomic_store_n(&initialized,1,__ATOMIC_RELEASE);
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
    XK_LOG("[object-pass] compiled 1; timing requires live owner-phase admission; worker snapshots only at quiescent frame reports\n");
#endif
    XK_LOG("[object-jobs] guest stacks %08X/%08X/%08X, %u bytes each\n",stacks[0],stacks[1],stacks[2],STACK_BYTES);
    XK_LOG("[object-locks] wait-site profiling %s; elapsed waits include scheduling and parked service time\n",math_profile?"on":"off");
    XK_LOG("[object-locks] backend %s; lightweight available %d\n",xv_object_mutex_name(&math_mutex),math_mutex.light_ready);
    XK_LOG("[object-point] private input/output bypass %s\n",point_private_enabled?"on":"off");
    XK_LOG("[object-locks] contention wait %s; 50 us service-check budget\n",math_wait_enabled?"bounded mutex":"sleep/poll");
    XK_LOG("[object-locks] code anchor %llX symbol xv_object_math_lock; private math %s\n",
        (unsigned long long)(uintptr_t)xv_object_math_lock,math_private_enabled?"on":"off");
    XK_LOG("[object-jobs] EXPERIMENT: whole object callbacks on core 0/1; owner services kernel requests and joins; shared game state and reordered updates are unproven\n");
    return 1;
fail:
#ifdef __vita__
    for(unsigned i=0;i<WORKERS;i++) {
        if(threads[i]>=0)sceKernelDeleteThread(threads[i]);
        if(wakes[i]>=0)sceKernelDeleteSema(wakes[i]);
        if(dones[i]>=0)sceKernelDeleteSema(dones[i]);
        if(replies[i]>=0)sceKernelDeleteSema(replies[i]);
    }
    if(owner_wake>=0)sceKernelDeleteSema(owner_wake);
#endif
    xv_object_mutex_destroy(&math_mutex);
    for(unsigned i=0;i<LANES;i++)if(stacks[i]) { xk_mem_free(stacks[i]);stacks[i]=0; }
    XK_LOG("[object-jobs] unavailable: worker or private-stack allocation failed; serial callbacks retained\n");
    return 0;
}
int xv_object_jobs_begin(xctx *c)
{
    if(configured<0) { const char *e=getenv("XV_EXPERIMENTAL_OBJECT_JOBS");configured=e?atoi(e)!=0:1; }
    if(!(override<0?configured:override)||owner||xv_phase_enabled||xv_is_object_job(c))return 0;
    /* Map construction/cinematic initialization has ordering dependencies.
     * Only opt into jobs once the owner observes a rendered gameplay view. */
    extern int xd3d_object_jobs_ready(void) __attribute__((weak));
    if(!xd3d_object_jobs_ready || !xd3d_object_jobs_ready())return 0;
    if(!initialize())return 0;
    owner=c;
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
    pass_timing_begin(c);
#endif
    return 1;
}
/* Guest-owner admission, never called by the network service thread. */
int xv_object_jobs_available(void)
{ return !xv_phase_enabled && initialize(); }
int xv_object_jobs_queue(xctx *c)
{
    if(c!=owner||X_M32(c->r[4])!=0x90299u)return 0;
    if(count==CAPACITY)xv_object_jobs_join();
    jobs[count++]=*c; submitted++;
    /* This call site's next iteration overwrites volatile inputs. Whole guest
     * context equivalence is deliberately not claimed by this experiment. */
    c->r[0]=(c->r[0]&~255u)|1u;c->r[4]+=4;return 1;
}
void xv_object_jobs_join(void)
{
    if(!count)return;
    uint64_t started=xk_os_monotonic_us();
#ifdef XV_TYPED_CLUSTER_QUERY
    if(active_workers&&query_enabled)xv_cluster_runtime_begin();
#endif
    next=0;__atomic_store_n(&running,1,__ATOMIC_RELEASE);
    memset(service_state,0,sizeof service_state);
    for(unsigned i=0;i<active_workers;i++) {
#ifdef __vita__
        if(sceKernelSignalSema(wakes[i],1)<0)abort();
#else
        sem_post(&wakes[i]);
#endif
    }
    /* The owner must stay available: running a callback here could block on a
     * mutex held by a worker awaiting its kernel service and deadlock the batch.
     * Main-thread work outside this joined object pass remains on core 2. */
    if(active_workers)service_owner();else execute(2);
    for(unsigned i=0;i<active_workers;i++) {
#ifdef __vita__
        if(sceKernelWaitSema(dones[i],1,NULL)<0)abort();
#else
        wait_sem(&dones[i]);
#endif
    }
    /* Every worker posts its final owner notification before its done semaphore.
     * With all done semaphores consumed, no old notification can arrive later. */
#ifdef __vita__
    int drained;
    while((drained=sceKernelPollSema(owner_wake,1))==0) {}
    if(drained!=(int)SCE_KERNEL_ERROR_SEMA_ZERO)abort();
#else
    while(sem_trywait(&owner_wake)==0||errno==EINTR) {}
    if(errno!=EAGAIN)abort();
#endif
    __atomic_store_n(&owner_notice,0,__ATOMIC_RELEASE);
    __atomic_store_n(&running,0,__ATOMIC_RELEASE);
#ifdef XV_TYPED_CLUSTER_QUERY
    xv_cluster_runtime_end();
#endif
    batch_us+=xk_os_monotonic_us()-started;batches++;count=0;
}
void xv_object_jobs_end(xctx **c)
{ if(*c)xv_object_jobs_finish(*c); }
void xv_object_jobs_finish(xctx *c)
{
    if(c==owner) {
        xv_object_jobs_join();
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
        pass_timing_finish(c);
#endif
        owner=NULL;passes++;
#if XV_QUERY_REUSE
        extern void xv_query_reuse_epoch(void);
        xv_query_reuse_epoch();
#endif
#if XV_QUERY_REPEAT_CENSUS
        extern void xv_query_repeat_probe_epoch(void);
        xv_query_repeat_probe_epoch();
#endif
    }
}
void xv_object_jobs_override(int enabled)
{
    xv_object_jobs_join();
    /* Initialize before the first off arm, so all arms include the same
     * native-helper locking and reserved-memory overhead. */
    if(enabled>=0)initialize();
    override=enabled<0?-1:!!enabled;
}
void xv_object_jobs_report(unsigned frames)
{
    /* Called with the renderer's frame window, not every 60 simulation passes.
     * Reporting must never dispatch callbacks or reset live worker counters. */
    if(initialized!=1||owner||count||__atomic_load_n(&running,__ATOMIC_ACQUIRE))return;
#if XV_QUERY_REUSE
    extern void xv_query_reuse_report(unsigned);
    xv_query_reuse_report(frames);
#endif
#if XV_QUERY_REPEAT_CENSUS
    extern void xv_query_repeat_probe_report(unsigned);
    xv_query_repeat_probe_report(frames);
#endif
#if XV_QUERY_WORLD_RUN
    xv_query_world_run_report(frames);
#endif
#if XV_NATIVE_VISIBILITY_JOBS
    if(census_is_owner()) {
        XK_LOG("[visibility-jobs] %u frames batches %u lanes %u/%u/%u items; copied native data, joined before publication\n",
            frames,visibility_lane[2].calls,visibility_lane[0].items,visibility_lane[1].items,visibility_lane[2].items);
        memset(visibility_lane,0,sizeof visibility_lane);
    }
#endif
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
    pass_timing_report(frames);
#endif
#ifdef XV_OBJECT_HOLD_PROFILE
    hold_report(frames);
#endif
#ifdef XV_WORKER_QUERY
    for(unsigned lane=0;lane<WORKERS;lane++)
        XK_LOG("[worker-query] lane %u admission-checks accepted/disabled/nested/stack %u/%u/%u/%u\n",lane,
            query_admission[lane][0],query_admission[lane][1],query_admission[lane][2],query_admission[lane][3]);
    memset(query_admission,0,sizeof query_admission);xv_worker_query_report();
#endif
    XK_LOG("[object-jobs] %u frames passes %u batches %u jobs %u lanes %u/%u/%u work-us %llu/%llu/%llu batch-us %llu rejected %u; work sums overlap wall time\n",
        frames,passes,batches,submitted,executed[0],executed[1],executed[2],
        (unsigned long long)work_us[0],(unsigned long long)work_us[1],(unsigned long long)work_us[2],
        (unsigned long long)batch_us,rejected);
    XK_LOG("[object-jobs] owner event services %u cache yields %u resource queries %u registrations %u vertex locks %u\n",services,io_yields,resource_queries,resource_registers,vertex_locks);services=io_yields=resource_queries=resource_registers=vertex_locks=0;
    XK_LOG("[object-jobs] quiescent owner audio pumps %u\n",audio_pumps);audio_pumps=0;
    XK_LOG("[object-jobs] quiescent owner stream volume updates %u\n",audio_volumes);audio_volumes=0;
    XK_LOG("[object-jobs] quiescent owner deferred audio commits %u\n",audio_commits);audio_commits=0;
    XK_LOG("[object-jobs] quiescent owner stream-start commits %u\n",audio_start_commits);audio_start_commits=0;
    XK_LOG("[object-jobs] quiescent owner stream status %u packets %u\n",audio_status,audio_packets);audio_status=audio_packets=0;
    XK_LOG("[object-jobs] quiescent owner voice stops %u\n",audio_stops);audio_stops=0;
    XK_LOG("[object-jobs] quiescent owner frequency updates %u spatial parameters %u\n",audio_frequencies,audio_parameters);audio_frequencies=audio_parameters=0;
    XK_LOG("[object-wait] timed %u attempts %u/%u acquired %u/%u timeouts %u/%u\n",
        math_wait_override<0?math_wait_enabled:(unsigned)math_wait_override,
        math_wait_stats[0].attempts,math_wait_stats[1].attempts,
        math_wait_stats[0].acquired,math_wait_stats[1].acquired,
        math_wait_stats[0].timeouts,math_wait_stats[1].timeouts);
    memset(math_wait_stats,0,sizeof math_wait_stats);
#ifdef XV_OBJECT_POSE_EXPERIMENT
    for(unsigned lane=0;lane<WORKERS;lane++) {
        if(pose_depth[lane])abort();
        XK_LOG("[object-pose] lane %u enabled %u entered %u enclosing %u finished %u cleanup %u recursive-locks %u\n",
            lane,pose_enabled,pose_stats[lane].entered,pose_stats[lane].enclosing,
            pose_stats[lane].finished,pose_stats[lane].cleanup,pose_stats[lane].recursive_locks);
    }
    memset(pose_stats,0,sizeof pose_stats);
#endif
    for(unsigned lane=0;lane<WORKERS;lane++)
        XK_LOG("[object-point] lane %u checks %u private %u nested %u shared-input %u shared-output %u\n",
            lane,point_private_stats[lane].checks,point_private_stats[lane].admitted,
            point_private_stats[lane].nested,point_private_stats[lane].shared_input,
            point_private_stats[lane].shared_output);
    memset(point_private_stats,0,sizeof point_private_stats);
#ifdef XV_OBJECT_QUAT_EXPERIMENT
    for(unsigned lane=0;lane<WORKERS;lane++)
        XK_LOG("[object-quat] lane %u checks %u private %u nested %u shared-input %u shared-output %u constants %u\n",
            lane,quat_private_stats[lane].checks,quat_private_stats[lane].admitted,
            quat_private_stats[lane].nested,quat_private_stats[lane].shared_input,
            quat_private_stats[lane].shared_output,quat_private_stats[lane].constants);
    memset(quat_private_stats,0,sizeof quat_private_stats);
#endif
    for(unsigned lane=0;lane<WORKERS;lane++)for(unsigned site=0;site<=POINT_SITES;site++)
        if(point_sites[lane][site].count)
            XK_LOG("[object-point-site] lane %u pc %08X count %u varied %u first output %08X matrix %08X vector %08X object %08X callback %08X overflow %u\n",
                lane,point_sites[lane][site].pc,point_sites[lane][site].count,point_sites[lane][site].varied,
                point_sites[lane][site].output,point_sites[lane][site].matrix,point_sites[lane][site].vector,
                point_sites[lane][site].object,point_sites[lane][site].callback,site==POINT_SITES);
    memset(point_sites,0,sizeof point_sites);
#ifdef XV_OBJECT_QUAT_PROFILE
    for(unsigned lane=0;lane<WORKERS;lane++)for(unsigned site=0;site<=QUAT_SITES;site++)
        if(quat_sites[lane][site].count)
            XK_LOG("[object-quat-site] lane %u pc %08X count %u private-input %u first input %08X output %08X overflow %u\n",
                lane,quat_sites[lane][site].pc,quat_sites[lane][site].count,quat_sites[lane][site].private_input,
                quat_sites[lane][site].input,quat_sites[lane][site].output,site==QUAT_SITES);
    memset(quat_sites,0,sizeof quat_sites);
#endif
    XK_LOG("[object-jobs] probed stack peak bytes %u/%u/%u of %u; excludes unprobed small frames\n",stack_peak[0],stack_peak[1],stack_peak[2],STACK_BYTES);
    XK_LOG("[object-locks] fast %u idle-owner %u acquired %u/%u/%u nested %u/%u contended %u/%u wait-us %llu/%llu; worker waits overlap\n",
        math_fast_path,math_idle_calls,math_stats[0].acquired,math_stats[1].acquired,math_stats[2].acquired,
        math_stats[0].nested,math_stats[1].nested,math_stats[0].contended,math_stats[1].contended,
        (unsigned long long)math_stats[0].wait_us,(unsigned long long)math_stats[1].wait_us);
    for(unsigned lane=0;lane<WORKERS;lane++)for(unsigned site=0;site<=WAIT_SITES;site++)
        if(wait_sites[lane][site].count) {
            XK_LOG("[object-lock-site] lane %u pc %llX count %u wait-us %llu max-us %llu overflow %u\n",
                lane,(unsigned long long)wait_sites[lane][site].pc,wait_sites[lane][site].count,
                (unsigned long long)wait_sites[lane][site].us,
                (unsigned long long)wait_sites[lane][site].max_us,site==WAIT_SITES);
        }
    XK_LOG("[object-lock-backend] %s\n",xv_object_mutex_name(&math_mutex));
    memset(wait_sites,0,sizeof wait_sites);
    for(unsigned lane=0;lane<WORKERS;lane++)for(unsigned kind=0;kind<4;kind++)
        if(private_stats[lane][kind].attempted) {
            static const char *names[]={"point","matrix","quaternion","basis"};
            XK_LOG("[object-private-math] lane %u %s attempted %u released %u nested %u shared %u disabled %u\n",
                lane,names[kind],private_stats[lane][kind].attempted,
                private_stats[lane][kind].released,private_stats[lane][kind].nested,
                private_stats[lane][kind].shared,private_stats[lane][kind].disabled);
        }
    memset(private_stats,0,sizeof private_stats);
    math_idle_calls=0;memset(math_stats,0,sizeof math_stats);
    passes=batches=submitted=rejected=0;memset(executed,0,sizeof executed);memset(work_us,0,sizeof work_us);batch_us=0;
}
/* Halo's original large-frame helper adjusts ESP without probing guard pages.
 * Validate before it writes the relocated return address. Keep all guest
 * registers/flags and the original allocation body unchanged. */
void xv_object_job_stack_probe(xctx *c)
{
    if(!xv_is_object_job(c))return;
    for(unsigned i=0;i<LANES;i++)if(c==&contexts[i]) {
        uint32_t sp=c->r[4],bytes=c->r[0],low=stacks[i]+256,top=stacks[i]+STACK_BYTES;
        if(sp<low||sp>top-4||bytes>sp-low)
            xv_object_job_stop(c,0x1D130u,"guest stack allocation exceeds worker capacity");
        unsigned used=top-(sp-bytes);
        if(used>stack_peak[i])stack_peak[i]=used;
        return;
    }
    xv_object_job_stop(c,0x1D130u,"stack allocation outside active worker");
}
void xv_object_job_indirect(xctx *c,unsigned target)
{
    for(unsigned i=0;i<LANES;i++)if(c==&contexts[i]) {
        unsigned *depth=&indirect_depth[i];
        if(target) {if(*depth<32)indirect_stack[i][*depth]=target;(*depth)++;}
        else if(*depth)(*depth)--;
        return;
    }
}
void xv_object_job_stop(xctx *c,unsigned address,const char *reason)
{
    XV_LIGHT_CENSUS_CANCEL(c,XV_LC_STOP);
    uint32_t object=c->r[1];
    for(unsigned i=0;i<LANES;i++)if(c==&contexts[i]) {
        object=active_objects[i];char chain[300];unsigned used=0;
        XK_LOG("[object-jobs] fault stack %08X..%08X esp %08X canary %08X\n",stacks[i],stacks[i]+STACK_BYTES,c->r[4],X_M32(stacks[i]));
        XK_LOG("[object-jobs] fault registers %08X %08X %08X %08X %08X %08X %08X %08X\n",c->r[0],c->r[1],c->r[2],c->r[3],c->r[4],c->r[5],c->r[6],c->r[7]);
        for(unsigned word=0;word<24&&c->r[4]>=stacks[i]&&c->r[4]<=stacks[i]+STACK_BYTES-4-4*word;word++)
            XK_LOG("[object-jobs] fault stack +%02X %08X\n",4*word,X_M32(c->r[4]+4*word));
        uint32_t frame=c->r[5];
        for(unsigned depth=0;depth<16&&frame>=stacks[i]&&frame<=stacks[i]+STACK_BYTES-8;depth++) {
            uint32_t previous=X_M32(frame),back=X_M32(frame+4);
            XK_LOG("[object-jobs] guest frame %u bp %08X return %08X previous %08X\n",depth,frame,back,previous);
            if(previous<=frame)break;
            frame=previous;
        }
        for(unsigned k=0;k<indirect_depth[i]&&k<32;k++)used+=(unsigned)snprintf(chain+used,sizeof chain-used," %08X",indirect_stack[i][k]);
        chain[used]=0;XK_LOG("[object-jobs] fault lane %u indirect-depth %u targets%s\n",i,indirect_depth[i],chain);
    }
    XK_LOG("[object-jobs] STOP %s target %08X return %08X object %08X; experiment cannot enter owner-only services\n",
        reason,address,X_M32(c->r[4]),object);
    abort();
}
void xv_object_job_hle(xctx *c,unsigned address,xv_fn_t fn)
{
    if(address==0x1D675Cu) {fn(c);return;} /* stateless memory comparison */
    if(address==0x19384Fu||address==0x193884u) {
        /* Both 2A210 -> 28B70 startup and completion callback 29590 refill
         * through 28B00/28870. Worker calls queue to the quiescent owner;
         * nested completion calls run inline there to avoid recursive RPC. */
        unsigned back=address==0x19384Fu?0x28B35u:0x289FDu;
        if(!fn||X_M32(c->r[4])!=back)
            xv_object_job_stop(c,address,"stream service outside audited refill");
        if(__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE)==c) {
            if(worker_lane()>=0)xv_object_job_stop(c,address,"stream callback outside audio owner");
            fn(c);return;
        }
    }
    if(__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE)==c)
        xv_object_job_stop(c,address,"unsupported nested owner audio service");
    unsigned parameter_return=sound_parameter_return(address);
    if((address!=0x1D665Cu&&address!=0x1D6640u&&address!=0x184A20u&&address!=0x184AB0u&&address!=0x1858D0u&&address!=0x193E27u&&address!=0x193D4Fu&&address!=0x193C1Bu&&address!=0x19C5FFu&&address!=0x19384Fu&&address!=0x193884u&&!parameter_return)||!fn)
        xv_object_job_stop(c,address,"unsupported HLE");
    if(parameter_return&&X_M32(c->r[4])!=parameter_return)
        xv_object_job_stop(c,address,"stream parameter outside audited object sound update");
    /* Object sound cleanup 28710 stops its active voice before marking the
     * guest slot inactive. Preserve the real handler and ret 4 convention. */
    if(address==0x19C5FFu&&X_M32(c->r[4])!=0x28745u)
        xv_object_job_stop(c,address,"voice stop outside audited object sound cleanup");
    /* 28B70 commits before starting a previously idle stream; 291D0 commits
     * during the existing update path. Both pass one DirectSound argument. */
    if(address==0x193C1Bu&&X_M32(c->r[4])!=0x291EFu&&X_M32(c->r[4])!=0x28BB6u)
        xv_object_job_stop(c,address,"deferred audio commit outside audited sound update");
    /* 291D0 also fades the active streams after committing deferred settings.
     * Both callers pass the stream object and a signed, clamped volume. */
    if(address==0x193D4Fu&&X_M32(c->r[4])!=0x2982Fu&&X_M32(c->r[4])!=0x292FBu)
        xv_object_job_stop(c,address,"stream volume outside audited object sound update");
    if(address==0x193E27u&&X_M32(c->r[4])!=0x29427u)
        xv_object_job_stop(c,address,"audio pump outside audited cache callback");
    if(address==0x1858D0u&&X_M32(c->r[4])!=0x116240u)
        xv_object_job_stop(c,address,"vertex lock outside audited impact transaction");
    if(address==0x1D6640u && (X_M32(c->r[4])!=0x12AA9u||
        (X_M32(c->r[4]+4)!=0x32B60u&&X_M32(c->r[4]+4)!=0x3268Au&&
         X_M32(c->r[4]+4)!=0x17A804u)))
        xv_object_job_stop(c,address,"yield outside audited cache wait");
    if(c==&contexts[2]&&!active_workers) {
        if(address==0x1D6640u) {
            extern int xk_object_io_step(void);
            if(xk_object_io_step()<0)abort();
            c->r[0]=STATUS_SUCCESS;c->r[4]+=4;io_yields++;
        } else if(address==0x193E27u||address==0x19384Fu||address==0x193884u)service_audio(c,fn,address);
        else if(address==0x193D4Fu){fn(c);audio_volumes++;}
        else if(address==0x193C1Bu)service_audio_commit(c,fn);
        else if(address==0x19C5FFu){fn(c);audio_stops++;}
        else if(parameter_return){fn(c);if(address==0x194470u)audio_frequencies++;else audio_parameters++;}
        else {fn(c);if(address==0x184A20u)resource_queries++;else if(address==0x184AB0u)resource_registers++;else if(address==0x1858D0u)vertex_locks++;else services++;}
        return;
    }
    for(unsigned i=0;i<active_workers;i++)if(c==&contexts[i]) {
        service_fn[i]=fn;service_address[i]=address;
        if(needs_quiescence(address))__atomic_store_n(&pause_workers,1,__ATOMIC_RELEASE);
        __atomic_store_n(&service_state[i],1,__ATOMIC_RELEASE);notify_owner();
        wait_reply(i);return;
    }
    xv_object_job_stop(c,address,"kernel service outside active worker");
}
void xv_object_jobs_shutdown(void)
{
#ifdef XV_LIGHT_QUERY_CENSUS
    xv_light_census_cancel(NULL,XV_LC_STOP);
#endif
    if(initialized!=1)return;
    xv_object_jobs_join();
#if defined(XV_OBJECT_PASS_TIMING) && XV_OBJECT_PASS_TIMING
    pass_timing_cancel();
#endif
    owner=NULL;
    __atomic_store_n(&stopping,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<WORKERS;i++) {
#ifdef __vita__
        sceKernelSignalSema(wakes[i],1);sceKernelWaitThreadEnd(threads[i],NULL,NULL);
        sceKernelDeleteThread(threads[i]);sceKernelDeleteSema(wakes[i]);sceKernelDeleteSema(dones[i]);
        sceKernelDeleteSema(replies[i]);
#else
        sem_post(&wakes[i]);pthread_join(threads[i],NULL);sem_destroy(&wakes[i]);sem_destroy(&dones[i]);sem_destroy(&replies[i]);
#endif
    }
#ifdef __vita__
    sceKernelDeleteSema(owner_wake);
#else
    sem_destroy(&owner_wake);
#endif
    xv_object_mutex_destroy(&math_mutex);
    for(unsigned i=0;i<LANES;i++)xk_mem_free(stacks[i]);
    initialized=-1;
}
#endif
