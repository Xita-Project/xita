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
static unsigned stack_peak[LANES];
static uint32_t active_objects[LANES], indirect_stack[LANES][32];
static unsigned indirect_depth[LANES];
/* A worker publishes one request, then parks until the guest owner replies.
 * States: 0 executing, 1 service request, 2 completed, 3 completion consumed,
 * 4 parked (including an event reply held during an I/O handoff). */
static unsigned service_state[WORKERS], services, owner_notice, pause_workers, io_yields, resource_queries, resource_registers;
static unsigned vertex_locks;
static unsigned service_address[WORKERS];
static xv_fn_t service_fn[WORKERS];
static int initialized, override=-1;
static xctx *owner;
static unsigned batches, submitted, executed[LANES], rejected;
static uint64_t work_us[LANES], batch_us;
#ifdef __vita__
static SceUID threads[WORKERS]={-1,-1}, wakes[WORKERS]={-1,-1}, dones[WORKERS]={-1,-1}, math_mutex=-1;
static SceUID owner_wake=-1, replies[WORKERS]={-1,-1};
#else
static pthread_t threads[WORKERS];
static sem_t wakes[WORKERS], dones[WORKERS];
static sem_t owner_wake, replies[WORKERS];
static pthread_mutex_t math_mutex;
#endif

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
static int needs_quiescence(unsigned address)
{
    return address==0x1D6640u||address==0x184AB0u||address==0x1858D0u;
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
        __atomic_store_n(&owner_notice,0,__ATOMIC_RELEASE);
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

int xv_object_math_lock(void)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
    int lane=worker_lane();
    if(lane>=0)for(;;) {
        park_worker((unsigned)lane);
#ifdef __vita__
        int result=sceKernelTryLockMutex(math_mutex,1);
        if(!result)return 1;
        if(result!=(int)SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN)abort();
        sceKernelDelayThread(50);
#else
        int result=pthread_mutex_trylock(&math_mutex);
        if(!result)return 1;
        if(result!=EBUSY)abort();
        struct timespec delay={0,50000};nanosleep(&delay,NULL);
#endif
    }
#ifdef __vita__
    if(sceKernelLockMutex(math_mutex,1,NULL)<0)abort();
#else
    if(pthread_mutex_lock(&math_mutex))abort();
#endif
    return 1;
}

void xv_object_math_unlock(int *locked)
{
    if(!*locked)return;
#ifdef __vita__
    if(sceKernelUnlockMutex(math_mutex,1)<0)abort();
#else
    if(pthread_mutex_unlock(&math_mutex))abort();
#endif
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
    const char *workers=getenv("XV_OBJECT_JOB_WORKERS");
    if(workers && (!strcmp(workers,"0")||!strcmp(workers,"1")))active_workers=(unsigned)atoi(workers);
    if(active_workers!=WORKERS)XK_LOG("[object-jobs] DIAGNOSTIC: %u active workers; zero selects owner-only execution\n",active_workers);
    for(unsigned i=0;i<LANES;i++) {
        stacks[i]=xk_mem_alloc(STACK_BYTES,4096,0,0,1);
        if(!stacks[i])goto fail;
    }
#ifdef __vita__
    math_mutex=sceKernelCreateMutex("xv_object_math",SCE_KERNEL_MUTEX_ATTR_RECURSIVE,0,NULL);
    if(math_mutex<0)goto fail;
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
    pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&math_mutex,&attr);pthread_mutexattr_destroy(&attr);
    sem_init(&owner_wake,0,0);
    for(unsigned i=0;i<WORKERS;i++) {
        sem_init(&wakes[i],0,0);sem_init(&dones[i],0,0);sem_init(&replies[i],0,0);
        if(pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i))abort();
    }
#endif
    __atomic_store_n(&initialized,1,__ATOMIC_RELEASE);
    XK_LOG("[object-jobs] guest stacks %08X/%08X/%08X, %u bytes each\n",stacks[0],stacks[1],stacks[2],STACK_BYTES);
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
    if(math_mutex>=0)sceKernelDeleteMutex(math_mutex);
    if(owner_wake>=0)sceKernelDeleteSema(owner_wake);
#endif
    for(unsigned i=0;i<LANES;i++)if(stacks[i]) { xk_mem_free(stacks[i]);stacks[i]=0; }
    XK_LOG("[object-jobs] unavailable: worker or private-stack allocation failed; serial callbacks retained\n");
    return 0;
}
int xv_object_jobs_begin(xctx *c)
{
    static int configured=-1;
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
    static unsigned passes;
    if(c==owner) {
        xv_object_jobs_join();owner=NULL;
        if(++passes==60) { xv_object_jobs_report(passes);passes=0; }
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
    if(!initialized)return;
    xv_object_jobs_join();
    XK_LOG("[object-jobs] %u passes batches %u jobs %u lanes %u/%u/%u work-us %llu/%llu/%llu batch-us %llu rejected %u; work sums overlap wall time\n",
        frames,batches,submitted,executed[0],executed[1],executed[2],
        (unsigned long long)work_us[0],(unsigned long long)work_us[1],(unsigned long long)work_us[2],
        (unsigned long long)batch_us,rejected);
    XK_LOG("[object-jobs] owner event services %u cache yields %u resource queries %u registrations %u vertex locks %u\n",services,io_yields,resource_queries,resource_registers,vertex_locks);services=io_yields=resource_queries=resource_registers=vertex_locks=0;
    XK_LOG("[object-jobs] probed stack peak bytes %u/%u/%u of %u; excludes unprobed small frames\n",stack_peak[0],stack_peak[1],stack_peak[2],STACK_BYTES);
    batches=submitted=rejected=0;memset(executed,0,sizeof executed);memset(work_us,0,sizeof work_us);batch_us=0;
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
    if((address!=0x1D665Cu&&address!=0x1D6640u&&address!=0x184A20u&&address!=0x184AB0u&&address!=0x1858D0u)||!fn)
        xv_object_job_stop(c,address,"unsupported HLE");
    if(address==0x1858D0u&&X_M32(c->r[4])!=0x116240u)
        xv_object_job_stop(c,address,"vertex lock outside audited impact transaction");
    if(address==0x1D6640u && (X_M32(c->r[4])!=0x12AA9u||
        (X_M32(c->r[4]+4)!=0x32B60u&&X_M32(c->r[4]+4)!=0x3268Au)))
        xv_object_job_stop(c,address,"yield outside audited cache wait");
    if(c==&contexts[2]&&!active_workers) {
        if(address==0x1D6640u) {
            extern int xk_object_io_step(void);
            if(xk_object_io_step()<0)abort();
            c->r[0]=STATUS_SUCCESS;c->r[4]+=4;io_yields++;
        } else {fn(c);if(address==0x184A20u)resource_queries++;else if(address==0x184AB0u)resource_registers++;else if(address==0x1858D0u)vertex_locks++;else services++;}
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
    sceKernelDeleteMutex(math_mutex);
    sceKernelDeleteSema(owner_wake);
#else
    pthread_mutex_destroy(&math_mutex);
    sem_destroy(&owner_wake);
#endif
    for(unsigned i=0;i<LANES;i++)xk_mem_free(stacks[i]);
    initialized=-1;
}
#endif
