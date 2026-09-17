/* Actual preparation + upload allocators and both native workers, Vita calls
 * backed by pthreads. Sources stay borrowed until the real completion join. */
#define XV_VERTEX_UPLOAD_BYTES (512u*1024u)
#define main vertex_baseline_main
#include "vertex_upload_test.c"
#undef main
#include "../../runtime/xv_vertex_prepare.c"
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* Separate IDs from the allocator's private ID space. */
static struct {
    int type, started;
    sem_t sem;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned bits;
    pthread_t tid;
    SceKernelThreadEntry entry;
} handles[64];
static unsigned hid=10, create_calls, fail_create;
static int pause_prepare, parked, fail_wake, fail_notify, fail_wait;
static unsigned barriers, executed_cores;
static int alloc_handle(int type)
{
    if(++create_calls==fail_create)return -1;
    assert(hid<64);unsigned id=hid++;handles[id].type=type;return id;
}
void xv_cpu_log_thread(const char *name)
{
    assert(!strcmp(name,"vertex-prepare") || !strcmp(name,"vertex-upload"));
    __atomic_fetch_add(&executed_cores,1,__ATOMIC_RELAXED);
}
void xv_gpu_write_barrier(void)
{ __atomic_thread_fence(__ATOMIC_SEQ_CST);__atomic_fetch_add(&barriers,1,__ATOMIC_RELAXED); }
SceUID sceKernelCreateSema(const char *name,SceUInt attr,int initial,int maximum,SceKernelSemaOptParam *opt)
{
    int id=alloc_handle(1);if(id<0)return id;
    assert(!initial && (maximum==1 || maximum==64));
    assert(!sem_init(&handles[id].sem,0,0));return id;
}
int sceKernelDeleteSema(SceUID id)
{ assert(handles[id].type==1 && !sem_destroy(&handles[id].sem));handles[id].type=0;return 0; }
int sceKernelSignalSema(SceUID id,int n)
{
    assert(handles[id].type==1 && n==1);
    if(id==wake && __atomic_exchange_n(&fail_wake,0,__ATOMIC_RELAXED))return -1;
    return sem_post(&handles[id].sem);
}
int sceKernelWaitSema(SceUID id,int n,SceUInt *timeout)
{
    assert(handles[id].type==1 && n==1);int r=sem_wait(&handles[id].sem);
    if(id==wake) {
        __atomic_store_n(&parked,1,__ATOMIC_RELEASE);
        while(__atomic_load_n(&pause_prepare,__ATOMIC_ACQUIRE))usleep(100);
        __atomic_store_n(&parked,0,__ATOMIC_RELEASE);
    }
    return r;
}
SceUID sceKernelCreateEventFlag(const char *name,int attr,int bits,SceKernelEventFlagOptParam *opt)
{
    int id=alloc_handle(2);if(id<0)return id;
    assert(!attr && !bits);assert(!pthread_mutex_init(&handles[id].lock,NULL));
    assert(!pthread_cond_init(&handles[id].cond,NULL));return id;
}
int sceKernelDeleteEventFlag(SceUID id)
{
    assert(handles[id].type==2);assert(!pthread_mutex_destroy(&handles[id].lock));
    assert(!pthread_cond_destroy(&handles[id].cond));handles[id].type=0;return 0;
}
int sceKernelSetEventFlag(SceUID id,unsigned bits)
{
    assert(handles[id].type==2 && bits==1);
    if(__atomic_exchange_n(&fail_notify,0,__ATOMIC_RELAXED))return -1;
    pthread_mutex_lock(&handles[id].lock);handles[id].bits|=bits;
    pthread_cond_signal(&handles[id].cond);pthread_mutex_unlock(&handles[id].lock);return 0;
}
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout)
{
    assert(handles[id].type==2 && bits==1 && timeout && *timeout==1000);
    assert(mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT));
    if(__atomic_exchange_n(&fail_wait,0,__ATOMIC_RELAXED))return -1;
    struct timespec end;clock_gettime(CLOCK_REALTIME,&end);end.tv_nsec+=1000000;
    if(end.tv_nsec>=1000000000){end.tv_sec++;end.tv_nsec-=1000000000;}
    int rc=0;pthread_mutex_lock(&handles[id].lock);
    while(!handles[id].bits && !rc)rc=pthread_cond_timedwait(&handles[id].cond,&handles[id].lock,&end);
    if(handles[id].bits){*out=handles[id].bits;handles[id].bits=0;rc=0;}
    pthread_mutex_unlock(&handles[id].lock);return rc?-1:0;
}
SceUID sceKernelCreateThread(const char *name,SceKernelThreadEntry e,int priority,SceSize size,SceUInt attr,int mask,const SceKernelThreadOptParam *opt)
{
    assert(mask==SCE_KERNEL_CPU_MASK_USER_0 && size==32768 && priority==65);
    int id=alloc_handle(3);if(id>=0)handles[id].entry=e;return id;
}
static void *host_thread(void *arg)
{ unsigned id=(uintptr_t)arg;handles[id].entry(0,NULL);return NULL; }
int sceKernelStartThread(SceUID id,SceSize n,void *arg)
{
    assert(handles[id].type==3);
    if(++create_calls==fail_create)return -1;
    assert(!pthread_create(&handles[id].tid,NULL,host_thread,(void *)(uintptr_t)id));
    handles[id].started=1;return 0;
}
int sceKernelWaitThreadEnd(SceUID id,int *status,SceUInt *timeout)
{ assert(handles[id].started && !pthread_join(handles[id].tid,NULL));handles[id].started=0;return 0; }
int sceKernelDeleteThread(SceUID id)
{ assert(handles[id].type==3 && !handles[id].started);handles[id].type=0;return 0; }
int sceKernelGetThreadCurrentPriority(void) { return 64; }
int sceKernelDelayThread(SceUInt us) { usleep(us);return 0; }
SceUInt64 sceKernelGetProcessTimeWide(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000; }
static void reset(void)
{
    xv_vertex_prepare_shutdown();xv_vertex_upload_shutdown();assert(!live);
    for(unsigned i=10;i<hid;i++)assert(!handles[i].type);
    memset(handles,0,sizeof handles);hid=10;next_id=1;create_calls=fail_create=0;
}

#ifndef XV_VERTEX_PREPARE_TEST_NO_MAIN
int main(int argc,char **argv)
{
    setenv("XV_VERTEX_PREPARE",argc>1?"0":"1",1);
    unsigned char *src=malloc(65536),*expected=malloc(65536);assert(src && expected);
    for(unsigned i=0;i<65536;i++)src[i]=(unsigned char)(i*17+23);
    xv_vertex_prepare_batch b={.slot=0,.count=1,.streams={{.source=src,.bytes=65536}}};
    if(argc>1) {
        /* Off/on/off and restoration must use the real runtime setting. */
        assert(xv_vertex_prepare_available());
        for(int mode=0;mode<4;mode++) {
            int value=mode==3?-1:mode==1;
            xv_vertex_prepare_override(value,16384);
            xv_vertex_prepare_begin(&b);
            assert(!!pending==(mode==1));
            assert(xv_vertex_prepare_finish(&b));
            assert(!memcmp(b.streams[0].result,src,65536));
        }
        assert(cutoff()==65536 && enabled==0 && override_enabled==-1);
        reset();free(src);free(expected);
        puts("PASS: actual disabled/on/off/restore behavior and configured cutoff restored");return 0;
    }
    for(unsigned fail=1;fail<=4;fail++) {
        fail_create=fail;xv_vertex_prepare_begin(&b);assert(xv_vertex_prepare_finish(&b));
        assert(!memcmp(b.streams[0].result,src,65536));reset();
    }
    fail_wake=1;xv_vertex_prepare_begin(&b);assert(!pending && xv_vertex_prepare_finish(&b));reset();
    /* Explicit comparison cutoff and restoration to enabled configuration. */
    xv_vertex_prepare_override(1,131072);
    xv_vertex_prepare_begin(&b);assert(!pending && xv_vertex_prepare_finish(&b));
    xv_vertex_prepare_override(-1,16384);
    xv_vertex_prepare_begin(&b);assert(pending && xv_vertex_prepare_finish(&b));
    assert(cutoff()==65536 && enabled==1);reset();
    /* Owner material work demonstrably proceeds while the source loan is live. */
    __atomic_store_n(&pause_prepare,1,__ATOMIC_RELEASE);
    xv_vertex_prepare_begin(&b);assert(pending==&b);
    while(!__atomic_load_n(&parked,__ATOMIC_ACQUIRE))usleep(100);
    uint32_t material[1024];for(unsigned i=0;i<1024;i++)material[i]=i*79;
    assert(!__atomic_load_n(&complete,__ATOMIC_ACQUIRE));
    memcpy(expected,src,65536);
    __atomic_store_n(&fail_notify,1,__ATOMIC_RELAXED);
    __atomic_store_n(&fail_wait,1,__ATOMIC_RELAXED);
    __atomic_store_n(&pause_prepare,0,__ATOMIC_RELEASE);
    assert(xv_vertex_prepare_finish(&b));
    src[8192]^=1;assert(!memcmp(b.streams[0].result,expected,65536));
    for(unsigned i=0;i<1024;i++)assert(material[i]==i*79);
    reset();
    /* Sparse validation preserves the exact referenced vertex records. */
    b.streams[0].bytes=65536;b.count=1;
    xv_vertex_prepare_begin(&b);assert(xv_vertex_prepare_finish(&b));
    const void *first=b.streams[0].result;
    xv_vertex_refs refs;xv_vertex_refs_clear(&refs);
    xv_vertex_refs_add(&refs,0);xv_vertex_refs_add(&refs,2047);
    b.streams[0].refs=&refs;b.streams[0].stride=32;
    src[32768]^=1; /* Not referenced in this draw. */
    xv_vertex_prepare_begin(&b);assert(xv_vertex_prepare_finish(&b));
    assert(b.streams[0].result==first);
    src[3]^=1; /* Referenced mutation creates a new retained version. */
    xv_vertex_prepare_begin(&b);assert(xv_vertex_prepare_finish(&b));
    assert(b.streams[0].result!=first && !memcmp(b.streams[0].result,src,65536));
    b.streams[0].refs=NULL;reset();
    /* Combined real preparation and GPU-copy workers, 3 slots and rewrites. */
    xv_vertex_upload_override(1);xv_vertex_worker_override(1);
    for(unsigned frame=0;frame<600;frame++) {
        unsigned slot=frame%3;xv_vertex_upload_reset(slot);
        src[frame%65536]^=17;
        b.slot=slot;b.count=2;b.streams[1]=b.streams[0];
        xv_vertex_prepare_begin(&b);assert(xv_vertex_prepare_finish(&b));
        assert(b.streams[0].result==b.streams[1].result);
        memcpy(expected,src,65536);
        xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot);
        src[(frame+1)%65536]^=51;
        assert(!memcmp(b.streams[0].result,expected,65536));
    }
    assert(batches>=600 && barriers && executed_cores);
    /* Allocator rejection and oversized descriptors do not leak a pending loan. */
    b.count=1;b.streams[0].bytes=XV_VERTEX_UPLOAD_BYTES+1;
    xv_vertex_prepare_begin(&b);assert(!xv_vertex_prepare_finish(&b) && !pending);
    b.count=XV_VERTEX_PREPARE_STREAMS+1;
    xv_vertex_prepare_begin(&b);assert(!xv_vertex_prepare_finish(&b));
    xv_vertex_prepare_report(600);reset();free(src);free(expected);
    puts("PASS: real preparation/upload workers, 600 slot generations, source rewrites, independent material work, allocation/start/dispatch failures, lost completion notification and joins");
    return 0;
}

#endif
