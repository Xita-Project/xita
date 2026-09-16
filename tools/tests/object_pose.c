#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <fenv.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <xmmintrin.h>

/* Generated exact transform-loop bodies; native hierarchy batching stays off. */
void pose_reference(xctx *),pose_candidate(xctx *),pose_suffix(xctx *);
int xv_math_matrix_multiply(xctx *);
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations,workers,fast,selected,kind,comparisons,callbacks;
static unsigned holder_ready,peer_ready,serviced,held,service_calls;
static pthread_t owner_thread;
static pthread_barrier_t pair;
static const char *failure_mode;

int xd3d_object_jobs_ready(void) {return 1;}
int xk_object_io_step(void) {abort();}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int top)
{(void)a;(void)l;(void)h;(void)top;assert(n==XV_OBJECT_JOB_STACK_BYTES);return 0x100000+allocations++*n;}
int xk_mem_free(uint32_t a) {(void)a;return 1;}
static unsigned get(unsigned *p) {return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void put(unsigned *p,unsigned v) {__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static void wait_for(unsigned *p)
{
    uint64_t until=xk_os_monotonic_us()+5000000;
    while(!get(p)) {
        assert(xk_os_monotonic_us()<until);
        struct timespec pause={0,50000};nanosleep(&pause,NULL);
    }
}
static void rendezvous(void)
{int r=pthread_barrier_wait(&pair);assert(!r||r==PTHREAD_BARRIER_SERIAL_THREAD);}
static void *foreign_check(void *arg)
{
    assert(!xv_object_is_worker_thread());
    assert(!xv_object_pose_begin(arg));
    if(failure_mode&&!strcmp(failure_mode,"foreign-override"))xv_object_pose_override(1);
    return NULL;
}
static void owner_service(xctx *c)
{
    assert(pthread_equal(pthread_self(),owner_thread));
    assert(!xv_object_is_worker_thread());
    assert(xv_is_object_job(c));
    assert(!xv_object_pose_begin(c)); /* Owner service retains a real worker context. */
    if(workers==2&&fast&&selected)assert(get(&held));
    put(&serviced,1);service_calls++;
    c->r[0]=0;c->r[4]+=4;
}
static void submit_service(xctx *c)
{
    unsigned sp=c->r[4];X_PUSH32(0x291EFu);
    xv_object_job_hle(c,0x193C1Bu,owner_service);
    assert(c->r[4]==sp&&c->r[0]==0);
}
static void forbidden_handler(xctx *c) {(void)c;fprintf(stderr,"FORBIDDEN handler invoked\n");abort();}

/* Returning early invokes the same production cleanup used by generated code. */
static void early_return(xctx *c)
{
    XV_OBJECT_POSE_SCOPE();XV_OBJECT_POSE_BEGIN(c);
    assert(!!xv_object_pose_locked_==!!(selected&&workers&&fast));
    if(xv_object_pose_locked_) {
        XV_OBJECT_MATH_GUARD();
        assert(xv_object_math_locked_==xv_object_pose_locked_);
    }
    return;
}
static void recursive_scope(xctx *c)
{
    XV_OBJECT_POSE_SCOPE();XV_OBJECT_POSE_BEGIN(c);
    int token=xv_object_pose_locked_;
    early_return(c);
    XV_OBJECT_POSE_BEGIN(c); /* Subsequent loop iterations cannot reacquire. */
    assert(token==xv_object_pose_locked_);
    XV_OBJECT_POSE_FINISH();XV_OBJECT_POSE_FINISH(); /* Explicit close is idempotent. */
}
static void admission(xctx *c)
{
    { XV_OBJECT_POSE_SCOPE(); } /* A function that never reaches the loop. */
    assert(xv_object_is_worker_thread()==(workers!=0));
    xctx copy=*c;
    assert(!xv_object_pose_begin(&copy));
    void *marker=c->fiber;c->fiber=NULL;assert(!xv_object_pose_begin(c));c->fiber=marker;
    pthread_t thread;assert(!pthread_create(&thread,NULL,foreign_check,c));
    assert(!pthread_join(thread,NULL));
    recursive_scope(c);
    { XV_OBJECT_MATH_GUARD();recursive_scope(c); }
}

static void fixture(xctx *c,unsigned id,unsigned variant)
{
    unsigned base=0x200000+id*0x8000,model=base,pose=base+0x100,nodes=base+0x1000;
    unsigned object=base+0x3000,tag=base+0x3100,matrices=base+0x4000,sp=c->r[4]-0x1000;
    unsigned n=1+(id*7+variant*11)%32;
    memset(X_G(base),0xa5,0x6000);memset(X_G(sp-64),0xa5,0x400);
    c->r[0]=0;c->r[4]=sp;c->r[5]=object;c->fsp=(id+variant)%8;c->fcw=0x37f;
    c->fsw=(uint16_t)(id*97);c->preempt=1000;
    for(unsigned i=0;i<8;i++) {c->st[i]=i+.375;for(unsigned j=0;j<4;j++)c->xmm[i][j]=(float)(4*i+j)+.125f;}
    X_M32(model+0xb8)=n;X_M32(model+0xbc)=nodes;
    X_M32(sp+0x18)=tag;X_M32(sp+0x1c)=variant==1?0x95000:0;
    X_M32(sp+0x24)=matrices;X_M32(sp+0x28)=pose;X_M32(sp+0x2c)=model;
    X_M32(sp+0x10)=1;X_M16(sp+0x178)=0;X_M8(sp+0x17)=variant==2;
    X_M32(object+4)=id&1?0x1000:0;X_M32(object+0xcc)=0;
    X_M32(tag+0x8c)=id&2?0:0xffffffffu;
    for(unsigned j=0;j<3;j++) {
        X_MF32(object+0x0c+j*4)=(float)(id+j)/8;
        X_MF32(object+0x24+j*4)=j==0?1:0;
        X_MF32(object+0x30+j*4)=j==2?1:0;
        X_MF32(tag+0x14+j*4)=(float)(3-j)/16;
    }
    for(unsigned i=0;i<n;i++) {
        X_M16(nodes+i*156+0x20)=i+1<n?i+1:0xffff;
        X_M16(nodes+i*156+0x22)=0xffff;
        X_M16(nodes+i*156+0x24)=i?i-1:0xffff;
        for(unsigned j=0;j<8;j++)X_MF32(pose+i*32+j*4)=j==7?1.f:(float)((int)(i*3+j)%11-5)/32;
        for(unsigned j=0;j<13;j++)X_MF32(matrices+i*52+j*4)=j==0||j==1||j==5||j==9?1:0;
    }
    if(id%7==0)X_M32(pose)=0x7fc12345; /* Preserve exceptional native/lift behavior. */
    if(id%11==0)X_M32(pose+4)=0x80000000;
}
static void compare(xctx *c,unsigned id,unsigned variant)
{
    xctx entry=*c;fixture(c,id,variant);xctx initial=*c,expected=*c;
    unsigned base=0x200000+id*0x8000,sp=c->r[4];
    unsigned char *saved=malloc(0x6400),*output=malloc(0x6400);assert(saved&&output);
    memcpy(saved,X_G(base),0x6000);memcpy(saved+0x6000,X_G(sp-64),0x400);
    unsigned fp=_mm_getcsr()&~63u;_mm_setcsr(fp);
    pose_reference(&expected);unsigned end_fp=_mm_getcsr();
    memcpy(output,X_G(base),0x6000);memcpy(output+0x6000,X_G(sp-64),0x400);
    memcpy(X_G(base),saved,0x6000);memcpy(X_G(sp-64),saved+0x6000,0x400);
    _mm_setcsr(fp);*c=initial;
    (variant&1?pose_suffix:pose_candidate)(c);
    assert(!memcmp(c,&expected,sizeof expected));assert(_mm_getcsr()==end_fp);
    assert(!memcmp(X_G(base),output,0x6000));assert(!memcmp(X_G(sp-64),output+0x6000,0x400));
    __atomic_add_fetch(&comparisons,1,__ATOMIC_RELAXED);free(output);free(saved);*c=entry;
}
static void service_pair(xctx *c,unsigned id)
{
    if(workers!=2||!fast||!selected) {submit_service(c);return;}
    rendezvous();
    if(id==0) {
        XV_OBJECT_POSE_SCOPE();XV_OBJECT_POSE_BEGIN(c);assert(xv_object_pose_locked_>=2);
        put(&held,1);put(&holder_ready,1);wait_for(&peer_ready);
        if(kind==1)submit_service(c);
        else while(!get(&serviced)) {
            /* Must park even at recursive depth, while retaining the outer lock. */
            XV_OBJECT_MATH_GUARD();
        }
        assert(get(&serviced));put(&held,0);XV_OBJECT_POSE_FINISH();
    } else {
        wait_for(&holder_ready);put(&peer_ready,1);
        if(kind==1) {
            XV_OBJECT_MATH_GUARD(); /* Polling waiter must acknowledge the pause. */
            assert(get(&serviced)&&!get(&held));
        } else submit_service(c);
    }
}
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1];
    if(failure_mode) {
        XV_OBJECT_POSE_SCOPE();XV_OBJECT_POSE_BEGIN(c);
        if(!strcmp(failure_mode,"live-override"))xv_object_pose_override(0);
        if(!strcmp(failure_mode,"unsupported-hle")) {
            X_PUSH32(0x12345);xv_object_job_hle(c,0x1D66EC,forbidden_handler);
        }
        if(!strcmp(failure_mode,"budget-stop")) {c->preempt=0;xv_preempt(c);}
    }
    if(kind)service_pair(c,id);
    else {
        if(id==0)admission(c);
        const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
        assert(!fesetround(modes[id%4]));
        for(unsigned variant=0;variant<3;variant++)compare(c,id,variant);
        assert(!fesetround(FE_TONEAREST));
    }
    __atomic_add_fetch(&callbacks,1,__ATOMIC_RELAXED);c->r[4]+=4;
}
static void batch(xctx *c,unsigned n)
{
    assert(xv_object_jobs_begin(c));
    for(unsigned i=0;i<n;i++) {
        c->r[1]=i;c->r[4]=0x20000;X_M32(c->r[4])=0x90299;
        assert(xv_object_jobs_queue(c));
    }
    if(failure_mode&&!strcmp(failure_mode,"queued-override"))xv_object_pose_override(0);
    xv_object_jobs_finish(c);
}
int main(int argc,char **argv)
{
    owner_thread=pthread_self();workers=(unsigned)atoi(getenv("XV_OBJECT_JOB_WORKERS"));
    fast=(unsigned)atoi(getenv("XV_OBJECT_LOCK_FAST_PATH"));
    failure_mode=argc>1?argv[1]:NULL;
    assert(!pthread_barrier_init(&pair,NULL,2));
    g_xram=calloc(1,8<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<(8<<20)/4096;i++)g_xpt[i]=i*4096;
    X_MF32(0x1f0a68)=0;X_MF32(0x1f0a78)=1;X_MF32(0x1f0b04)=2;
    X_M32(0x39ce24)=0x90000;X_M32(0x90014)=0x91000;
    X_M32(0x2fc6ac)=0x92000;X_M32(0x92034)=0x93000;X_M32(0x93008)=0x94000;X_M32(0x94004)=0x1000;
    for(unsigned j=0;j<3;j++)X_MF32(0x9100c+j*4)=(float)(j+1)/8;
    for(unsigned j=0;j<13;j++)X_MF32(0x95000+j*4)=j==0?1.25f:j==1||j==5||j==9?1:0;
    setenv("XV_NATIVE_OBJECT_BASIS","1",1);
    xctx c={0};c.r[4]=0x20000;c.fcw=0x37f;
    /* The existing string diagnostics have unsynchronized lazy configuration.
     * Prime it before dispatch; its separate cold-init race is not suppressed. */
    xctx strings={0};x_str_movs(&strings,4,X_STR_REP);
    assert(!xv_object_pose_available());assert(!xv_object_pose_begin(&c));assert(!xv_object_is_worker_thread());
    xv_object_jobs_override(1);
    assert(xv_object_pose_available()==!!(workers&&fast));
    if(failure_mode) {
        selected=1;xv_object_pose_override(1);
        if(!strcmp(failure_mode,"foreign-override")) {
            pthread_t t;assert(!pthread_create(&t,NULL,foreign_check,&c));assert(!pthread_join(t,NULL));
        } else batch(&c,2);
        return 0; /* A rejected operation returning normally fails the runner. */
    }
    /* First run proves default-off. Final negative override is cancellation/restoration. */
    for(unsigned pass=0;pass<4;pass++) {
        selected=pass==1||pass==2;
        if(pass)xv_object_pose_override(pass==3?-1:1);
        kind=0;batch(&c,24);
        if(pass==1)for(kind=1;kind<=2;kind++) {
            holder_ready=peer_ready=serviced=held=0;batch(&c,2);
        }
        fprintf(stderr,"POSE PASS %u selected %u\n",pass,selected);
        xv_object_jobs_report(1);xv_object_jobs_report(0);
        assert(!xv_object_pose_begin(&c));
    }
    assert(comparisons==288&&callbacks==100);
    assert(service_calls==(workers==2&&fast?2:4));
    xv_object_jobs_shutdown();assert(!xv_object_is_worker_thread());
    free(g_xpt);free(g_xram);pthread_barrier_destroy(&pair);
    printf("PASS: %u workers fast %u: 288 full-context/memory/FP pose comparisons, both entries, default/override/restoration, service directions %s\n",
           workers,fast,workers==2&&fast?"held-holder and peer-request":"serial/disabled");
    return 0;
}
