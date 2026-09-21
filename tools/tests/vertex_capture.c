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
#define XV_VERTEX_PERSISTENT_BYTES (128u*1024u)
#define VP_ENTRIES 16u
#include "../../runtime/xv_vertex_upload.c"
#if XV_VERTEX_CAPTURE_NOTIFY
static void capture_notify_point(unsigned point);
#define XV_CAPTURE_NOTIFY_POINT(point) capture_notify_point(point)
#endif
#include "../../runtime/xv_vertex_capture.c"
#include "../../runtime/xv_upload_worker.c"

static struct { void *base;int mapped; } mem[32];
static pthread_mutex_t mem_lock=PTHREAD_MUTEX_INITIALIZER;
static struct { pthread_mutex_t lock;pthread_cond_t cond;unsigned bits;int live; } events[8];
static struct { pthread_t id;SceKernelThreadEntry entry;int live,started,joined; } threads[8];
static sem_t semaphore;
static int sem_live,pause_capture,pause_copy,parked_capture,parked_copy,fail_wake,fail_done;
static int fail_capture_alloc,fail_gpu_alloc,fail_create_at,create_calls;
static int fail_persistent;
static pthread_t owner;
#if XV_VERTEX_CAPTURE_NOTIFY
static unsigned notify_pause[8],notify_reached[8];
static unsigned wake_wait_calls,wake_timeouts,done_wait_calls,done_timeouts;
static void capture_notify_point(unsigned point)
{
    if(point==CAP_BEFORE_EXECUTE && __atomic_load_n(&pause_capture,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&parked_capture,1,__ATOMIC_RELEASE);
        while(__atomic_load_n(&pause_capture,__ATOMIC_ACQUIRE))usleep(100);
        __atomic_store_n(&parked_capture,0,__ATOMIC_RELEASE);
    }
    __atomic_fetch_add(&notify_reached[point],1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&notify_pause[point],__ATOMIC_ACQUIRE))usleep(100);
}
static void gate(unsigned point) {
    __atomic_store_n(&notify_reached[point],0,__ATOMIC_RELAXED);
    __atomic_store_n(&notify_pause[point],1,__ATOMIC_RELEASE);
}
static void ungate(unsigned point) { __atomic_store_n(&notify_pause[point],0,__ATOMIC_RELEASE); }
static void reached(unsigned point) {
    uint64_t end=sceKernelGetProcessTimeWide()+2000000;
    while(!__atomic_load_n(&notify_reached[point],__ATOMIC_ACQUIRE) && sceKernelGetProcessTimeWide()<end)usleep(100);
    assert(__atomic_load_n(&notify_reached[point],__ATOMIC_ACQUIRE));
}
#define READ_NOTIFY(v) __atomic_load_n(&(v),__ATOMIC_ACQUIRE)
#endif

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
    if((!strcmp(name,"xv_persistent_cpu") && fail_persistent==1) ||
       (!strcmp(name,"xv_persistent_gpu") && fail_persistent==2))return -1;
    assert(type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW || type==SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE);
    pthread_mutex_lock(&mem_lock);unsigned i;
    for(i=1;i<32 && mem[i].base;i++);assert(i<32);
    mem[i].base=aligned_alloc(4096,(size+4095u)&~4095u);assert(mem[i].base);
    memset(mem[i].base,0xa5,size);pthread_mutex_unlock(&mem_lock);return i;
}
int sceKernelGetMemBlockBase(SceUID id,void **p) { assert(id>0 && id<32);*p=mem[id].base;return 0; }
int sceKernelFreeMemBlock(SceUID id)
{ pthread_mutex_lock(&mem_lock);assert(mem[id].base && !mem[id].mapped);free(mem[id].base);mem[id].base=NULL;pthread_mutex_unlock(&mem_lock);return 0; }
int sceGxmMapMemory(void *p,SceSize n,SceGxmMemoryAttribFlags flags)
{
    assert((n==XV_VERTEX_UPLOAD_BYTES || n==XV_VERTEX_PERSISTENT_BYTES) && flags==SCE_GXM_MEMORY_ATTRIB_READ);
    if(n==XV_VERTEX_PERSISTENT_BYTES && fail_persistent==3)return -1;
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
#if XV_VERTEX_CAPTURE_NOTIFY
    if(id==cap_wake)__atomic_fetch_add(&wake_wait_calls,1,__ATOMIC_RELAXED);
    if(id==cap_done)__atomic_fetch_add(&done_wait_calls,1,__ATOMIC_RELAXED);
#endif
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
#if XV_VERTEX_CAPTURE_NOTIFY
    if(rc && id==cap_wake)__atomic_fetch_add(&wake_timeouts,1,__ATOMIC_RELAXED);
    if(rc && id==cap_done)__atomic_fetch_add(&done_timeouts,1,__ATOMIC_RELAXED);
#endif
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
#if XV_VERTEX_CAPTURE_NOTIFY
static output *ordered_outputs;
static unsigned ordered_at;
#endif
static void collected(void *context,int ok)
{
    assert(pthread_equal(owner,pthread_self()));output *o=context;
#if XV_VERTEX_CAPTURE_NOTIFY
    if(ordered_outputs)assert(o==&ordered_outputs[ordered_at++]);
#endif
    o->callbacks++;o->ok=ok;
}
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
static void detail_sampling(void)
{
    unsigned char guest[256];
    setenv("XV_VERTEX_CAPTURE_DETAIL","1",1);
    for(unsigned i=0;i<65;i++) {
        output o={0};memset(guest,i,sizeof guest);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o));join(0);
        assert(o.ok && o.callbacks==1 && !memcmp(o.result[0],guest,sizeof guest));
    }
    assert(cap_detail_samples==2 && cap_detail_copies==2 && cap_detail_publishes==2);
    assert(cap_detail_copy_bytes==2*sizeof guest);
    xv_vertex_capture_report(65);
    assert(!cap_detail_samples && !cap_detail_copies && !cap_detail_publishes &&
        !cap_detail_compares && !cap_detail_compare_bytes && !cap_detail_copy_bytes &&
        !cap_detail_compare_us && !cap_detail_copy_us && !cap_detail_publish_us);
    cleanup();
    setenv("XV_VERTEX_CAPTURE_DETAIL","0",1);
    output o={0};assert(capture(0,guest,sizeof guest,16,NULL,0,&o));join(0);
    assert(o.ok && !cap_detail_samples);cleanup();
    unsetenv("XV_VERTEX_CAPTURE_DETAIL");
    puts("PASS: sampled capture detail counts 1/64 submissions, resets reports, startup disable; outputs unchanged");
}
#if XV_VERTEX_CAPTURE_REUSE && XV_CAPTURE_TRUST_TAGS
uint8_t *g_xram;
static void trust_verification(void)
{
    /* Exercise production lookup decisions with timing off, on, and sparse.
     * A missing write notification must still be detected without profiling. */
    g_xram=calloc(1,TAG_PHYS_BASE+256);assert(g_xram);
    uint8_t snapshot[256];
    uint8_t *source=g_xram+TAG_PHYS_BASE;
    xv_vertex_prepare_stream stream={.source=source,.bytes=256,.stride=16};
    for(unsigned mode=0;mode<3;mode++) {
        cap_arena=snapshot;cap_used=0;cap_reuse_enabled=1;cap_reuse_reset();
        trust_enabled=1;trust_disabled=trust_verify_serial=0;
        trust_hits=trust_verified=trust_mismatches=0;
        memset(source,0x21,256);memcpy(snapshot,source,256);
        assert(cap_reuse_add(&stream,0,0,0)==1);
        for(unsigned i=0;i<128;i++) {
            int sample=mode==1 || (mode==2 && !(i&63));
            assert(cap_reuse_find(&stream,0,0,0,sample)==1);
            assert(trust_verified==1+i/64);
        }
        assert(trust_verified==2 && trust_hits==126);
        source[17]^=1; /* Lookup 129 independently verifies and disables trust. */
        assert(!cap_reuse_find(&stream,0,0,0,mode==1));
        assert(trust_disabled && trust_mismatches==1 && trust_verified==3);
        assert(!cap_reuse_find(&stream,0,0,0,0));
        memcpy(snapshot,source,256);
        assert(cap_reuse_find(&stream,0,0,0,0)==1);
        assert(trust_verified==3); /* Disabled trust always uses exact equality. */
        trust_disabled=0;
        xv_vertex_capture_tags_written(TAG_PHYS_BASE,256);
        source[19]^=1;
        assert(!cap_reuse_find(&stream,0,0,0,0)); /* Generation invalidation. */
        assert(trust_verified==3);
    }
    cap_arena=NULL;cap_reuse_reset();cap_reuse_enabled=-1;
    trust_enabled=-1;trust_disabled=trust_verify_serial=0;
    trust_hits=trust_verified=trust_mismatches=0;trust_bytes=0;
    cap_detail_compares=0;cap_detail_compare_bytes=cap_detail_compare_us=0;
    free(g_xram);g_xram=NULL;
    puts("PASS: trust verification independent of profiling; mismatch disables trust; write generation invalidates reuse");
}
#endif
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
static void unused_mask_lifetime(void)
{
    unsigned char guest[32768],expected[32768];
    xv_vertex_refs *refs=mmap(NULL,4096,PROT_READ|PROT_WRITE,
        MAP_ANONYMOUS|MAP_PRIVATE,-1,0);assert(refs!=MAP_FAILED);
    /* Dense coverage, short input, and mismatched-span metadata all require
     * full-span validation. Packed inputs also ignore a sparse mask. The
     * producer unmaps its mask and overwrites its vertices before execution. */
    for(unsigned mode=0;mode<5;mode++) {
#if !XV_PACKED_VERTEX_LAYOUT
        if(mode==4)continue;
#endif
        assert(!mprotect(refs,4096,PROT_READ|PROT_WRITE));
        xv_vertex_refs_clear(refs);
        if(mode==0)for(unsigned i=0;i<1024;i+=8)xv_vertex_refs_add(refs,i);
        xv_vertex_refs_add(refs,0);xv_vertex_refs_add(refs,mode==1?127:1023);
        unsigned bytes=mode==1?4096:mode==2?16384:32768;
        unsigned packed=mode==4?1:0;
        /* mode 3 is truly sparse: it must still receive a private mask. */
        for(unsigned i=0;i<sizeof guest;i++)guest[i]=(unsigned char)(i*13+mode);
        memcpy(expected,guest,bytes);output a={0};
        __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
        assert(capture(0,guest,bytes,32,refs,packed,&a));wait_parked(&parked_capture);
        const xv_vertex_refs *owned=cap_jobs[0].batch.streams[0].refs;
        if(mode==3)assert(owned && owned!=refs && !memcmp(owned,refs,sizeof *refs));
        else assert(!owned);
        assert(!mprotect(refs,4096,PROT_NONE));memset(guest,0xef,bytes);
        __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
        assert(a.ok && a.callbacks==1);
        if(packed)for(unsigned i=0;i<bytes/32;i++)
            assert(!memcmp(a.result[0]+i*16,expected+i*32,16));
        else assert(!memcmp(a.result[0],expected,bytes));
        cleanup();
    }
    assert(!munmap(refs,4096));
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
    static unsigned char src[65536];output slots[3]={0},duplicates[3]={0};
    xv_vertex_worker_override(1);__atomic_store_n(&pause_copy,1,__ATOMIC_RELEASE);
    for(unsigned slot=0;slot<3;slot++) {
        memset(src,slot+9,sizeof src);assert(capture(slot,src,sizeof src,32,NULL,0,&slots[slot]));
        assert(capture(slot,src,sizeof src,32,NULL,0,&duplicates[slot]));
        xv_vertex_capture_drain();xv_vertex_upload_seal(slot);
    }
    wait_parked(&parked_copy);memset(src,0,sizeof src);
    __atomic_store_n(&pause_copy,0,__ATOMIC_RELEASE);
    for(unsigned slot=0;slot<3;slot++) {
        xv_vertex_upload_wait(slot);assert(slots[slot].ok);
        assert(duplicates[slot].ok && duplicates[slot].result[0]==slots[slot].result[0]);
        for(unsigned i=0;i<sizeof src;i++)assert(slots[slot].result[0][i]==slot+9);
    }
    cleanup();
}
#if XV_VERTEX_CAPTURE_PACKED
static void compact_versions(void)
{
    enum { N=129, RAW=N*32, COMPACT=N*16 };
    unsigned char *mapping=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    assert(mapping!=MAP_FAILED);unsigned char *guest=mapping+1;
    unsigned char expected[COMPACT],changed[COMPACT];
    for(unsigned i=0;i<RAW;i++)guest[i]=(unsigned char)(i*7+i/32);
    for(unsigned v=0;v<N;v++)memcpy(expected+v*16,guest+v*32,16);
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);output a={0},b={0},c={0},shorter={0};
    unsigned used=cap_used;uint64_t saved=cap_compact_saved;
    assert(capture(0,guest,RAW,32,NULL,XV_PACKED_PREFIX16,&a));wait_parked(&parked_capture);
    assert(cap_used-used==COMPACT && cap_compact_saved-saved==COMPACT);
    assert(!memcmp(cap_arena+used,expected,COMPACT));
    for(unsigned v=0;v<N;v++)memset(guest+v*32+16,0xda,16);
    assert(capture(0,guest,RAW,32,NULL,XV_PACKED_PREFIX16,&b));
    guest[(N-1)*32+15]^=1;memcpy(changed,expected,COMPACT);changed[COMPACT-1]^=1;
    assert(capture(0,guest,RAW,32,NULL,XV_PACKED_PREFIX16,&c));
    assert(capture(0,guest,32*7,32,NULL,XV_PACKED_PREFIX16,&shorter));
    assert(!mprotect(mapping,8192,PROT_NONE));
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(a.ok&&b.ok&&c.ok&&shorter.ok&&a.result[0]==b.result[0]&&a.result[0]!=c.result[0]);
    assert(shorter.result[0]==c.result[0]);
    assert(!memcmp(a.result[0],expected,COMPACT)&&!memcmp(c.result[0],changed,COMPACT));
    assert(!mprotect(mapping,8192,PROT_READ|PROT_WRITE));
    /* Both capture encodings must share the same GPU cache representation. */
    const void *legacy=xv_vertex_upload_packed(0,guest,N);assert(legacy==c.result[0]);
    const void *raw=xv_vertex_upload(0,guest,RAW);assert(raw&&raw!=legacy&&!memcmp(raw,guest,RAW));
    assert(!xv_vertex_upload_compact_snapshot(0,guest,changed,0));
    assert(!xv_vertex_upload_compact_snapshot(0,guest,changed,17));
    assert(!xv_vertex_upload_compact_snapshot(3,guest,changed,COMPACT));
    assert(!xv_vertex_upload_compact_snapshot(0,guest,NULL,COMPACT));
    assert(!munmap(mapping,8192));cleanup();
}
static void compact_pressure_and_retirement(void)
{
    unsigned char *guest=malloc(XV_VERTEX_CAPTURE_BYTES);assert(guest);
    memset(guest,0x53,XV_VERTEX_CAPTURE_BYTES);output a={0},b={0};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);unsigned pressure=cap_pressure;
    assert(capture(0,guest,XV_VERTEX_CAPTURE_BYTES,32,NULL,XV_PACKED_PREFIX16,&a));wait_parked(&parked_capture);
    assert(capture(0,guest,XV_VERTEX_CAPTURE_BYTES,32,NULL,XV_PACKED_PREFIX16,&b));
    assert(cap_used==XV_VERTEX_CAPTURE_BYTES/(XV_VERTEX_CAPTURE_REUSE?2:1) && cap_pressure==pressure);
    memset(guest,0xa9,XV_VERTEX_CAPTURE_BYTES);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(a.ok&&b.ok&&a.result[0]==b.result[0]);
    for(unsigned i=0;i<XV_VERTEX_CAPTURE_BYTES/2;i++)assert(a.result[0][i]==0x53);
    cleanup();free(guest);
    unsigned char source[8192],wanted[4096];output slots[3]={0};
    xv_vertex_worker_override(1);
    for(unsigned generation=0;generation<9;generation++) {
        unsigned slot=generation%3;xv_vertex_upload_reset(slot);
        for(unsigned i=0;i<sizeof(source);i++)source[i]=(unsigned char)(i+generation);
        for(unsigned v=0;v<256;v++)memcpy(wanted+v*16,source+v*32,16);
        slots[slot]=(output){0};assert(capture(slot,source,sizeof(source),32,NULL,XV_PACKED_PREFIX16,&slots[slot]));
        memset(source,0,sizeof(source));join(slot);
        assert(slots[slot].ok&&!memcmp(slots[slot].result[0],wanted,sizeof(wanted)));
    }
    cleanup();
    setenv("XV_VERTEX_CAPTURE_PACKED","0",1);output off={0};
    assert(capture(0,source,sizeof(source),32,NULL,XV_PACKED_PREFIX16,&off));
    assert(cap_used==sizeof(source));join(0);assert(off.ok);cleanup();
    unsetenv("XV_VERTEX_CAPTURE_PACKED");
}
#endif
#if XV_VERTEX_CAPTURE_REUSE
static void capture_reuse_versions(void)
{
    unsigned char *guest=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    assert(guest!=MAP_FAILED);memset(guest,0x13,4096);
    output a={0},b={0},changed={0},restored={0},other_slot={0},other_stride={0};
    unsigned hits=cap_reuse_hits,prepared=cap_reuse_prepared;
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,guest,512,16,NULL,0,&a));wait_parked(&parked_capture);
    assert(capture(0,guest,512,16,NULL,0,&b));assert(cap_used==512);
    guest[511]^=1;assert(capture(0,guest,512,16,NULL,0,&changed));
    guest[511]^=1;assert(capture(0,guest,512,16,NULL,0,&restored));
    /* A returned source version is deliberately not searched beyond newest. */
    assert(cap_used==1536 && cap_reuse_hits==hits+1);
    assert(capture(1,guest,512,16,NULL,0,&other_slot));
    assert(capture(0,guest,512,8,NULL,0,&other_stride));assert(cap_used==2560);
    assert(!mprotect(guest,4096,PROT_NONE));
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);join(1);
    assert(a.ok&&b.ok&&changed.ok&&restored.ok&&other_slot.ok&&other_stride.ok);
    assert(a.result[0]==b.result[0] && a.result[0]!=changed.result[0] && a.result[0]==restored.result[0]);
    assert(a.result[0]!=other_slot.result[0] && other_slot.result[0][511]==0x13);
    assert(a.result[0][511]==0x13 && changed.result[0][511]==0x12);
    assert(cap_reuse_prepared==prepared+1 && cap_entry_count==5);
    for(unsigned i=0;i<cap_entry_count;i++)assert(!cap_results[i]);
    assert(!munmap(guest,4096));cleanup();
}
static void capture_reuse_sparse(void)
{
    unsigned char guest[32768];memset(guest,0x33,sizeof guest);
    xv_vertex_refs refs;xv_vertex_refs_clear(&refs);xv_vertex_refs_add(&refs,0);xv_vertex_refs_add(&refs,1023);
    output a={0},b={0},c={0},d={0};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,guest,sizeof guest,32,&refs,0,&a));wait_parked(&parked_capture);
    assert(capture(0,guest,sizeof guest,32,&refs,0,&b));assert(cap_used==65536 && !cap_entry_count);
    memset(guest+512*32,0x91,32);xv_vertex_refs_add(&refs,512);
    assert(capture(0,guest,sizeof guest,32,&refs,0,&c));
    /* Full validation after sparse reuse must not reuse a stale hole. */
    assert(capture(0,guest,sizeof guest,32,NULL,0,&d));
    memset(guest,0xef,sizeof guest);memset(&refs,0,sizeof refs);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(a.ok&&b.ok&&c.ok&&d.ok && a.result[0]==b.result[0]);
    assert(a.result[0][512*32]==0x33 && c.result[0][512*32]==0x91 && d.result[0][512*32]==0x91);
    cleanup();
}
static void wait_prepared(void)
{
    uint64_t end=sceKernelGetProcessTimeWide()+2000000;
    unsigned target=__atomic_load_n(&cap_submitted,__ATOMIC_RELAXED);
    while(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)!=target && sceKernelGetProcessTimeWide()<end)usleep(100);
    assert(__atomic_load_n(&cap_completed,__ATOMIC_ACQUIRE)==target);
}
#if XV_VERTEX_CAPTURE_READY
static void capture_ready_results(void)
{
    unsigned char src[4096],other[4096];memset(src,0x35,sizeof src);memset(other,0x57,sizeof other);
    output first={0},both={0},mixed={0},pending={0},repeat={0},after_drain={0};
    assert(capture(0,src,sizeof src,16,NULL,0,&first));wait_prepared();
    unsigned submitted=cap_submitted,checks=cap_reuse_checks,ready=cap_ready_draws;
    xv_vertex_prepare_batch b={.slot=0,.count=2};
    for(unsigned i=0;i<2;i++)b.streams[i]=(xv_vertex_prepare_stream){.source=src,.bytes=sizeof src,.stride=16};
    const void **targets[]={(const void **)&both.result[0],(const void **)&both.result[1]};
    assert(xv_vertex_capture_submit(&b,targets,collected,&both));
    assert(first.callbacks==1 && both.callbacks==1 && both.ok);
    assert(both.result[0]==first.result[0] && both.result[1]==first.result[0]);
    assert(cap_submitted==submitted && cap_ready_draws==ready+1 && cap_reuse_checks==checks+2);

    /* One miss queues the whole batch. Preflight hits are not compared twice,
     * and no target/callback may be published before the full job succeeds. */
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    b.streams[1].source=other;targets[0]=(const void **)&mixed.result[0];targets[1]=(const void **)&mixed.result[1];
    checks=cap_reuse_checks;
    assert(xv_vertex_capture_submit(&b,targets,collected,&mixed));wait_parked(&parked_capture);
    assert(!mixed.callbacks && !mixed.result[0] && !mixed.result[1]);
    assert(cap_submitted==submitted+1 && cap_reuse_checks==checks+1);
    /* Even a fully prepared hit must queue behind an outstanding predecessor. */
    assert(capture(0,src,sizeof src,16,NULL,0,&pending));
    assert(!pending.callbacks && cap_submitted==submitted+2 && cap_ready_draws==ready+1);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);wait_prepared();cap_collect();
    assert(mixed.ok && pending.ok && mixed.callbacks==1 && pending.callbacks==1);
    assert(mixed.result[0]==first.result[0] && pending.result[0]==first.result[0]);
    assert(!memcmp(mixed.result[1],other,sizeof other));

    src[4095]^=1;submitted=cap_submitted;
    assert(capture(0,src,sizeof src,16,NULL,0,&repeat));
    assert(cap_submitted==submitted+1);wait_prepared();cap_collect();
    assert(repeat.ok && repeat.result[0]!=first.result[0] && first.result[0][4095]==0x35);
    xv_vertex_capture_drain();submitted=cap_submitted;
    assert(capture(0,src,sizeof src,16,NULL,0,&after_drain));
    assert(cap_submitted==submitted+1);join(0);
    assert(after_drain.ok && !memcmp(after_drain.result[0],src,sizeof src));cleanup();
}
static void capture_ready_copy_and_pressure(void)
{
    static unsigned char src[65536],other[65536];memset(src,0x26,sizeof src);memset(other,0x48,sizeof other);
    output first={0},second={0},again={0};
    xv_vertex_worker_override(1);__atomic_store_n(&pause_copy,1,__ATOMIC_RELEASE);
    assert(capture(0,src,sizeof src,32,NULL,0,&first));wait_prepared();wait_parked(&parked_copy);
    assert(capture(0,other,sizeof other,32,NULL,0,&second));wait_prepared();
    assert(cap_used==XV_VERTEX_CAPTURE_BYTES);
    unsigned submitted=cap_submitted,pressure=cap_pressure;
    assert(capture(0,src,sizeof src,32,NULL,0,&again));
    assert(again.ok && again.callbacks==1 && cap_submitted==submitted && cap_pressure==pressure);
    assert(again.result[0]==first.result[0]);
    /* Preparation completion is not GPU-copy completion. Source changes after
     * capture cannot affect either result; the original seal/wait still owns it. */
    memset(src,0xff,sizeof src);memset(other,0xff,sizeof other);
    __atomic_store_n(&pause_copy,0,__ATOMIC_RELEASE);join(0);
    for(unsigned i=0;i<sizeof src;i++)assert(again.result[0][i]==0x26 && second.result[0][i]==0x48);
    cleanup();
}
static void capture_ready_pressure_miss(void)
{
    static unsigned char src[65536],other[65536];memset(src,0x31,sizeof src);memset(other,0x52,sizeof other);
    output first={0},second={0},mixed={0};
    assert(capture(0,src,sizeof src,32,NULL,0,&first));wait_prepared();
    assert(capture(0,other,sizeof other,32,NULL,0,&second));wait_prepared();
    assert(cap_used==XV_VERTEX_CAPTURE_BYTES);other[0]^=1;
    xv_vertex_prepare_batch b={.slot=0,.count=2};
    b.streams[0]=(xv_vertex_prepare_stream){.source=src,.bytes=sizeof src,.stride=32};
    b.streams[1]=(xv_vertex_prepare_stream){.source=other,.bytes=sizeof other,.stride=32};
    const void **targets[]={(const void **)&mixed.result[0],(const void **)&mixed.result[1]};
    unsigned pressure=cap_pressure,submitted=cap_submitted;
    assert(xv_vertex_capture_submit(&b,targets,collected,&mixed));
    assert(cap_pressure==pressure+1 && cap_submitted==submitted+1);join(0);
    assert(mixed.ok && mixed.callbacks==1 && !memcmp(mixed.result[0],src,sizeof src));
    assert(!memcmp(mixed.result[1],other,sizeof other) && second.result[0][0]==0x52);cleanup();
}
#if XV_PACKED_VERTEX_LAYOUT
static void capture_ready_packed_and_failure(void)
{
    unsigned char src[4096];memset(src,0x19,sizeof src);
    output first={0},same={0},changed={0},failed={0},recovered={0};
    assert(capture(0,src,sizeof src,32,NULL,XV_PACKED_PREFIX16,&first));wait_prepared();
    unsigned submitted=cap_submitted;
    for(unsigned i=16;i<sizeof src;i+=32)memset(src+i,0xee,16);
    assert(capture(0,src,sizeof src,32,NULL,XV_PACKED_PREFIX16,&same));
#if XV_VERTEX_CAPTURE_PACKED
    assert(same.ok && cap_submitted==submitted && same.result[0]==first.result[0]);
#else
    assert(cap_submitted==submitted+1);wait_prepared();cap_collect();
#endif
    src[15]^=1;submitted=cap_submitted;
    assert(capture(0,src,sizeof src,32,NULL,XV_PACKED_PREFIX16,&changed));
    assert(cap_submitted==submitted+1);wait_prepared();cap_collect();
    xv_vertex_prepare_batch b={.slot=0,.count=2};
    b.streams[0]=(xv_vertex_prepare_stream){.source=src,.bytes=sizeof src,.stride=32,.packed=XV_PACKED_PREFIX16};
    b.streams[1]=(xv_vertex_prepare_stream){.source=src,.bytes=32,.stride=4,.packed=99};
    const void **targets[]={(const void **)&failed.result[0],(const void **)&failed.result[1]};
    assert(xv_vertex_capture_submit(&b,targets,collected,&failed));wait_prepared();
    submitted=cap_submitted;
    assert(capture(0,src,sizeof src,32,NULL,XV_PACKED_PREFIX16,&recovered));
    assert(failed.callbacks==1 && !failed.ok && !failed.result[0] && !failed.result[1]);
    assert(recovered.ok && cap_submitted==submitted && recovered.result[0]==changed.result[0]);
    join(0);assert(first.result[0][15]==0x19 && recovered.result[0][15]==0x18);cleanup();
}
#endif
#endif
static void capture_reuse_capacity(void)
{
    unsigned char guest[CAPTURE_ENTRIES+2][32];output outputs[CAPTURE_ENTRIES+3]={0};
    for(unsigned i=0;i<CAPTURE_ENTRIES+2;i++) {
        memset(guest[i],i,sizeof guest[i]);assert(capture(0,guest[i],32,4,NULL,0,&outputs[i]));
        wait_prepared(); /* Complete without resetting the private arena. */
    }
    assert(cap_entry_count==CAPTURE_ENTRIES);
    unsigned used=cap_used,prepared=cap_reuse_prepared;
#if XV_VERTEX_CAPTURE_READY
    unsigned ready_before=cap_ready_draws;
#endif
    assert(capture(0,guest[0],32,4,NULL,0,&outputs[CAPTURE_ENTRIES+2]));
    assert(cap_used==used);join(0);
#if XV_VERTEX_CAPTURE_READY
    assert(cap_ready_draws==ready_before+1 && cap_reuse_prepared==prepared);
#else
    assert(cap_reuse_prepared==prepared+1);
#endif
    for(unsigned i=0;i<CAPTURE_ENTRIES+2;i++) {
        assert(outputs[i].ok&&outputs[i].callbacks==1);
        for(unsigned j=0;j<32;j++)assert(outputs[i].result[0][j]==(unsigned char)i);
    }
    assert(outputs[0].result[0]==outputs[CAPTURE_ENTRIES+2].result[0]);cleanup();
    setenv("XV_VERTEX_CAPTURE_REUSE","0",1);
    output a={0},b={0};assert(capture(0,guest[0],32,4,NULL,0,&a));
    assert(capture(0,guest[0],32,4,NULL,0,&b));assert(cap_used==64 && !cap_entry_count);
    join(0);assert(a.ok&&b.ok);cleanup();unsetenv("XV_VERTEX_CAPTURE_REUSE");
}
static void capture_reuse_failure(void)
{
    unsigned char guest[32]={0};output a={0},b={0};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    __atomic_store_n(&fail_gpu_alloc,1,__ATOMIC_RELAXED);
    assert(capture(0,guest,32,4,NULL,0,&a));wait_parked(&parked_capture);
    assert(capture(0,guest,32,4,NULL,0,&b));assert(cap_used==32);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(!a.ok&&!a.result[0]&&a.callbacks==1&&b.ok&&b.result[0]&&b.callbacks==1);
    cleanup();
}
static void capture_retained_generations(void)
{
    setenv("XV_VERTEX_CAPTURE_RETAIN","1",1);
    unsigned char *guest=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    assert(guest!=MAP_FAILED);
    unsigned char expected[512],filler[512];
    for(unsigned i=0;i<sizeof expected;i++)guest[i]=expected[i]=(unsigned char)(i*7);
    memset(filler,0xbc,sizeof filler);output first={0},again={0};
    assert(capture(0,guest,sizeof expected,16,NULL,0,&first));join(0);
    assert(first.ok && cap_entry_count==1 && cap_used==sizeof expected);
    xv_vertex_capture_drain(); /* The already-collected branch must also retain safely. */
    assert(!cap_results[0]);
    unsigned retained=cap_retained_hits,used=cap_used;
    /* A true retained hit cannot write even one byte into the old CPU payload.
     * Reserve its former GPU address for DIFFERENT data in the next generation:
     * retaining a stale GPU result would return that unrelated filler. */
    assert(!mprotect(cap_arena,4096,PROT_READ));
    xv_vertex_upload_reset(0);
    const void *other=xv_vertex_upload(0,filler,sizeof filler);assert(other==first.result[0]);
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,guest,sizeof expected,16,NULL,0,&again));wait_parked(&parked_capture);
    assert(!mprotect(guest,4096,PROT_NONE));
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(again.ok && again.result[0]!=other && cap_used==used && cap_retained_hits==retained+1);
    assert(!memcmp(again.result[0],expected,sizeof expected) && !memcmp(other,filler,sizeof filler));
    assert(!mprotect(cap_arena,4096,PROT_READ|PROT_WRITE));
    assert(!mprotect(guest,4096,PROT_READ|PROT_WRITE));
    /* Source mutation, guest address reuse, all three retired GPU slots, and
     * both stable and changed payloads must remain exact across joined drains. */
    for(unsigned generation=0;generation<12;generation++) {
        unsigned slot=generation%3;output next={0};xv_vertex_upload_reset(slot);
        if(generation%6==0)guest[511]=expected[511]^=1;
        assert(capture(slot,guest,sizeof expected,16,NULL,0,&next));join(slot);
        assert(next.ok && !memcmp(next.result[0],expected,sizeof expected));
        for(unsigned i=0;i<cap_entry_count;i++)assert(!cap_results[i]);
    }
    assert(!munmap(guest,4096));cleanup();assert(!cap_entry_count && !cap_used);
    setenv("XV_VERTEX_CAPTURE_RETAIN","0",1);
    output off={0};assert(capture(0,expected,sizeof expected,16,NULL,0,&off));join(0);
    assert(off.ok && !cap_used && !cap_entry_count);cleanup();unsetenv("XV_VERTEX_CAPTURE_RETAIN");
    output default_off={0};assert(capture(0,expected,sizeof expected,16,NULL,0,&default_off));join(0);
    assert(default_off.ok && !cap_used && !cap_entry_count);cleanup();
}
#endif
#if XV_VERTEX_PERSISTENT
static void persistent_slots(void)
{
    setenv("XV_VERTEX_PERSISTENT","1",1);
    unsigned char *src=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS|MAP_PRIVATE,-1,0);
    assert(src!=MAP_FAILED);memset(src,0x51,4096);
    output a={0},pending={0},b={0},changed={0};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,src,4096,16,NULL,0,&a));wait_parked(&parked_capture);
    assert(capture(1,src,4096,16,NULL,0,&pending));
    assert(!cap_used && vp_created==1 && vp_hits==1);
    assert(!mprotect(src,4096,PROT_NONE));
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);join(1);
    assert(a.ok&&pending.ok&&a.result[0]==pending.result[0]);
    for(unsigned i=0;i<4096;i++)assert(a.result[0][i]==0x51);
    assert(!mprotect(src,4096,PROT_READ|PROT_WRITE));
    /* Exact cross-slot hit writes neither the immutable mirror nor GPU bytes. */
    assert(!mprotect(vp_cpu,XV_VERTEX_PERSISTENT_BYTES,PROT_READ));
    assert(!mprotect(vp_gpu,XV_VERTEX_PERSISTENT_BYTES,PROT_READ));
    assert(capture(2,src,4096,16,NULL,0,&b));join(2);
    assert(b.ok&&b.result[0]==a.result[0]&&vp_copied==4096);
    assert(!mprotect(vp_cpu,XV_VERTEX_PERSISTENT_BYTES,PROT_READ|PROT_WRITE));
    assert(!mprotect(vp_gpu,XV_VERTEX_PERSISTENT_BYTES,PROT_READ|PROT_WRITE));
    xv_vertex_capture_begin_slot(0);xv_vertex_upload_reset(0);
    memset(src,0x92,4096);assert(capture(0,src,4096,16,NULL,0,&changed));join(0);
    assert(changed.ok&&changed.result[0]!=a.result[0]);
    for(unsigned i=0;i<4096;i++)assert(a.result[0][i]==0x51&&changed.result[0][i]==0x92);
    xv_vertex_capture_begin_slot(2);assert(vp_entries[0].pins==2);
    xv_vertex_capture_drain();assert(vp_entries[0].pins==2); /* CPU join is not retirement. */
    xv_vertex_capture_begin_slot(1);assert(!vp_entries[0].pins);
    assert(!munmap(src,4096));cleanup();
}
static void persistent_pressure(void)
{
    enum { BYTES=4*VP_PAGE, CACHED=VP_PAGES/4, ITEMS=CACHED+8 };
    _Static_assert(CACHED<VP_ENTRIES,"page pressure precedes metadata pressure");
    unsigned char *src=malloc(ITEMS*BYTES);assert(src);
    output items[ITEMS];memset(items,0,sizeof items);
    for(unsigned i=0;i<ITEMS;i++) {
        memset(src+i*BYTES,i+1,BYTES);assert(capture(i%3,src+i*BYTES,BYTES,16,NULL,0,&items[i]));
    }
    join(0);join(1);join(2);assert(vp_created==CACHED&&vp_full>=8);
    for(unsigned i=0;i<ITEMS;i++)for(unsigned j=0;j<BYTES;j++)assert(items[i].ok&&items[i].result[0][j]==i+1);
    /* Retiring just slot 0 may reclaim its ranges, never another slot's. */
    xv_vertex_capture_begin_slot(0);xv_vertex_upload_reset(0);
    output replacement={0};memset(src,0xad,BYTES);
    assert(capture(0,src,BYTES,16,NULL,0,&replacement));join(0);
    assert(replacement.ok&&replacement.result[0][0]==0xad);
    for(unsigned i=0;i<ITEMS;i++)if(i%3)for(unsigned j=0;j<BYTES;j++)assert(items[i].result[0][j]==i+1);
    free(src);cleanup();
}
static void persistent_metadata_pressure(void)
{
    _Static_assert(VP_ENTRIES<VP_PAGES,"metadata pressure precedes page pressure");
    unsigned char src[VP_ENTRIES+1][VP_PAGE];output items[VP_ENTRIES+1]={0};
    for(unsigned i=0;i<=VP_ENTRIES;i++) {
        memset(src[i],i+1,VP_PAGE);
        assert(capture(i%3,src[i],VP_PAGE,16,NULL,0,&items[i]));
    }
    join(0);join(1);join(2);assert(vp_created==VP_ENTRIES&&vp_full==1);
    unsigned free_pages=0;for(unsigned i=0;i<VP_PAGES;i++)free_pages+=!vp_pages[i];
    assert(free_pages==VP_PAGES-VP_ENTRIES);
    for(unsigned i=0;i<=VP_ENTRIES;i++)
        assert(items[i].ok&&!memcmp(items[i].result[0],src[i],VP_PAGE));
    xv_vertex_capture_begin_slot(0);xv_vertex_upload_reset(0);
    output replacement={0};memset(src[0],0xad,VP_PAGE);
    assert(capture(0,src[0],VP_PAGE,16,NULL,0,&replacement));join(0);
    assert(vp_created==VP_ENTRIES+1&&replacement.ok&&!memcmp(replacement.result[0],src[0],VP_PAGE));
    for(unsigned i=0;i<=VP_ENTRIES;i++)if(i%3)
        assert(!memcmp(items[i].result[0],src[i],VP_PAGE));
    cleanup();
}
static void persistent_fragmentation(void)
{
    enum { BYTES=4*VP_PAGE, ITEMS=VP_PAGES/4 };
    unsigned char src[ITEMS][BYTES],large[5*VP_PAGE];output items[ITEMS]={0};
    for(unsigned i=0;i<ITEMS;i++) {
        memset(src[i],i+1,BYTES);
        assert(capture(i%2,src[i],BYTES,16,NULL,0,&items[i]));
    }
    join(0);join(1);assert(vp_created==ITEMS&&!vp_full);
    xv_vertex_capture_begin_slot(0);xv_vertex_upload_reset(0);
    unsigned free_pages=0;for(unsigned i=0;i<VP_PAGES;i++)free_pages+=!vp_pages[i];
    assert(free_pages>=5); /* Four-page holes cannot fit this five-page span. */
    memset(large,0xc7,sizeof large);output fallback={0},fit={0};
    assert(capture(0,large,sizeof large,16,NULL,0,&fallback));join(0);
    assert(vp_created==ITEMS&&vp_full==1&&fallback.ok&&!memcmp(fallback.result[0],large,sizeof large));
    assert(capture(0,large,3*VP_PAGE,16,NULL,0,&fit));join(0);
    assert(vp_created==ITEMS+1&&fit.ok&&fit.result[0]==vp_gpu);
    assert(!memcmp(fit.result[0],large,3*VP_PAGE));
    assert(!memcmp(fallback.result[0],large,sizeof large));
    for(unsigned i=1;i<ITEMS;i+=2)assert(items[i].ok&&!memcmp(items[i].result[0],src[i],BYTES));
    cleanup();
}
static void persistent_bypass(void)
{
    unsigned char src[32768],expected[32768];memset(src,0x63,sizeof src);memcpy(expected,src,sizeof src);
    xv_vertex_refs refs;xv_vertex_refs_clear(&refs);xv_vertex_refs_add(&refs,0);xv_vertex_refs_add(&refs,1023);
    output sparse={0};__atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,src,sizeof src,32,&refs,0,&sparse));wait_parked(&parked_capture);
    assert(!vp_created&&!vp_hits&&!vp_gpu&&cap_used);
    memset(src,0x91,sizeof src);memset(&refs,0,sizeof refs);
    __atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(sparse.ok&&!memcmp(sparse.result[0],expected,sizeof expected));cleanup();
#if XV_PACKED_VERTEX_LAYOUT
    output packed={0};memcpy(src,expected,sizeof src);
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(capture(0,src,sizeof src,32,NULL,XV_PACKED_PREFIX16,&packed));wait_parked(&parked_capture);
    assert(!vp_created&&!vp_hits&&!vp_gpu&&cap_used);
    memset(src,0x92,sizeof src);__atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(packed.ok);for(unsigned i=0;i<sizeof src/32;i++)
        assert(!memcmp(packed.result[0]+i*16,expected+i*32,16));
    cleanup();
#endif
}
static void persistent_failures(void)
{
    unsigned char src[4096];memset(src,0x63,sizeof src);
    for(unsigned failure=1;failure<=3;failure++) {
        fail_persistent=failure;output o={0};
        assert(capture(0,src,sizeof src,16,NULL,0,&o));join(0);
        assert(o.ok&&!memcmp(o.result[0],src,sizeof src)&&vp_unavailable);
        cleanup();
    }
    fail_persistent=0;
    /* A failed earlier ordinary stream cannot leave a later promised cache
     * entry uninitialized for an already queued duplicate. */
    xv_vertex_prepare_batch batch={.slot=0,.count=2};output failed={0},next={0};
    batch.streams[0]=(xv_vertex_prepare_stream){.source=src,.bytes=32,.stride=4};
#if XV_PACKED_VERTEX_LAYOUT
    batch.streams[0].packed=99;
#else
    __atomic_store_n(&fail_gpu_alloc,1,__ATOMIC_RELAXED);
#endif
    batch.streams[1]=(xv_vertex_prepare_stream){.source=src,.bytes=4096,.stride=16};
    const void **targets[]={(const void **)&failed.result[0],(const void **)&failed.result[1]};
    __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);
    assert(xv_vertex_capture_submit(&batch,targets,collected,&failed));wait_parked(&parked_capture);
    assert(capture(0,src,sizeof src,16,NULL,0,&next));
    memset(src,0x7a,sizeof src);__atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);join(0);
    assert(!failed.ok&&!failed.result[0]&&!failed.result[1]&&failed.callbacks==1);
    assert(next.ok&&next.callbacks==1);for(unsigned i=0;i<4096;i++)assert(next.result[0][i]==0x63);
    cleanup();
}
static void persistent_generations(void)
{
    unsigned char src[3][1024];output live[3][3]={0};unsigned char expected[3][3][1024];
    unsigned state=78213;
    for(unsigned generation=0;generation<240;generation++) {
        unsigned slot=generation%3;xv_vertex_capture_begin_slot(slot);xv_vertex_upload_reset(slot);
        memset(live[slot],0,sizeof live[slot]);
        for(unsigned item=0;item<3;item++) {
            if(generation<3 || generation%4==0)for(unsigned b=0;b<1024;b++) {
                state=state*1664525u+1013904223u;src[item][b]=state>>24;
            }
            memcpy(expected[slot][item],src[item],1024);
            assert(capture(slot,src[item],1024,16,NULL,0,&live[slot][item]));
        }
        join(slot);
        for(unsigned s=0;s<3;s++)for(unsigned i=0;i<3;i++)if(live[s][i].callbacks)
            assert(live[s][i].ok&&!memcmp(live[s][i].result[0],expected[s][i],1024));
    }
    assert(vp_hits>100);cleanup();setenv("XV_VERTEX_PERSISTENT","0",1);
}
#endif
#if XV_VERTEX_CAPTURE_NOTIFY
static void *complete_at_frontier(void *arg)
{
    unsigned point=(uintptr_t)arg;
    reached(point);ungate(CAP_BEFORE_COMPLETE);reached(CAP_AFTER_NOTIFY);ungate(point);
    return NULL;
}
static void *complete_rearmed(void *unused)
{
    reached(CAP_DRAIN_WAIT);ungate(CAP_BEFORE_COMPLETE);reached(CAP_AFTER_NOTIFY);
    ungate(CAP_DRAIN_WAIT);
    uint64_t end=sceKernelGetProcessTimeWide()+2000000;
    while(READ_NOTIFY(notify_reached[CAP_DRAIN_ARMED])<2 && sceKernelGetProcessTimeWide()<end)usleep(100);
    assert(READ_NOTIFY(notify_reached[CAP_DRAIN_ARMED])>=2);
    assert(READ_NOTIFY(cap_waiting));ungate(CAP_AFTER_NOTIFY);return NULL;
}
static void notification_races(void)
{
    unsigned char guest[64];memset(guest,0x47,sizeof guest);
    /* Arrive after empty observation, on both sides of the sleep CAS. */
    for(unsigned asleep=0;asleep<2;asleep++) {
        unsigned point=asleep?CAP_AFTER_SLEEP:CAP_BEFORE_SLEEP;
        output o={0};gate(point);gate(CAP_AFTER_COMPLETE);assert(cap_start());reached(point);
        unsigned waits=READ_NOTIFY(wake_wait_calls),timeouts=READ_NOTIFY(wake_timeouts);
        unsigned signals=READ_NOTIFY(cap_wake_signals),skips=READ_NOTIFY(cap_wake_skips);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o));ungate(point);reached(CAP_AFTER_COMPLETE);
        assert(READ_NOTIFY(wake_wait_calls)==waits+asleep);
        assert(READ_NOTIFY(wake_timeouts)==timeouts);
        assert(READ_NOTIFY(cap_wake_signals)==signals+asleep);
        assert(READ_NOTIFY(cap_wake_skips)==skips+!asleep);
        ungate(CAP_AFTER_COMPLETE);join(0);assert(o.ok && o.callbacks==1);
        assert(!memcmp(o.result[0],guest,sizeof guest));cleanup();
    }
    /* Completion published before owner arms: no wait and safe report reset
     * while the worker still owns post-publication notification bookkeeping. */
    {
        output o={0};gate(CAP_AFTER_COMPLETE);gate(CAP_AFTER_NOTIFY);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o));reached(CAP_AFTER_COMPLETE);
        unsigned waits=READ_NOTIFY(done_wait_calls);join(0);
        assert(READ_NOTIFY(done_wait_calls)==waits && !READ_NOTIFY(cap_waiting));
        xv_vertex_capture_report(1);assert(!READ_NOTIFY(cap_done_skips));
        ungate(CAP_AFTER_COMPLETE);reached(CAP_AFTER_NOTIFY);
        assert(READ_NOTIFY(cap_done_skips)==1);ungate(CAP_AFTER_NOTIFY);cleanup();
    }
    /* Owner arms first; also force completion between incomplete reread and
     * actual wait, with successful and failed notification. */
    for(unsigned scenario=0;scenario<3;scenario++) {
        unsigned point=scenario?CAP_DRAIN_WAIT:CAP_DRAIN_ARMED;
        output o={0};gate(CAP_BEFORE_COMPLETE);gate(CAP_AFTER_NOTIFY);gate(point);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o));reached(CAP_BEFORE_COMPLETE);
        unsigned signals=READ_NOTIFY(cap_done_signals),failures=READ_NOTIFY(cap_done_notify_failures);
        unsigned timeouts=READ_NOTIFY(done_timeouts);
        if(scenario==2)__atomic_store_n(&fail_done,1,__ATOMIC_RELEASE);
        pthread_t coordinator;assert(!pthread_create(&coordinator,NULL,complete_at_frontier,(void *)(uintptr_t)point));
        join(0);assert(!pthread_join(coordinator,NULL));
        assert(o.ok && o.callbacks==1 && !READ_NOTIFY(cap_waiting));
        assert(READ_NOTIFY(cap_done_signals)==signals+1);
        assert(READ_NOTIFY(cap_done_notify_failures)==failures+(scenario==2));
        assert(READ_NOTIFY(done_timeouts)==timeouts+(scenario==2));
        ungate(CAP_AFTER_NOTIFY);cleanup();
    }
    /* A whole burst needs one producer signal and no completion signals when
     * the owner is busy elsewhere. Publication/callbacks remain FIFO. */
    {
        output o[8]={0};ordered_outputs=o;ordered_at=0;
        __atomic_store_n(&pause_capture,1,__ATOMIC_RELEASE);gate(CAP_AFTER_SLEEP);
        unsigned wake=READ_NOTIFY(cap_wake_signals),skip=READ_NOTIFY(cap_wake_skips);
        unsigned done=READ_NOTIFY(cap_done_signals),dskip=READ_NOTIFY(cap_done_skips);
        for(unsigned i=0;i<8;i++){guest[0]=i;assert(capture(0,guest,sizeof guest,16,NULL,0,&o[i]));}
        wait_parked(&parked_capture);__atomic_store_n(&pause_capture,0,__ATOMIC_RELEASE);
        reached(CAP_AFTER_SLEEP);
        assert(READ_NOTIFY(cap_wake_signals)==wake+1 && READ_NOTIFY(cap_wake_skips)==skip+7);
        assert(READ_NOTIFY(cap_done_signals)==done && READ_NOTIFY(cap_done_skips)==dskip+8);
        join(0);assert(ordered_at==8);
        for(unsigned i=0;i<8;i++)assert(o[i].callbacks==1 && o[i].ok && o[i].result[0][0]==i);
        ordered_outputs=NULL;ungate(CAP_AFTER_SLEEP);cleanup();
    }
    /* First completion clears waiting. The next owner loop must arm it again
     * before the second job completes; a completed reread can also avoid a wait. */
    {
        output o[2]={0};ordered_outputs=o;ordered_at=0;
        gate(CAP_BEFORE_COMPLETE);gate(CAP_AFTER_NOTIFY);gate(CAP_DRAIN_WAIT);
        __atomic_store_n(&notify_reached[CAP_DRAIN_ARMED],0,__ATOMIC_RELAXED);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o[0]));reached(CAP_BEFORE_COMPLETE);
        guest[0]++;assert(capture(0,guest,sizeof guest,16,NULL,0,&o[1]));
        unsigned signals=READ_NOTIFY(cap_done_signals);
        pthread_t coordinator;assert(!pthread_create(&coordinator,NULL,complete_rearmed,NULL));
        join(0);assert(!pthread_join(coordinator,NULL));
        /* Worker increments after completed; join alone cannot assert its
         * post-publication counter yet. Shutdown joins the worker itself. */
        cleanup();assert(READ_NOTIFY(cap_done_signals)>=signals+1 &&
            READ_NOTIFY(cap_done_signals)<=signals+2 && ordered_at==2);
        assert(o[0].ok && o[1].ok);ordered_outputs=NULL;
    }
    /* Failed sparse wake still progresses through the finite idle wait. */
    {
        output o={0};unsigned failures=READ_NOTIFY(cap_wake_failures);
        __atomic_store_n(&fail_wake,1,__ATOMIC_RELEASE);
        assert(capture(0,guest,sizeof guest,16,NULL,0,&o));join(0);
        assert(o.ok && READ_NOTIFY(cap_wake_failures)==failures+1);cleanup();
        assert(cap_start());__atomic_store_n(&fail_wake,1,__ATOMIC_RELEASE);cleanup();
        assert(READ_NOTIFY(cap_wake_state)==CAP_SLEEPING && !READ_NOTIFY(cap_waiting));
    }
    puts("PASS: forced empty/sleep races, sparse wake, owner/completion RMW races, sticky events, failed wake/done recovery, post-publication report reset and failed shutdown wake");
}
#endif
int main(void)
{
    owner=pthread_self();setenv("XV_VERTEX_CAPTURE","1",1);
    setenv("XV_VERTEX_PERSISTENT","0",1);
    setenv("XV_VERTEX_CAPTURE_RETAIN","1",1); /* Exercise optional lifetime path. */
    xv_vertex_worker_override(0);xv_vertex_upload_override(1);
#if XV_VERTEX_CAPTURE_REUSE && XV_CAPTURE_TRUST_TAGS
    trust_verification();
#endif
    detail_sampling();
#if XV_VERTEX_CAPTURE_NOTIFY
    notification_races();
#endif
    private_inputs();sparse_and_packed();unused_mask_lifetime();pressure_and_wrap();failure_cases();
    queue_capacity();partial_failure_and_fallback();gpu_copy_lifetime();
#if XV_VERTEX_CAPTURE_PACKED
    compact_versions();compact_pressure_and_retirement();
    puts("PASS: compact staging halves payload; tail/prefix mutations, unmapped input, shorter reuse, raw/packed cache interoperation, nine retired generations, invalid requests and startup disable");
#endif
#if XV_VERTEX_CAPTURE_REUSE
    capture_reuse_versions();capture_reuse_sparse();capture_reuse_capacity();capture_reuse_failure();
    capture_retained_generations();
    puts("PASS: retained CPU payload is read-only on hits; GPU generations/reordering revalidated, mutated sources and all three slots exact, shutdown and retention disable");
    puts("PASS: exact snapshot/result reuse, mutations and return-to-old-version, unmapping, slot/stride separation, sparse-to-full validation, job wrap and full metadata cache, startup disable and allocation retry");
#endif
#if XV_VERTEX_CAPTURE_READY
    capture_ready_results();capture_ready_copy_and_pressure();capture_ready_pressure_miss();
#if XV_PACKED_VERTEX_LAYOUT
    capture_ready_packed_and_failure();
#endif
    puts("PASS: completed result reuse skips queue; mixed/pending draws preserve publication, mutation/drain invalidate, full arena hit and pending GPU copy retain ownership");
#endif
    puts("PASS: private inputs, rewritten aliases, mask ownership, packed/raw identity, arena/queue pressure, ticket wrap, partial/allocation/thread/notification failures, fallback drains, disable and three GPU-copy slots");
#if XV_VERTEX_PERSISTENT
    persistent_slots();persistent_bypass();persistent_pressure();persistent_metadata_pressure();
    persistent_fragmentation();persistent_failures();persistent_generations();
    puts("PASS: immutable persistent GPU versions, pending FIFO hits, read-only reuse, sparse/packed bypass, exact mutations, all-slot retirement, page/metadata/fragmentation pressure, allocation/map/partial failures and 240 mixed generations");
#endif
    return 0;
}
