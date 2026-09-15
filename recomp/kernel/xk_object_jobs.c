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
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#endif

enum { WORKERS=2, LANES=3, CAPACITY=128, STACK_BYTES=65536 };
const char xv_object_job_marker=0;
static xctx jobs[CAPACITY], contexts[LANES];
static unsigned count, next, running, stopping;
static uint32_t stacks[LANES];
static int initialized, override=-1;
static xctx *owner;
static unsigned batches, submitted, executed[LANES], rejected;
static uint64_t work_us[LANES], batch_us;
#ifdef __vita__
static SceUID threads[WORKERS]={-1,-1}, wakes[WORKERS]={-1,-1}, dones[WORKERS]={-1,-1}, math_mutex=-1;
#else
static pthread_t threads[WORKERS];
static sem_t wakes[WORKERS], dones[WORKERS];
static pthread_mutex_t math_mutex;
#endif

int xv_object_math_lock(void)
{
    if(__atomic_load_n(&initialized,__ATOMIC_ACQUIRE)!=1)return 0;
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
        xctx *c=&contexts[lane]; *c=jobs[i];
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
        if(sceKernelSignalSema(dones[lane],1)<0)abort();
    }
}
#else
static void wait_sem(sem_t *s) { while(sem_wait(s))if(errno!=EINTR)abort(); }
static void *worker(void *arg)
{
    unsigned lane=(unsigned)(uintptr_t)arg;
    for(;;) {
        wait_sem(&wakes[lane]);
        if(__atomic_load_n(&stopping,__ATOMIC_ACQUIRE))return NULL;
        execute(lane); sem_post(&dones[lane]);
    }
}
#endif
static int initialize(void)
{
    if(initialized)return initialized>0;
    initialized=-1;
    for(unsigned i=0;i<LANES;i++) {
        stacks[i]=xk_mem_alloc(STACK_BYTES,4096,0,0,1);
        if(!stacks[i])goto fail;
    }
#ifdef __vita__
    math_mutex=sceKernelCreateMutex("xv_object_math",SCE_KERNEL_MUTEX_ATTR_RECURSIVE,0,NULL);
    if(math_mutex<0)goto fail;
    for(unsigned i=0;i<WORKERS;i++) {
        wakes[i]=sceKernelCreateSema("xv_object_wake",0,0,1,NULL);
        dones[i]=sceKernelCreateSema("xv_object_done",0,0,1,NULL);
        if(wakes[i]<0||dones[i]<0)goto fail;
        threads[i]=sceKernelCreateThread(i?"xv_objects_c1":"xv_objects_c0",worker,
            sceKernelGetThreadCurrentPriority(),512*1024,0,1u<<i,NULL);
        if(threads[i]<0)goto fail;
    }
    for(unsigned i=0;i<WORKERS;i++)
        if(sceKernelStartThread(threads[i],sizeof i,&i)<0)abort();
#else
    pthread_mutexattr_t attr;pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&math_mutex,&attr);pthread_mutexattr_destroy(&attr);
    for(unsigned i=0;i<WORKERS;i++) {
        sem_init(&wakes[i],0,0);sem_init(&dones[i],0,0);
        if(pthread_create(&threads[i],NULL,worker,(void *)(uintptr_t)i))abort();
    }
#endif
    __atomic_store_n(&initialized,1,__ATOMIC_RELEASE);
    XK_LOG("[object-jobs] EXPERIMENT: whole object callbacks, workers core 0/1 plus owner; shared game state and reordered updates are unproven; private stacks, joined batches, guarded HLE\n");
    return 1;
fail:
#ifdef __vita__
    for(unsigned i=0;i<WORKERS;i++) {
        if(threads[i]>=0)sceKernelDeleteThread(threads[i]);
        if(wakes[i]>=0)sceKernelDeleteSema(wakes[i]);
        if(dones[i]>=0)sceKernelDeleteSema(dones[i]);
    }
    if(math_mutex>=0)sceKernelDeleteMutex(math_mutex);
#endif
    for(unsigned i=0;i<LANES;i++)if(stacks[i]) { xk_mem_free(stacks[i]);stacks[i]=0; }
    XK_LOG("[object-jobs] unavailable: worker or private-stack allocation failed; serial callbacks retained\n");
    return 0;
}
int xv_object_jobs_begin(xctx *c)
{
    static int configured=-1;
    if(configured<0) { const char *e=getenv("XV_EXPERIMENTAL_OBJECT_JOBS");configured=e&&atoi(e)!=0; }
    if(!(override<0?configured:override)||owner||xv_phase_enabled||xv_is_object_job(c))return 0;
    if(!initialize())return 0;
    owner=c;return 1;
}
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
    for(unsigned i=0;i<WORKERS;i++) {
#ifdef __vita__
        if(sceKernelSignalSema(wakes[i],1)<0)abort();
#else
        sem_post(&wakes[i]);
#endif
    }
    execute(2);
    for(unsigned i=0;i<WORKERS;i++) {
#ifdef __vita__
        if(sceKernelWaitSema(dones[i],1,NULL)<0)abort();
#else
        wait_sem(&dones[i]);
#endif
    }
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
    batches=submitted=rejected=0;memset(executed,0,sizeof executed);memset(work_us,0,sizeof work_us);batch_us=0;
}
void xv_object_job_stop(xctx *c,unsigned address,const char *reason)
{
    XK_LOG("[object-jobs] STOP %s target %08X return %08X object %08X; experiment cannot enter owner-only services\n",
        reason,address,X_M32(c->r[4]),c->r[1]);
    abort();
}
void xv_object_job_hle(xctx *c,unsigned address,xv_fn_t fn)
{
    /* Stateless memory comparison only. Kernel, file, sound and D3D mutations
     * need a separate ownership protocol before they may run from these jobs. */
    if(address!=0x1D675Cu)xv_object_job_stop(c,address,"unsupported HLE");
    fn(c);
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
#else
        sem_post(&wakes[i]);pthread_join(threads[i],NULL);sem_destroy(&wakes[i]);sem_destroy(&dones[i]);
#endif
    }
#ifdef __vita__
    sceKernelDeleteMutex(math_mutex);
#else
    pthread_mutex_destroy(&math_mutex);
#endif
    for(unsigned i=0;i<LANES;i++)xk_mem_free(stacks[i]);
    initialized=-1;
}
#endif
