/* Production upload allocator + real worker, with pthread-backed Vita calls. */
#define XV_VERTEX_UPLOAD_BYTES (512u*1024u)
#define main vertex_baseline_main
#include "vertex_upload_test.c"
#undef main
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "../../runtime/xv_upload_worker.c"

static sem_t wake_sem;
static pthread_t native_thread;
static SceKernelThreadEntry entry;
static int running, sem_live, pause_worker, fail_signal, worker_parked;
static unsigned init_step, init_failure, barriers;
static pthread_mutex_t done_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t done_cond=PTHREAD_COND_INITIALIZER;
static int done_live,done_bits,fail_done_create,fail_done_signal,fail_done_wait;
SceUID sceKernelCreateEventFlag(const char *s,int attr,int bits,SceKernelEventFlagOptParam *o)
{
    assert(!strcmp(s,"xv_snapshot_done") && !attr && !bits && !o && !done_live);
    if(fail_done_create) {fail_done_create=0;return -1;}
    done_live=1;done_bits=0;return 3;
}
int sceKernelDeleteEventFlag(SceUID id)
{ assert(id==3 && done_live && !running);done_live=0;done_bits=0;return 0; }
int sceKernelSetEventFlag(SceUID id,unsigned bits)
{
    assert(id==3 && done_live && bits==1);
    if(__atomic_exchange_n(&fail_done_signal,0,__ATOMIC_RELAXED))return -1;
    pthread_mutex_lock(&done_lock);done_bits|=bits;
    pthread_cond_signal(&done_cond);pthread_mutex_unlock(&done_lock);return 0;
}
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout)
{
    assert(id==3 && done_live && bits==1 && mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT) && timeout && *timeout==1000);
    if(__atomic_exchange_n(&fail_done_wait,0,__ATOMIC_RELAXED))return -1;
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=*timeout*1000;
    if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
    int rc=0;pthread_mutex_lock(&done_lock);
    while(!done_bits && !rc)rc=pthread_cond_timedwait(&done_cond,&done_lock,&until);
    if(done_bits){*out=done_bits;done_bits=0;rc=0;}
    pthread_mutex_unlock(&done_lock);return rc?-1:0;
}
void xv_cpu_log_thread(const char *s) { assert(!strcmp(s,"vertex-upload")); }
void xv_gpu_write_barrier(void)
{ __atomic_thread_fence(__ATOMIC_SEQ_CST); __atomic_fetch_add(&barriers,1u,__ATOMIC_RELAXED); }
SceUID sceKernelCreateSema(const char *s,SceUInt a,int initial,int maximum,SceKernelSemaOptParam *o)
{
    assert(!initial && maximum==UPLOAD_JOBS);
    if (++init_step==init_failure) return -1;
    assert(!sem_live && !sem_init(&wake_sem,0,0)); sem_live=1; return 1;
}
int sceKernelDeleteSema(SceUID id)
{ assert(id==1 && sem_live && !sem_destroy(&wake_sem)); sem_live=0; return 0; }
int sceKernelSignalSema(SceUID id,int n)
{ assert(id==1 && n==1); if(fail_signal) {fail_signal=0;return -1;} return sem_post(&wake_sem); }
int sceKernelWaitSema(SceUID id,int n,SceUInt *timeout)
{
    assert(id==1 && n==1); int r=sem_wait(&wake_sem);
    __atomic_store_n(&worker_parked,1,__ATOMIC_RELEASE);
    while (__atomic_load_n(&pause_worker,__ATOMIC_ACQUIRE)) usleep(100);
    __atomic_store_n(&worker_parked,0,__ATOMIC_RELEASE);
    return r;
}
SceUID sceKernelCreateThread(const char *s,SceKernelThreadEntry e,int priority,SceSize bytes,SceUInt a,int mask,const SceKernelThreadOptParam *o)
{
    assert(priority==65 && bytes==32768 && mask==SCE_KERNEL_CPU_MASK_USER_0);
    if (++init_step==init_failure) return -1;
    entry=e; return 2;
}
static void *worker_run(void *p) { entry(0,NULL); return NULL; }
int sceKernelStartThread(SceUID id,SceSize bytes,void *p)
{
    assert(id==2 && !bytes && !p);
    if (++init_step==init_failure) return -1;
    assert(!pthread_create(&native_thread,NULL,worker_run,NULL)); running=1; return 0;
}
int sceKernelWaitThreadEnd(SceUID id,int *status,SceUInt *timeout)
{ assert(id==2 && running && !pthread_join(native_thread,NULL)); running=0; return 0; }
int sceKernelDeleteThread(SceUID id) { assert(id==2 && !running); return 0; }
int sceKernelGetThreadCurrentPriority(void) { return 64; }
int sceKernelDelayThread(SceUInt us) { usleep(us); return 0; }
SceUInt64 sceKernelGetProcessTimeWide(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
static void reset_worker(void)
{
    xv_vertex_upload_shutdown(); assert(!running && !sem_live && !live && !done_live);
    next_id=1; init_step=init_failure=0; fail_signal=0;
}
static void queue_guards(void)
{
    unsigned char src[128], dst[UPLOAD_JOBS+1][128]; uint32_t ticket=123;
    memset(src,0x6d,sizeof src); memset(dst,0,sizeof dst);
    for (unsigned fail=1;fail<=3;fail++) {
        init_failure=fail;
        assert(!xv_upload_worker_submit(dst,src,sizeof src,&ticket) && ticket==123);
        reset_worker();
    }
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    fail_signal=1;
    assert(!xv_upload_worker_submit(dst,src,sizeof src,&ticket) && ticket==123);
    for (unsigned i=0;i<UPLOAD_JOBS;i++)
        assert(xv_upload_worker_submit(dst[i],src,sizeof src,&ticket));
    uint32_t previous=ticket;
    assert(!xv_upload_worker_submit(dst[UPLOAD_JOBS],src,sizeof src,&ticket) && ticket==previous);
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);
    xv_upload_worker_wait(ticket);
    for (unsigned i=0;i<UPLOAD_JOBS;i++) assert(!memcmp(src,dst[i],sizeof src));
    assert(dst[UPLOAD_JOBS][0]==0 && barriers>=UPLOAD_JOBS);
    reset_worker();
    /* Ticket zero is valid after wrap. */
    submitted=completed=UINT32_MAX-1;
    for(unsigned i=0;i<3;i++) assert(xv_upload_worker_submit(dst[i],src,sizeof src,&ticket));
    assert(ticket==1);xv_upload_worker_wait(ticket);reset_worker();
}
static int consumer_entered, consumer_done;
static void *consume(void *p)
{
    __atomic_store_n(&consumer_entered,1,__ATOMIC_RELEASE);
    xv_vertex_upload_wait(0);
    __atomic_store_n(&consumer_done,1,__ATOMIC_RELEASE);return NULL;
}
static void delayed_slots(void)
{
    unsigned char src[70001];memset(src,0x19,sizeof src);
    const unsigned char *gpu[3];
    xv_vertex_worker_override(1);
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    for (unsigned s=0;s<3;s++) {
        xv_vertex_upload_reset(s);
        src[0]=(unsigned char)(s+1);
        gpu[s]=xv_vertex_upload(s,src,sizeof src); assert(gpu[s]);
        src[0]=99; /* mutate guest source immediately while its GPU copy is paused */
        const unsigned char *mirror=xv_vertex_upload_readback(s,gpu[s]);
        assert(mirror!=gpu[s] && mirror[0]==s+1 && mirror[70000]==0x19);
        assert(xv_vertex_upload_readback(s,src)==src);
        xv_vertex_upload_seal(s);
    }
    pthread_t consumer;assert(!pthread_create(&consumer,NULL,consume,NULL));
    while (!__atomic_load_n(&consumer_entered,__ATOMIC_ACQUIRE)) usleep(100);
    usleep(1000);assert(!__atomic_load_n(&consumer_done,__ATOMIC_ACQUIRE));
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);
    assert(!pthread_join(consumer,NULL));
    for(unsigned s=0;s<3;s++) {
        xv_vertex_upload_wait(s);
        assert(gpu[s][0]==s+1 && gpu[s][70000]==0x19);
        assert(!memcmp(pools[s].cpu,pools[s].gpu,pools[s].valid_bytes));
    }
    reset_worker();
}
static void generations(void)
{
    unsigned char source[8193], expected[3][16][8193];
    const void *gpu[3][16]={{0}};unsigned lengths[3][16]={{0}}, counts[3]={0};
    uint32_t rng=0x1173925;
    for(unsigned frame=0;frame<300;frame++) {
        unsigned slot=frame%3;counts[slot]=0;
        xv_vertex_worker_override(frame%4!=0);xv_vertex_upload_reset(slot);
        xv_vertex_upload_override(frame%2);xv_vertex_copy_override(frame%3==0);
        for(unsigned draw=0;draw<16;draw++) {
            rng=rng*1664525u+1013904223u;unsigned bytes=1+(rng%8193);
            memset(source,(unsigned char)(rng>>24),bytes);
            memcpy(expected[slot][draw],source,bytes);lengths[slot][draw]=bytes;
            gpu[slot][draw]=xv_vertex_upload(slot,source,bytes);assert(gpu[slot][draw]);
            assert(!memcmp(xv_vertex_upload_readback(slot,gpu[slot][draw]),source,bytes));
            assert(xv_vertex_upload(slot,source,bytes)==gpu[slot][draw]);
            memset(source,0xee,bytes);counts[slot]++;
            if(draw==7) xv_vertex_worker_override(frame%4==0); /* generation policy stays latched */
        }
        xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot);
        for(unsigned s=0;s<3;s++)for(unsigned d=0;d<counts[s];d++)
            assert(!memcmp(gpu[s][d],expected[s][d],lengths[s][d]));
        assert(!memcmp(pools[slot].cpu,pools[slot].gpu,pools[slot].valid_bytes));
    }
    /* Reset joins unsealed queued work; shutdown flushes the final short tail. */
    xv_vertex_worker_override(1);xv_vertex_upload_reset(0);
    unsigned char large[70001];memset(large,0x39,sizeof large);
    assert(xv_vertex_upload(0,large,sizeof large));xv_vertex_upload_reset(0);
    assert(xv_vertex_upload(0,large,8193));reset_worker();
}
static struct { unsigned char *dst, *src; unsigned bytes; int done, accepted; } loan;
static void *borrow_source(void *unused)
{
    loan.accepted=xv_upload_worker_snapshot(loan.dst,loan.src,loan.bytes);
    __atomic_store_n(&loan.done,1,__ATOMIC_RELEASE);return NULL;
}
static void snapshot_loans(void)
{
    const unsigned n=150001;
    unsigned char *a=malloc(n+128), *b=malloc(n+128);assert(a && b);
    memset(a,0x42,n+128);memset(b,0x91,n+128);
    assert(!xv_upload_worker_snapshot(b,a,131071) && thread<0 && b[0]==0x91);
    fail_done_create=1;assert(!xv_upload_worker_snapshot(b,a,n));
    assert(!xv_upload_worker_snapshot(b,a,n) && thread<0);
    for(unsigned j=0;j<n;j++)assert(b[j]==0x91);
    reset_worker();
    for(unsigned fail=1;fail<=3;fail++) {
        init_failure=fail;assert(!xv_upload_worker_snapshot(b,a,n));
        for(unsigned j=0;j<n;j++)assert(b[j]==0x91);
        reset_worker();
    }
    fail_signal=1;assert(!xv_upload_worker_snapshot(b,a,n));
    for(unsigned j=0;j<n;j++)assert(b[j]==0x91);
    reset_worker();
    /* A queued ordinary GPU copy makes the loan decline without writing. */
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    unsigned char small[64]={0}, dest[64]={0};uint32_t ticket;
    assert(xv_upload_worker_submit(dest,small,sizeof small,&ticket));
    assert(!xv_upload_worker_snapshot(b,a,n));
    for(unsigned j=0;j<n;j++)assert(b[j]==0x91);
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);
    xv_upload_worker_wait(ticket);reset_worker();
    /* The caller cannot return its source loan while the consumer is delayed. */
    for(unsigned fault=0;fault<4;fault++) {
    memset(a,0x42,n+128);
    if(fault==3) {
        assert(xv_upload_worker_snapshot(b,a,n));
        assert(!sceKernelSetEventFlag(snapshot_done,1)); /* deliberately stale */
    }
    __atomic_store_n(&fail_done_signal,fault==1,__ATOMIC_RELAXED);
    __atomic_store_n(&fail_done_wait,fault==2,__ATOMIC_RELAXED);
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    loan.dst=b+17;loan.src=a+3;loan.bytes=n;loan.done=0;
    pthread_t caller;assert(!pthread_create(&caller,NULL,borrow_source,NULL));
    for(unsigned i=0;i<10000 && !__atomic_load_n(&worker_parked,__ATOMIC_ACQUIRE);i++)usleep(100);
    assert(__atomic_load_n(&worker_parked,__ATOMIC_ACQUIRE));
    if(fault==2) {
        for(unsigned i=0;i<10000 && __atomic_load_n(&fail_done_wait,__ATOMIC_RELAXED);i++)usleep(100);
        assert(!__atomic_load_n(&fail_done_wait,__ATOMIC_RELAXED));
    }
    assert(!__atomic_load_n(&loan.done,__ATOMIC_ACQUIRE));
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);
    assert(!pthread_join(caller,NULL) && loan.accepted);
    assert(completed==submitted && !memcmp(b+17,a+3,n));
    memset(a,0x37,n+128);
    for(unsigned j=0;j<n;j++)assert(b[17+j]==0x42);
    reset_worker();
    }
    __atomic_store_n(&fail_done_signal,0,__ATOMIC_RELAXED);
    __atomic_store_n(&fail_done_wait,0,__ATOMIC_RELAXED);
    /* Every destination alignment, odd sizes and untouched surrounding bytes. */
    for(unsigned off=0;off<64;off++) {
        memset(b,0x91,n+128);
        assert(xv_upload_worker_snapshot(b+off,a+(off*7%64),n));
        for(unsigned j=0;j<n+128;j++)assert(b[j]==(j>=off && j<off+n?0x37:0x91));
        assert(completed==submitted);
    }
    reset_worker();
    /* Real uploader: both generations remain exact after immediate guest writes. */
    xv_snapshot_worker_override(1);xv_vertex_worker_override(1);xv_vertex_upload_override(0);
    const unsigned char *old[3]={0};unsigned char values[3]={0};
    for(unsigned frame=0;frame<81;frame++) {
        unsigned slot=frame%3; xv_vertex_upload_reset(slot);
        values[slot]=(unsigned char)frame;memset(a,values[slot],n);
        old[slot]=xv_vertex_upload(slot,a,n);assert(old[slot]);
        memset(a,0xde,n);xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot);
        for(unsigned s=0;s<3;s++)if(old[s])
            for(unsigned j=0;j<n;j++)assert(old[s][j]==values[s]);
        assert(!memcmp(pools[slot].cpu,pools[slot].gpu,pools[slot].valid_bytes));
    }
    assert(snapshot_jobs>0);xv_snapshot_worker_override(-1);reset_worker();free(a);free(b);
    puts("PASS: synchronous snapshot loans, event creation/signal/wait faults, busy/start/signal fallback, delayed join, 64 alignments, bounds and 81 retained slot generations");
}
#ifndef XV_UPLOAD_WORKER_TEST_NO_MAIN
int main(void)
{
    assert(!xv_vertex_worker_enabled());queue_guards();delayed_slots();generations();snapshot_loans();
    puts("PASS: real upload worker, 3 startup failures, failed dispatch, full queue, delayed consumer, ticket wrap, source mutation, 300 mixed slot generations, reset and shutdown");
    return 0;
}
#endif
