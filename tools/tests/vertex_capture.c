/* Production capture FIFO, uploader and copy worker under pthread Vita services.
 * Expected draws are retained independently from the guest source/masks. */
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#define XV_VERTEX_UPLOAD_BYTES (512u*1024u)
#define XV_VERTEX_CAPTURE_BYTES (128u*1024u)
#include "../../runtime/xv_vertex_upload.c"
#include "../../runtime/xv_vertex_capture.c"
#include "../../runtime/xv_upload_worker.c"

static struct { void *base;int mapped; } mem[32];
static pthread_mutex_t mem_lock=PTHREAD_MUTEX_INITIALIZER;
static struct { pthread_mutex_t lock;pthread_cond_t cond;unsigned bits;int live; } events[8];
static struct { pthread_t id;SceKernelThreadEntry entry;int live,started,joined; } threads[8];
static sem_t semaphore;
static int sem_live,pause_capture,pause_copy,parked_capture,parked_copy,fail_wake,fail_done;
static int fail_capture_alloc,fail_gpu_alloc,fail_create_at,create_calls;
static pthread_t owner;

void xv_logf(const char *fmt,...) { (void)fmt; }
void xv_cpu_log_thread(const char *name) { assert(!strcmp(name,"vertex-capture") || !strcmp(name,"vertex-upload")); }
void xv_gpu_flush(const void *p,uint32_t n) { assert(p && n); }
void xv_gpu_write_barrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
uint64_t sceKernelGetProcessTimeWide(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
int sceKernelDelayThread(SceUInt n) { usleep(n);return 0; }
int sceKernelGetThreadCurrentPriority(void) { return 64; }
SceUID sceKernelAllocMemBlock(const char *name,SceKernelMemBlockType type,SceSize size,SceKernelAllocMemBlockOpt *opt)
{
    (void)opt;
    if(!strcmp(name,"xv_vertex_capture") && fail_capture_alloc) { fail_capture_alloc=0;return -1; }
    if(!strcmp(name,"xv_vertices_gpu") && __atomic_exchange_n(&fail_gpu_alloc,0,__ATOMIC_RELAXED))return -1;
    assert(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW || type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE);
    pthread_mutex_lock(&mem_lock);unsigned i;
    for(i=1;i<32 && mem[i].base;i++);assert(i<32);
    mem[i].base=aligned_alloc(64,(size+63u)&~63u);assert(mem[i].base);
    memset(mem[i].base,0xa5,size);pthread_mutex_unlock(&mem_lock);return i;
}
int sceKernelGetMemBlockBase(SceUID id,void **p) { assert(id>0 && id<32);*p=mem[id].base;return 0; }
int sceKernelFreeMemBlock(SceUID id)
{ pthread_mutex_lock(&mem_lock);assert(mem[id].base && !mem[id].mapped);free(mem[id].base);mem[id].base=NULL;pthread_mutex_unlock(&mem_lock);return 0; }
int sceGxmMapMemory(void *p,SceSize n,SceGxmMemoryAttribFlags flags)
{
    assert(n==XV_VERTEX_UPLOAD_BYTES && flags==SCE_GXM_MEMORY_ATTRIB_READ);
    pthread_mutex_lock(&mem_lock);unsigned i;for(i=1;i<32 && mem[i].base!=p;i++);
    assert(i<32);mem[i].mapped=1;pthread_mutex_unlock(&mem_lock);return 0;
}
int sceGxmUnmapMemory(void *p)
{ unsigned i;for(i=1;i<32 && mem[i].base!=p;i++);assert(i<32 && mem[i].mapped);mem[i].mapped=0;return 0; }
SceUID sceKernelCreateEventFlag(const char *name,int attr,int bits,SceKernelEventFlagOptParam *opts)
{
    (void)name;(void)opts;assert(!attr && !bits);
    if(++create_calls==fail_create_at)return -1;
    unsigned i;for(i=0;i<8 && events[i].live;i++);assert(i<8);
    pthread_mutex_init(&events[i].lock,NULL);pthread_cond_init(&events[i].cond,NULL);
    events[i].bits=0;events[i].live=1;return 100+i;
}
int sceKernelDeleteEventFlag(SceUID id)
{ unsigned i=id-100;assert(i<8 && events[i].live);pthread_mutex_destroy(&events[i].lock);pthread_cond_destroy(&events[i].cond);events[i].live=0;return 0; }
int sceKernelSetEventFlag(SceUID id,unsigned bits)
{
    if(id==cap_wake && __atomic_exchange_n(&fail_wake,0,__ATOMIC_RELAXED))return -1;
    if(id==cap_done && __atomic_exchange_n(&fail_done,0,__ATOMIC_RELAXED))return -1;
    unsigned i=id-100;assert(i<8 && events[i].live);
    pthread_mutex_lock(&events[i].lock);events[i].bits|=bits;
    pthread_cond_signal(&events[i].cond);pthread_mutex_unlock(&events[i].lock);return 0;
}
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout)
{
    unsigned i=id-100;assert(i<8 && events[i].live && timeout);
    assert(mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT));
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=(long)*timeout*1000;
    if(t.tv_nsec>=1000000000){t.tv_sec+=t.tv_nsec/1000000000;t.tv_nsec%=1000000000;}
    int rc=0;pthread_mutex_lock(&events[i].lock);
    while(!(events[i].bits&bits) && !rc)rc=pthread_cond_timedwait(&events[i].cond,&events[i].lock,&t);
    if(events[i].bits&bits){*out=events[i].bits;events[i].bits&=~bits;rc=0;}
    pthread_mutex_unlock(&events[i].lock);
    if(id==cap_wake) {
        __atomic_store_n(&parked_capture,1,__ATOMIC_RELEASE);
        while(__atomic_load_n(&pause_capture,__ATOMIC_ACQUIRE))usleep(100);
        __atomic_store_n(&parked_capture,0,__ATOMIC_RELEASE);
    }
    return rc?-1:0;
}
SceUID sceKernelCreateThread(const char *name,SceKernelThreadEntry fn,int priority,SceSize stack,SceUInt attrs,int mask,const SceKernelThreadOptParam *opts)
{
    (void)name;(void)attrs;(void)opts;assert(priority==65 && stack==32768 && mask==SCE_KERNEL_CPU_MASK_USER_0);
    if(++create_calls==fail_create_at)return -1;
    unsigned i;for(i=0;i<8 && threads[i].live;i++);assert(i<8);
    threads[i].entry=fn;threads[i].live=1;threads[i].started=threads[i].joined=0;return 200+i;
}
static void *host_run(void *arg) { unsigned i=(uintptr_t)arg;threads[i].entry(0,NULL);return NULL; }
int sceKernelStartThread(SceUID id,SceSize n,void *p)
{
    assert(!n && !p);if(++create_calls==fail_create_at)return -1;
    unsigned i=id-200;assert(i<8 && threads[i].live);
    assert(!pthread_create(&threads[i].id,NULL,host_run,(void *)(uintptr_t)i));threads[i].started=1;return 0;
}
int sceKernelWaitThreadEnd(SceUID id,int *status,SceUInt *timeout)
{ (void)status;(void)timeout;unsigned i=id-200;assert(threads[i].started && !threads[i].joined);assert(!pthread_join(threads[i].id,NULL));threads[i].joined=1;return 0; }
int sceKernelDeleteThread(SceUID id)
{ unsigned i=id-200;assert(threads[i].live && (!threads[i].started || threads[i].joined));threads[i].live=0;return 0; }
SceUID sceKernelCreateSema(const char *name,SceUInt attr,int initial,int maximum,SceKernelSemaOptParam *opts)
{ (void)name;(void)attr;(void)opts;assert(!sem_live && !initial && maximum==UPLOAD_JOBS);assert(!sem_init(&semaphore,0,0));sem_live=1;return 300; }
int sceKernelDeleteSema(SceUID id) { assert(id==300 && sem_live);assert(!sem_destroy(&semaphore));sem_live=0;return 0; }
int sceKernelSignalSema(SceUID id,int n) { assert(id==300 && n==1);return sem_post(&semaphore); }
int sceKernelWaitSema(SceUID id,int n,SceUInt *timeout)
{
    (void)timeout;assert(id==300 && n==1);int rc=sem_wait(&semaphore);
    __atomic_store_n(&parked_copy,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&pause_copy,__ATOMIC_ACQUIRE))usleep(100);
    __atomic_store_n(&parked_copy,0,__ATOMIC_RELEASE);return rc;
}

typedef struct { const unsigned char *result[2];unsigned callbacks;int ok; } output;
static void collected(void *context,int ok)
{ assert(pthread_equal(owner,pthread_self()));output *o=context;o->callbacks++;o->ok=ok; }
static int capture(unsigned slot,unsigned char *source,unsigned bytes,unsigned stride,const xv_vertex_refs *refs,unsigned packed,output *out)
{
    xv_vertex_prepare_batch b={.slot=slot,.count=1};
    b.streams[0]=(xv_vertex_prepare_stream){.source=source,.bytes=bytes,.stride=stride,.refs=refs};
#if XV_PACKED_VERTEX_LAYOUT
    b.streams[0].packed=packed;
#else
    assert(!packed);
#endif
    const void **targets[]={(const void **)&out->result[0]};
    return xv_vertex_capture_submit(&b,targets,collected,out);
}
static void join(unsigned slot) { xv_vertex_capture_drain();xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot); }
static void cleanup(void)
{
    xv_vertex_capture_shutdown();xv_vertex_upload_shutdown();
    for(unsigned i=1;i<32;i++)assert(!mem[i].base);
    for(unsigned i=0;i<8;i++)assert(!events[i].live && !threads[i].live);
    assert(!sem_live);xv_vertex_worker_override(0);xv_vertex_upload_override(1);
}
static void wait_parked(int *flag)
{
    uint64_t end=sceKernelGetProcessTimeWide()+2000000;
    while(!__atomic_load_n(flag,__ATOMIC_ACQUIRE) && sceKernelGetProcessTimeWide()<end)usleep(100);
    assert(__atomic_load_n(flag,__ATOMIC_ACQUIRE));
}
static void private_inputs(void)
{
    unsigned char *guest=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);assert(guest!=MAP_FAILED);
    unsigned char first[4096],second[4096];memset(first,0x21,sizeof first);memset(second,0x43,sizeof second);
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    memcpy(guest,first,4096);output a={0},b={0},c={0};
    assert(capture(0,guest,4096,16,NULL,0,&a));wait_parked(&parked_capture);
    memcpy(guest,second,4096);assert(capture(0,guest,4096,16,NULL,0,&b));
    assert(capture(0,guest,4096,16,NULL,0,&c));
    assert(!a.result[0] && !a.callbacks);assert(!mprotect(guest,4096,PROT_NONE));
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(a.ok && b.ok && c.ok && a.callbacks==1 && b.callbacks==1 && c.callbacks==1);
    assert(a.result[0]!=b.result[0] && b.result[0]==c.result[0]);
    assert(!memcmp(a.result[0],first,4096) && !memcmp(b.result[0],second,4096));
    assert(!munmap(guest,4096));cleanup();
}
static void sparse_and_packed(void)
{
    unsigned char guest[32768],expected[32768];memset(guest,0x39,sizeof guest);memcpy(expected,guest,sizeof guest);
    xv_vertex_refs refs;xv_vertex_refs_clear(&refs);xv_vertex_refs_add(&refs,0);xv_vertex_refs_add(&refs,1023);
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);output a={0},b={0};
    assert(capture(0,guest,sizeof guest,32,&refs,0,&a));wait_parked(&parked_capture);
    memset(&refs,0,sizeof refs);memset(guest,0xee,sizeof guest);
#if XV_PACKED_VERTEX_LAYOUT
    assert(capture(0,guest,sizeof guest,32,NULL,XV_PACKED_PREFIX16,&b));
#endif
    memset(guest,0x80,sizeof guest);__atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(a.ok && !memcmp(a.result[0],expected,sizeof expected));
#if XV_PACKED_VERTEX_LAYOUT
    assert(b.ok && b.result[0]!=a.result[0]);for(unsigned i=0;i<16384;i++)assert(b.result[0][i]==0xee);
#else
    (void)b;
#endif
    cleanup();
}
static void pressure_and_wrap(void)
{
    unsigned char guest[8192];output outputs[300]={0};
    cap_submitted=cap_completed=cap_retired=UINT32_MAX-15u;
    unsigned before=cap_pressure;
    for(unsigned i=0;i<300;i++) {
        memset(guest,i,sizeof guest);assert(capture(1,guest,sizeof guest,16,NULL,0,&outputs[i]));
        if(i%48==47) { join(1);for(unsigned j=i-47;j<=i;j++) {
            assert(outputs[j].ok);for(unsigned k=0;k<sizeof guest;k++)assert(outputs[j].result[0][k]==(unsigned char)j);
        }xv_vertex_upload_reset(1); }
    }
    join(1);assert(cap_pressure>before && cap_submitted<300);
    for(unsigned i=288;i<300;i++)assert(outputs[i].ok && outputs[i].callbacks==1);
    cleanup();
}
static void failure_cases(void)
{
    unsigned char src[32]={0};output o={0};
    fail_capture_alloc=1;assert(!capture(0,src,sizeof src,4,NULL,0,&o));assert(!o.callbacks);cleanup();
    for(unsigned failure=1;failure<=4;failure++) {
        create_calls=0;fail_create_at=failure;assert(!capture(0,src,sizeof src,4,NULL,0,&o));cleanup();
    }
    fail_create_at=0;__atomic_store_n(&fail_gpu_alloc,1,__ATOMIC_RELAXED);
    assert(capture(0,src,sizeof src,4,NULL,0,&o));join(0);assert(o.callbacks==1 && !o.ok && !o.result[0]);
    output good={0};assert(capture(0,src,sizeof src,4,NULL,0,&good));join(0);assert(good.ok);cleanup();
    fail_wake=fail_done=1;o=(output){0};assert(capture(0,src,sizeof src,4,NULL,0,&o));join(0);assert(o.ok);cleanup();
}
static void *release_capture(void *unused)
{
    (void)unused;usleep(20000);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);return NULL;
}
static void queue_capacity(void)
{
    unsigned char src[32]={0};output outputs[33]={0};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    unsigned pressure=cap_pressure;
    for(unsigned i=0;i<32;i++) {
        src[0]=i;assert(capture(0,src,sizeof src,4,NULL,0,&outputs[i]));
    }
    assert(cap_submitted-cap_retired==32);wait_parked(&parked_capture);
    pthread_t releaser;assert(!pthread_create(&releaser,NULL,release_capture,NULL));
    src[0]=32;assert(capture(0,src,sizeof src,4,NULL,0,&outputs[32]));
    assert(!pthread_join(releaser,NULL));join(0);assert(cap_pressure==pressure+1);
    for(unsigned i=0;i<33;i++)assert(outputs[i].ok && outputs[i].callbacks==1 && outputs[i].result[0][0]==i);
    cleanup();
}
static void partial_failure_and_fallback(void)
{
    unsigned char src[32]={0};output o={0};
#if XV_PACKED_VERTEX_LAYOUT
    xv_vertex_prepare_batch b={.slot=0,.count=2};
    b.streams[0]=(xv_vertex_prepare_stream){.source=src,.bytes=32,.stride=4};
    b.streams[1]=(xv_vertex_prepare_stream){.source=src,.bytes=32,.stride=4};
    b.streams[1].packed=99; /* second stream fails after the first succeeded */
    const void **targets[]={(const void **)&o.result[0],(const void **)&o.result[1]};
    assert(xv_vertex_capture_submit(&b,targets,collected,&o));join(0);
    assert(o.callbacks==1 && !o.ok && !o.result[0] && !o.result[1]);cleanup();
#endif
    o=(output){0};assert(capture(0,src,sizeof src,4,NULL,0,&o));
    output rejected={0};
    assert(!capture(0,src,XV_VERTEX_CAPTURE_BYTES+1u,4,NULL,0,&rejected));
    assert(o.ok && o.callbacks==1 && !rejected.callbacks); /* fallback drained */
    cleanup();setenv("XV_VERTEX_CAPTURE","0",1);
    assert(!capture(0,src,sizeof src,4,NULL,0,&rejected));assert(cap_thread<0);
    cleanup();setenv("XV_VERTEX_CAPTURE","1",1);
}
static void gpu_copy_lifetime(void)
{
    static unsigned char src[65536];output slots[3]={0};
    xv_vertex_worker_override(1);__atomic_store_n(&pause_copy,1,__ATOMIC_RELEASE);
    for(unsigned slot=0;slot<3;slot++) {
        memset(src,slot+9,sizeof src);assert(capture(slot,src,sizeof src,32,NULL,0,&slots[slot]));
        xv_vertex_capture_drain();xv_vertex_upload_seal(slot);
    }
    wait_parked(&parked_copy);memset(src,0,sizeof src);
    __atomic_store_n(&pause_copy,0,__ATOMIC_RELEASE);
    for(unsigned slot=0;slot<3;slot++) {
        xv_vertex_upload_wait(slot);assert(slots[slot].ok);
        for(unsigned i=0;i<sizeof src;i++)assert(slots[slot].result[0][i]==slot+9);
    }
    cleanup();
}
int main(void)
{
    owner=pthread_self();setenv("XV_VERTEX_CAPTURE","1",1);
    xv_vertex_worker_override(0);xv_vertex_upload_override(1);
    private_inputs();sparse_and_packed();pressure_and_wrap();failure_cases();
    queue_capacity();partial_failure_and_fallback();gpu_copy_lifetime();
    puts("PASS: private inputs, rewritten aliases, mask ownership, packed/raw identity, arena/queue pressure, ticket wrap, partial/allocation/thread/notification failures, fallback drains, disable and three GPU-copy slots");
    return 0;
}
