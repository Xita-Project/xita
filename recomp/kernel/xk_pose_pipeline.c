#include "xk_pose_pipeline.h"
#include "xk_frame_snapshot.h"
#include "xk_render_snapshot.h"
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <stdio.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#else
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#endif

/* No live guest address is dereferenced by the worker. Numeric identity fields
 * are comparison keys only. A complete palette is the smallest publication. */
enum { CAP=128, NODES=64 };
typedef struct { uint32_t datum,model,pose,nodes,control; unsigned count; } Key;
typedef struct { Key key; float left[NODES][13],right[NODES][13];
    fenv_t environment; xv_pose_product product; } Input;
typedef struct { uint64_t world,frame; unsigned count; Input item[CAP]; } Inputs;
typedef struct { Key key; float right[NODES][13]; xv_render_pose result; } Output;
typedef struct { uint64_t world,frame; unsigned count; Output item[CAP]; } Results;
static Inputs *inputs;
static Results *results;
static xv_frame_snapshot exchange;
static const Results *reading;
static unsigned pending,stopping,submitted,write_slot;
static int initialized,active,acquire_attempted;
static uint64_t world=1,frame;
static unsigned hits,misses,queued,late,overflow,frames;
#ifdef __vita__
static SceUID wake=-1,thread=-1;
#else
static sem_t wake;
static pthread_t thread;
#endif
#define LOAD(p) __atomic_load_n(&(p),__ATOMIC_ACQUIRE)
#define STORE(p,v) __atomic_store_n(&(p),(v),__ATOMIC_RELEASE)
static unsigned control(void)
{
#ifdef __arm__
    unsigned v;__asm__ volatile("vmrs %0, fpscr":"=r"(v));return v&~0x9fu;
#else
    return (unsigned)fegetround();
#endif
}
static int signal_work(void)
{
#ifdef __vita__
    return sceKernelSignalSema(wake,1);
#else
    return sem_post(&wake);
#endif
}
static void wait_work(void)
{
#ifdef __vita__
    if(sceKernelWaitSema(wake,1,NULL)<0)abort();
#else
    while(sem_wait(&wake))if(errno!=EINTR)abort();
#endif
}
static void run_work(void)
{
#ifdef __vita__
    extern void xv_cpu_log_thread(const char *);
    xv_cpu_log_thread("pose-worker");
#endif
    for(;;) {
        wait_work();
        if(LOAD(stopping))break;
        if(!LOAD(pending))continue;
        Inputs *in=&inputs[submitted];
        Results *out=xv_frame_snapshot_begin(&exchange);
        if(out) {
            fenv_t saved;int saved_ok=fegetenv(&saved)==0,good=saved_ok;
            out->world=in->world;out->frame=in->frame;out->count=in->count;
            for(unsigned i=0;good&&i<in->count;i++) {
                Input *a=&in->item[i];Output *b=&out->item[i];
                if(fesetenv(&a->environment)) {good=0;break;}
                b->key=a->key;b->result.datum=a->key.datum;
                b->result.model=a->key.model;b->result.nodes=a->key.count;
                memcpy(b->right,a->right,a->key.count*sizeof a->right[0]);
                for(unsigned n=0;n<a->key.count;n++)
                    a->product(a->left[n],a->right[n],b->result.matrices[n]);
            }
            int restored=saved_ok&&fesetenv(&saved)==0;
            if(good&&restored)xv_frame_snapshot_publish(&exchange,sizeof *out);
            else xv_frame_snapshot_cancel(&exchange);
        }
        STORE(pending,0);
    }
}
#ifdef __vita__
static int worker(SceSize n,void *p) {(void)n;(void)p;run_work();return 0;}
#else
static void *worker(void *p) {(void)p;run_work();return NULL;}
#endif
static int init(void)
{
    if(initialized)return initialized>0;
    initialized=-1;
    inputs=calloc(2,sizeof *inputs);results=calloc(2,sizeof *results);
    if(!inputs||!results)goto fail;
    if(!xv_frame_snapshot_init(&exchange,&results[0],&results[1],sizeof *results))goto fail;
#ifdef __vita__
    wake=sceKernelCreateSema("xv_pose_work",0,0,1,NULL);
    if(wake<0)goto fail;
    thread=sceKernelCreateThread("xv_pose_work",worker,65,65536,0,SCE_KERNEL_CPU_MASK_USER_1,NULL);
    if(thread<0) {sceKernelDeleteSema(wake);wake=-1;goto fail;}
    if(sceKernelStartThread(thread,0,NULL)<0) {
        sceKernelDeleteThread(thread);sceKernelDeleteSema(wake);thread=wake=-1;goto fail;
    }
#else
    if(sem_init(&wake,0,0))goto fail;
    if(pthread_create(&thread,NULL,worker,NULL)) {sem_destroy(&wake);goto fail;}
#endif
    initialized=1;
    extern void xv_logf(const char *,...);
    xv_logf("[pose-pipeline] enabled: render palette snapshots, core 1, %u KiB owned buffers; one-frame age, no simulation joins removed\n",(unsigned)((2*sizeof *inputs+2*sizeof *results)/1024));
    return 1;
fail:
    { extern void xv_logf(const char *,...);
      xv_logf("[pose-pipeline] worker initialization failed; current-frame fallback retained\n"); }
    free(inputs);free(results);inputs=NULL;results=NULL;return 0;
}
void xv_pose_pipeline_begin(uint64_t next_frame)
{
    if(active)xv_pose_pipeline_end();
    frame=next_frame;acquire_attempted=0;
    /* Lazy initialization: dashboard-only launches create no new worker. */
    active=1;
    if(initialized==1) {
        write_slot=LOAD(pending)?submitted^1u:write_slot^1u;
        inputs[write_slot].count=0;inputs[write_slot].world=world;
        inputs[write_slot].frame=frame;
    }
}
int xv_pose_pipeline_try(uint32_t datum,uint32_t model,uint32_t pose,uint32_t nodes,
    unsigned count,const float *left,const void *right,unsigned stride,
    xv_pose_product product,float *output)
{
    if(!active||datum==UINT32_MAX||!count||count>NODES||!left||!right||!product||!output)return 0;
    int fresh=initialized==0;
    if(!init())return 0;
    if(fresh) {inputs[write_slot].count=0;inputs[write_slot].world=world;inputs[write_slot].frame=frame;}
    if(!acquire_attempted) {
        xv_snapshot_view view;acquire_attempted=1;
        if(xv_frame_snapshot_acquire(&exchange,&view)) {
            reading=view.data;
            if(view.size!=sizeof *reading||reading->world!=world||frame==0||reading->frame!=frame-1) {
                xv_frame_snapshot_release(&exchange);reading=NULL;
            }
        }
    }
    Key key={datum,model,pose,nodes,control(),count};
    Inputs *in=&inputs[write_slot];
    /* Only one palette per object is admitted. Later different variants use
     * current guest computation; identical repeated passes share the result. */
    Input *capture=NULL;
    for(unsigned i=0;i<in->count;i++)if(in->item[i].key.datum==datum) {
        Input *a=&in->item[i];
        if(memcmp(&a->key,&key,sizeof key)||memcmp(a->left,left,count*52u))return 0;
        for(unsigned n=0;n<count;n++)if(memcmp(a->right[n],(const char *)right+n*stride,52))return 0;
        capture=a;break;
    }
    if(!capture) {
        if(in->count==CAP) {overflow++;return 0;}
        Input *a=&in->item[in->count];
        if(fegetenv(&a->environment))return 0;
        a->key=key;a->product=product;memcpy(a->left,left,count*52u);
        for(unsigned n=0;n<count;n++)memcpy(a->right[n],(const char *)right+n*stride,52);
        in->count++;queued++;
    }
    if(reading)for(unsigned i=0;i<reading->count;i++) {
        const Output *a=&reading->item[i];
        if(memcmp(&a->key,&key,sizeof key))continue;
        /* Static bind data must still match even when guest addresses recycle. */
        for(unsigned n=0;n<count;n++)if(memcmp(a->right[n],(const char *)right+n*stride,52))goto miss;
        memcpy(output,a->result.matrices,count*52u);hits++;return 1;
    }
miss:
    misses++;return 0;
}
void xv_pose_pipeline_end(void)
{
    if(!active)return;
    active=0;
    if(initialized!=1)return;
    if(reading) {xv_frame_snapshot_release(&exchange);reading=NULL;}
    if(inputs[write_slot].count) {
        if(LOAD(pending))late++;
        else {submitted=write_slot;STORE(pending,1);if(signal_work()<0)abort();}
    }
    if(++frames==60) {
        extern void xv_logf(const char *,...);
        xv_logf("[pose-pipeline] 60 frames: reused %u fallback %u captured %u busy %u capacity %u; complete previous-frame palettes, experimental visual latency\n",hits,misses,queued,late,overflow);
        frames=hits=misses=queued=late=overflow=0;
    }
}
void xv_pose_pipeline_invalidate(void)
{
    /* Owner only. Worker sees its captured world number, not this variable. */
    if(active)xv_pose_pipeline_end();
    if(world==UINT64_MAX)abort();
    world++;
}
void xv_pose_pipeline_shutdown(void)
{
    if(active)xv_pose_pipeline_end();
    if(initialized==1) {
        STORE(stopping,1);
        /* Worker can finish a private batch; it never needs the guest owner. */
        signal_work();
#ifdef __vita__
        sceKernelWaitThreadEnd(thread,NULL,NULL);sceKernelDeleteThread(thread);sceKernelDeleteSema(wake);
#else
        pthread_join(thread,NULL);sem_destroy(&wake);
#endif
    }
    free(inputs);free(results);inputs=NULL;results=NULL;
    initialized=0;pending=stopping=submitted=write_slot=0;reading=NULL;active=0;
}
