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
#include "xk_object_mutex.h"
#include "../xv_phase.h"
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
static unsigned stack_peak[LANES];
static uint32_t active_objects[LANES], indirect_stack[LANES][32];
static unsigned indirect_depth[LANES];
/* A worker publishes one request, then parks until the guest owner replies.
 * States: 0 executing, 1 service request, 2 completed, 3 completion consumed,
 * 4 parked (including an event reply held during an I/O handoff). */
static unsigned service_state[WORKERS], services, owner_notice, pause_workers, io_yields, resource_queries, resource_registers;
static unsigned vertex_locks;
static unsigned audio_pumps, audio_volumes, audio_commits, audio_stops;
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
    return address==0x1D6640u||address==0x184AB0u||address==0x1858D0u||address==0x193E27u||address==0x193D4Fu||address==0x193C1Bu||address==0x19C5FFu||sound_parameter_return(address);
}
static void service_audio(xctx *c,xv_fn_t fn)
{
    if(__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE))abort();
    __atomic_store_n(&audio_service_context,c,__ATOMIC_RELEASE);
    fn(c);
    __atomic_store_n(&audio_service_context,NULL,__ATOMIC_RELEASE);
    audio_pumps++;
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
            /* Every lane is parked or completed. Run only the original cache
             * file fiber, preserving its stack, TLS, real reads and APCs. */
            extern int xk_object_io_step(void);
            if(yielding&&xk_object_io_step()<0)abort();
            for(unsigned i=0;i<active_workers;i++)
                if(__atomic_load_n(&service_state[i],__ATOMIC_ACQUIRE)==1) {
                    if(service_address[i]==0x1D6640u) {
                        contexts[i].r[0]=STATUS_SUCCESS;contexts[i].r[4]+=4;io_yields++;
                    } else if(service_address[i]==0x193E27u) {
                        /* Preserve real packet completion and original callback
                         * execution, with every other object lane parked. */
                        service_audio(&contexts[i],service_fn[i]);
                    } else if(service_address[i]==0x193D4Fu) {
                        /* Original stream volume update; no guest callbacks. */
                        service_fn[i](&contexts[i]);audio_volumes++;
                    } else if(service_address[i]==0x193C1Bu) {
                        /* Keep the existing deferred-settings handler on its
                         * owner, including its one-argument return convention. */
                        service_fn[i](&contexts[i]);audio_commits++;
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
            if(math_fast_path) { math_depth[lane]=1;return lane+2; }
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
    owner=c;return 1;
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
    batch_us+=xk_os_monotonic_us()-started;batches++;count=0;
}
void xv_object_jobs_end(xctx **c)
{ if(*c)xv_object_jobs_finish(*c); }
void xv_object_jobs_finish(xctx *c)
{
    if(c==owner) {
        xv_object_jobs_join();owner=NULL;
        passes++;
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
    XK_LOG("[object-jobs] %u frames passes %u batches %u jobs %u lanes %u/%u/%u work-us %llu/%llu/%llu batch-us %llu rejected %u; work sums overlap wall time\n",
        frames,passes,batches,submitted,executed[0],executed[1],executed[2],
        (unsigned long long)work_us[0],(unsigned long long)work_us[1],(unsigned long long)work_us[2],
        (unsigned long long)batch_us,rejected);
    XK_LOG("[object-jobs] owner event services %u cache yields %u resource queries %u registrations %u vertex locks %u\n",services,io_yields,resource_queries,resource_registers,vertex_locks);services=io_yields=resource_queries=resource_registers=vertex_locks=0;
    XK_LOG("[object-jobs] quiescent owner audio pumps %u\n",audio_pumps);audio_pumps=0;
    XK_LOG("[object-jobs] quiescent owner stream volume updates %u\n",audio_volumes);audio_volumes=0;
    XK_LOG("[object-jobs] quiescent owner deferred audio commits %u\n",audio_commits);audio_commits=0;
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
        /* Original completion callback 29590 can refill through 28B00/28870.
         * These two vtable services execute inline on the servicing owner;
         * queueing a second RPC here would wait for that same owner forever. */
        unsigned back=address==0x19384Fu?0x28B35u:0x289FDu;
        if(!fn||__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE)!=c||
           worker_lane()>=0||X_M32(c->r[4])!=back)
            xv_object_job_stop(c,address,"stream service outside quiescent audio callback");
        fn(c);return;
    }
    if(__atomic_load_n(&audio_service_context,__ATOMIC_ACQUIRE)==c)
        xv_object_job_stop(c,address,"unsupported nested owner audio service");
    unsigned parameter_return=sound_parameter_return(address);
    if((address!=0x1D665Cu&&address!=0x1D6640u&&address!=0x184A20u&&address!=0x184AB0u&&address!=0x1858D0u&&address!=0x193E27u&&address!=0x193D4Fu&&address!=0x193C1Bu&&address!=0x19C5FFu&&!parameter_return)||!fn)
        xv_object_job_stop(c,address,"unsupported HLE");
    if(parameter_return&&X_M32(c->r[4])!=parameter_return)
        xv_object_job_stop(c,address,"stream parameter outside audited object sound update");
    /* Object sound cleanup 28710 stops its active voice before marking the
     * guest slot inactive. Preserve the real handler and ret 4 convention. */
    if(address==0x19C5FFu&&X_M32(c->r[4])!=0x28745u)
        xv_object_job_stop(c,address,"voice stop outside audited object sound cleanup");
    if(address==0x193C1Bu&&X_M32(c->r[4])!=0x291EFu)
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
        } else if(address==0x193E27u)service_audio(c,fn);
        else if(address==0x193D4Fu){fn(c);audio_volumes++;}
        else if(address==0x193C1Bu){fn(c);audio_commits++;}
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
    xv_object_jobs_join();owner=NULL;
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
