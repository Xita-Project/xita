#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#if XV_HIERARCHY_SNAPSHOT
#include "kernel/xk_hierarchy_runtime.h"
#endif
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <fenv.h>

uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations,completed[300],workers;
static int release_enabled,fast_path,point_enabled;
static int quat_enabled;
static int quat_constants_original=1;
static unsigned quat_ready,quat_done,quat_service_ready,quat_service_done;
static unsigned private_ready,private_done,service_ready,service_done;
static pthread_t owner_thread;
static pthread_barrier_t concurrent;
int xd3d_object_jobs_ready(void) {return 1;}
int xk_object_io_step(void) {assert(0);return 0;}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int top)
{
    (void)a;(void)l;(void)h;(void)top;assert(n==XV_OBJECT_JOB_STACK_BYTES);
    uint32_t base=0x100000+allocations++*n;
    /* Real stacks need not be physically contiguous. Retain that property. */
    for(unsigned i=0;i<n/4096;i++)g_xpt[(base>>12)+i]=base+n-4096-i*4096;
    return base;
}
int xk_mem_free(uint32_t a) {(void)a;return 1;}
int xv_math_point_transform(xctx *);
int xv_math_matrix_multiply(xctx *);
int xv_math_quaternion_matrix(xctx *);
int xv_math_object_basis(xctx *);

static void check_admission(xctx *c,unsigned id)
{
    XV_OBJECT_MATH_GUARD();
    uint32_t out=c->r[4]-1024;
    int token=xv_object_math_locked_;
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,0x40000,12,0,0));
    xctx copy=*c;
    assert(!xv_object_math_release_private(&copy,&xv_object_math_locked_,0,out,12,0,0));
    uint32_t base=c->r[4]&~(XV_OBJECT_JOB_STACK_BYTES-1u);
    uint32_t foreign=base==0x100000?0x140004:0x100004;
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,foreign,12,0,0));
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,base,4,0,0));
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,base+XV_OBJECT_JOB_STACK_BYTES-4,8,0,0));
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,out,12,0x40000,4));
    uint32_t page=out>>12,saved=g_xpt[page];g_xpt[page]=0x40000;
    assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,out,12,0,0));
    g_xpt[page]=saved;
    {
        XV_OBJECT_MATH_GUARD();
        assert(!xv_object_math_release_private(c,&xv_object_math_locked_,0,out,12,0,0));
    }
    assert(xv_object_math_locked_==token);
#if XV_HIERARCHY_SNAPSHOT
    assert(!xv_object_hierarchy_suspend(&copy,token,out,52));
    assert(!xv_object_hierarchy_suspend(c,token,0x40000,52));
    assert(!xv_object_hierarchy_suspend(c,token,foreign,52));
    assert(!xv_object_hierarchy_suspend(c,token,out,0));
    g_xpt[page]=0x40000;
    assert(!xv_object_hierarchy_suspend(c,token,out,52));
    g_xpt[page]=saved;
    { XV_OBJECT_MATH_GUARD(); assert(!xv_object_hierarchy_suspend(c,xv_object_math_locked_,out,52)); }
    /* Use a mapped span below the entry SP for the captured worklist. */
    uint32_t entry_sp=c->r[4];c->r[4]-=2048;
    int snapshot=xv_object_hierarchy_suspend(c,token,out,52);
    assert(!!snapshot==(release_enabled&&fast_path&&workers!=0));
    if(snapshot&&workers==2&&id<2) {
        int result=pthread_barrier_wait(&concurrent);
        assert(!result||result==PTHREAD_BARRIER_SERIAL_THREAD);
    }
    xv_object_hierarchy_resume(snapshot);c->r[4]=entry_sp;
#endif
#if XV_QUERY_UNLOCK
    {
        extern int xv_object_world_query_release(xctx *);
        extern void xv_object_world_query_reacquire(int);
        assert(!xv_object_world_query_release(&copy));
        { XV_OBJECT_MATH_GUARD(); assert(!xv_object_world_query_release(c)); }
        uint32_t entry_sp=c->r[4];
        c->r[4]=base+64;assert(!xv_object_world_query_release(c));c->r[4]=entry_sp;
        int unlocked=xv_object_world_query_release(c);
        assert(!!unlocked==(fast_path&&workers!=0));
        assert(xv_object_math_locked_==token);
        if(unlocked&&workers==2&&id<2) {
            /* Both lanes must reach this point without the guard. */
            int result=pthread_barrier_wait(&concurrent);
            assert(!result||result==PTHREAD_BARRIER_SERIAL_THREAD);
        }
        xv_object_world_query_reacquire(unlocked);
        assert(!xv_object_world_query_release(&copy));
    }
#endif
    int released=xv_object_math_release_private(c,&xv_object_math_locked_,0,out,12,0,0);
    assert(released==(release_enabled&&fast_path&&workers!=0));
    assert(xv_object_math_locked_==(released?0:token));
    /* This rendezvous would deadlock with either lane still holding the guard.
     * Limit it to the first pair: tail batches may contain only one callback. */
    if(released&&workers==2&&id<2) {
        int result=pthread_barrier_wait(&concurrent);
        assert(!result||result==PTHREAD_BARRIER_SERIAL_THREAD);
    }
}
static void compare_helper(xctx *c,unsigned kind,unsigned id)
{
    int (*helpers[])(xctx *)={xv_math_point_transform,xv_math_matrix_multiply,
        xv_math_quaternion_matrix,xv_math_object_basis};
    xctx entry=*c;
    uint32_t area=(c->r[4]&~4095u)+512,sp=area+512;
    uint8_t initial[1024],expected_memory[1024];
    memset(X_G(area),0x5a,sizeof initial);
    /* Read-only shared input matrices and per-object vectors. */
    c->r[4]=sp;c->r[0]=area+128;c->r[1]=0x30000;c->r[2]=0x40000+id*16;
    if(kind==1) {
        X_M32(sp+4)=0x30000;X_M32(sp+8)=0x30100;X_M32(sp+12)=area+128;
    } else if(kind==2) {
        c->r[1]=id&1?0x40000+id*16:area+64;c->r[2]=area+128;
        if(!(id&1))memcpy(X_G(c->r[1]),X_G(0x40000+id*16),16);
        X_M32(sp)=0xA0000+(id&1)*16;
    }
    else if(kind==3)c->r[5]=0x50000;
    xctx expected=*c;
    memcpy(initial,X_G(area),sizeof initial);
    /* Same production arithmetic under an enclosing shared transaction gives
     * the reference; a copied context can never release the live lane lock. */
    {
        XV_OBJECT_MATH_GUARD();
        assert(helpers[kind](&expected));
        memcpy(expected_memory,X_G(area),sizeof expected_memory);
        memcpy(X_G(area),initial,sizeof initial);
    }
    assert(helpers[kind](c));
    assert(!memcmp(c,&expected,sizeof expected));
    assert(!memcmp(X_G(area),expected_memory,sizeof expected_memory));
    *c=entry;
}
static void *foreign_native_thread(void *opaque)
{
    assert(!xv_object_private_point(opaque));
    assert(!xv_object_private_quaternion(opaque));return NULL;
}
static void wait_flag(unsigned *flag)
{
    uint64_t until=xk_os_monotonic_us()+5000000;
    while(!__atomic_load_n(flag,__ATOMIC_ACQUIRE)) {
        assert(xk_os_monotonic_us()<until);
        struct timespec delay={0,1000};nanosleep(&delay,NULL);
    }
}
static void private_audio_commit(xctx *c)
{
    assert(pthread_equal(pthread_self(),owner_thread));
    assert(!xv_object_private_point(c)); /* Real worker context, owner thread. */
    c->r[4]+=4;__atomic_store_n(&service_done,1,__ATOMIC_RELEASE);
}
static void check_private_point(xctx *c,unsigned id)
{
    xctx entry=*c;
    uint32_t area=(c->r[4]&~4095u)+512;
    c->r[4]=area+512;c->r[0]=area+256;c->r[1]=area+128;c->r[2]=area+192;
    memcpy(X_G(c->r[1]),X_G(0x30000),52);
    memcpy(X_G(c->r[2]),X_G(0x40000+id*16),12);
    int admitted=point_enabled&&fast_path&&workers;
    assert(!!xv_object_private_point(c)==admitted);
    xctx copy=*c;assert(!xv_object_private_point(&copy));
    { XV_OBJECT_MATH_GUARD();assert(!xv_object_private_point(c)); }
    uint32_t saved=c->r[1];c->r[1]=0x30000;assert(!xv_object_private_point(c));c->r[1]=saved;
    saved=c->r[2];c->r[2]=0x40000;assert(!xv_object_private_point(c));c->r[2]=saved;
    saved=c->r[0];c->r[0]=0x60000;assert(!xv_object_private_point(c));c->r[0]=saved;
    uint32_t base=entry.r[4]&~(XV_OBJECT_JOB_STACK_BYTES-1u);
    saved=c->r[1];c->r[1]=base;assert(!xv_object_private_point(c));
    c->r[1]=base+XV_OBJECT_JOB_STACK_BYTES-4;assert(!xv_object_private_point(c));
    c->r[1]=base==0x100000?0x140004:0x100004;assert(!xv_object_private_point(c));c->r[1]=saved;
    uint32_t page=c->r[1]>>12,old_mapping=g_xpt[page];g_xpt[page]=0x60000;
    assert(!xv_object_private_point(c));g_xpt[page]=old_mapping;
    if(id==0) {pthread_t t;assert(!pthread_create(&t,NULL,foreign_native_thread,c));assert(!pthread_join(t,NULL));}
    /* Compare all guest context and private memory, including the decline path,
     * against the exact same production helper inside a held transaction. */
    for(unsigned numeric=0;numeric<2;numeric++) {
        if(numeric)X_M32(c->r[1])=0x7fc12345u;
        xctx before=*c,expected=*c;unsigned char initial[1024],result[1024];
        memcpy(initial,X_G(area),1024);
        int accepted;
        { XV_OBJECT_MATH_GUARD();accepted=xv_math_point_transform(&expected);
          memcpy(result,X_G(area),1024);memcpy(X_G(area),initial,1024); }
        assert(xv_math_point_transform(c)==accepted);
        assert(!memcmp(c,&expected,sizeof expected));assert(!memcmp(X_G(area),result,1024));
        *c=before;
    }
    memcpy(X_G(c->r[1]),X_G(0x30000),52);
    if(admitted&&workers==2&&id<2) {
        /* Both lanes must finish the guarded reference/setup before one holds
         * the guard for the independent-execution proof. */
        int barrier=pthread_barrier_wait(&concurrent);
        assert(!barrier||barrier==PTHREAD_BARRIER_SERIAL_THREAD);
        /* This must finish with another lane retaining the guard. A guard-taking
         * implementation cannot pass merely by producing the same numbers. */
        if(id==0) {
            XV_OBJECT_MATH_GUARD();__atomic_store_n(&private_ready,1,__ATOMIC_RELEASE);
            wait_flag(&private_done);
        } else {
            wait_flag(&private_ready);assert(xv_math_point_transform(c));c->r[4]-=4;
            __atomic_store_n(&private_done,1,__ATOMIC_RELEASE);
        }
        if(id==0) {
            XV_OBJECT_MATH_GUARD();X_M32(c->r[4])=0x291EF;
            __atomic_store_n(&service_ready,1,__ATOMIC_RELEASE);
            xv_object_job_hle(c,0x193C1B,private_audio_commit);c->r[4]-=4;
        } else {
            wait_flag(&service_ready);
            uint64_t until=xk_os_monotonic_us()+5000000;
            while(!__atomic_load_n(&service_done,__ATOMIC_ACQUIRE)) {
                assert(xk_os_monotonic_us()<until);
                assert(xv_math_point_transform(c));c->r[4]-=4;
            }
        }
    }
    *c=entry;
}
static void quat_audio_commit(xctx *c)
{
    assert(pthread_equal(pthread_self(),owner_thread));
    assert(!xv_object_private_quaternion(c));
    c->r[4]+=4;__atomic_store_n(&quat_service_done,1,__ATOMIC_RELEASE);
}
static void check_private_quaternion(xctx *c,unsigned id)
{
    xctx entry=*c;
    uint32_t area=(c->r[4]&~4095u)+512;
    c->r[4]=area+512;c->r[1]=area+128;c->r[2]=area+256;
    memcpy(X_G(c->r[1]),X_G(0x40000+id*16),16);
    int admitted=quat_enabled&&quat_constants_original&&fast_path&&workers;
    assert(!!xv_object_private_quaternion(c)==admitted);
    xctx copy=*c;assert(!xv_object_private_quaternion(&copy));
    { XV_OBJECT_MATH_GUARD();assert(!xv_object_private_quaternion(c)); }
    uint32_t saved=c->r[1];c->r[1]=0x40000;assert(!xv_object_private_quaternion(c));c->r[1]=saved;
    saved=c->r[2];c->r[2]=0x60000;assert(!xv_object_private_quaternion(c));c->r[2]=saved;
    saved=c->r[4];c->r[4]=0x60000;assert(!xv_object_private_quaternion(c));
    c->r[4]=8;assert(!xv_object_private_quaternion(c));c->r[4]=saved;
    uint32_t base=entry.r[4]&~(XV_OBJECT_JOB_STACK_BYTES-1u);
    saved=c->r[1];c->r[1]=base;assert(!xv_object_private_quaternion(c));
    c->r[1]=base+XV_OBJECT_JOB_STACK_BYTES-4;assert(!xv_object_private_quaternion(c));
    c->r[1]=base==0x100000?0x140004:0x100004;assert(!xv_object_private_quaternion(c));c->r[1]=saved;
    uint32_t page=c->r[1]>>12,old_mapping=g_xpt[page];g_xpt[page]=0x60000;
    assert(!xv_object_private_quaternion(c));g_xpt[page]=old_mapping;
    if(id==0) {pthread_t t;assert(!pthread_create(&t,NULL,foreign_native_thread,c));assert(!pthread_join(t,NULL));}
    static const uint32_t edges[]={0,0x80000000,1,0x007fffff,0x3f800000,0x7f800000,0xff800000,0x7fc12345,0x7f812345,0x7f7fffff};
    static const int rounding[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned numeric=0;numeric<sizeof edges/sizeof edges[0];numeric++) {
        assert(!fesetround(rounding[numeric%4]));
        X_M32(c->r[1])=edges[numeric];
        xctx before=*c,expected=*c;unsigned char initial[1024],result[1024];
        memcpy(initial,X_G(area),1024);
        int accepted;
        { XV_OBJECT_MATH_GUARD();accepted=xv_math_quaternion_matrix(&expected);
          memcpy(result,X_G(area),1024);memcpy(X_G(area),initial,1024); }
        assert(xv_math_quaternion_matrix(c)==accepted);
        assert(!memcmp(c,&expected,sizeof expected));assert(!memcmp(X_G(area),result,1024));
        *c=before;
    }
    assert(!fesetround(FE_TONEAREST));
    memcpy(X_G(c->r[1]),X_G(0x40000+id*16),16);
    if(admitted&&workers==2&&id<2) {
        int barrier=pthread_barrier_wait(&concurrent);
        assert(!barrier||barrier==PTHREAD_BARRIER_SERIAL_THREAD);
        if(id==0) {
            XV_OBJECT_MATH_GUARD();__atomic_store_n(&quat_ready,1,__ATOMIC_RELEASE);
            wait_flag(&quat_done);
        } else {
            wait_flag(&quat_ready);assert(xv_math_quaternion_matrix(c));c->r[4]-=4;
            __atomic_store_n(&quat_done,1,__ATOMIC_RELEASE);
        }
        if(id==0) {
            XV_OBJECT_MATH_GUARD();X_M32(c->r[4])=0x291EF;
            __atomic_store_n(&quat_service_ready,1,__ATOMIC_RELEASE);
            xv_object_job_hle(c,0x193C1B,quat_audio_commit);c->r[4]-=4;
        } else {
            wait_flag(&quat_service_ready);
            uint64_t until=xk_os_monotonic_us()+5000000;
            while(!__atomic_load_n(&quat_service_done,__ATOMIC_ACQUIRE)) {
                assert(xk_os_monotonic_us()<until);
                assert(xv_math_quaternion_matrix(c));c->r[4]-=4;
            }
        }
    }
    *c=entry;
}
#ifdef TEST_HIERARCHY_INTEGRATION
#include "hierarchy_worker.inc"
#endif
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1];assert(id<300);
    check_admission(c,id);
    compare_helper(c,0,id); /* Both first lanes exercise cold guarded config. */
    check_private_point(c,id);
    for(unsigned kind=0;kind<4;kind++)compare_helper(c,kind,id);
    check_private_quaternion(c,id);
#ifdef TEST_HIERARCHY_INTEGRATION
    compare_hierarchy(c,id);
#endif
    completed[id]++;c->r[4]+=4;
}
int main(void)
{
    owner_thread=pthread_self();
#ifdef XV_OBJECT_POINT_EXPERIMENT
    const char *point=getenv("XV_OBJECT_PRIVATE_POINT");point_enabled=point&&atoi(point);
#endif
#if defined(XV_OBJECT_QUAT_EXPERIMENT) && !defined(XV_QUAT_CACHE)
    const char *quat=getenv("XV_OBJECT_PRIVATE_QUATERNION");
#ifndef XV_OBJECT_QUAT_DEFAULT
#define XV_OBJECT_QUAT_DEFAULT 0
#endif
    quat_enabled=quat?atoi(quat)!=0:XV_OBJECT_QUAT_DEFAULT;
#endif
    workers=(unsigned)atoi(getenv("XV_OBJECT_JOB_WORKERS"));
    release_enabled=atoi(getenv("XV_OBJECT_PRIVATE_MATH"));
    fast_path=atoi(getenv("XV_OBJECT_LOCK_FAST_PATH"));
    assert(!pthread_barrier_init(&concurrent,NULL,2));
    g_xram=calloc(1,2<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    for(unsigned i=0;i<512;i++)g_xpt[i]=i*4096;
    for(unsigned i=0;i<13;i++) {X_MF32(0x30000+i*4)=(float)(i+1)/8;X_MF32(0x30100+i*4)=(float)(i+3)/16;}
    for(unsigned id=0;id<300;id++)for(unsigned i=0;i<4;i++)X_MF32(0x40000+id*16+i*4)=(float)(id+i)/32;
    X_M32(0x50004)=0x1000;
    for(unsigned i=1;i<14;i++)X_MF32(0x50004+i*4)=(float)i/8;
    X_MF32(0x1F0A68)=0;X_MF32(0x1F0A78)=1;X_MF32(0x1F0B04)=2;
    /* Image data/mappings may change only while workers are drained. Exercise
     * both unsupported constant cases without concurrent writes to .rdata. */
    const char *constant_mode=getenv("OBJECT_QUAT_CONSTANT_MODE");
    if(constant_mode&&!strcmp(constant_mode,"modified")) {
        X_MF32(0x1F0B04)=3;quat_constants_original=0;
    } else if(constant_mode&&!strcmp(constant_mode,"remapped")) {
        memcpy(g_xram+0x1e0000,g_xram+0x1f0000,4096);
        g_xpt[0x1f0]=0x1e0000;quat_constants_original=0;
    }
    setenv("XV_NATIVE_OBJECT_BASIS","1",1);
#ifdef TEST_HIERARCHY_INTEGRATION
    setenv("XV_NATIVE_MODEL_HIERARCHY","1",1);
#endif
    xctx c={0};c.r[4]=0x20000;c.fcw=0x37f;
    assert(!xv_object_math_available());
    xv_object_jobs_override(1);
    assert(xv_object_math_available()==(workers!=0&&fast_path));
#ifdef XV_OBJECT_HOLD_PROFILE
    assert(xv_object_holds_available()==(workers!=0&&fast_path));
    if(getenv("OBJECT_HOLD_TEST"))xv_object_holds_override(1);
#endif
#ifdef XV_OBJECT_POINT_EXPERIMENT
    assert(xv_object_point_available()==(workers!=0&&fast_path));
#else
    assert(!xv_object_point_available());
#endif
#if defined(XV_OBJECT_QUAT_EXPERIMENT) && !defined(XV_QUAT_CACHE)
    assert(xv_object_quat_available()==(workers!=0&&fast_path));
#else
    assert(!xv_object_quat_available());
#endif
    for(unsigned round=0;round<2;round++) {
        /* Restore default on the second pass without changing worker mode. */
        xv_object_math_override(round?-1:release_enabled);
        xv_object_point_override(round?-1:point_enabled);
        xv_object_quat_override(round?-1:quat_enabled);
        private_ready=private_done=service_ready=service_done=0;
        quat_ready=quat_done=quat_service_ready=quat_service_done=0;
        assert(xv_object_jobs_begin(&c));
        for(unsigned id=0;id<300;id++) {
            c.r[1]=id;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;
            assert(xv_object_jobs_queue(&c));
        }
        xv_object_jobs_finish(&c);
    }
    for(unsigned id=0;id<300;id++)assert(completed[id]==2);
    xv_object_math_report_check();
#ifdef TEST_HIERARCHY_INTEGRATION
    xv_object_hierarchy_report(2);xv_object_hierarchy_report(0);
#endif
    extern void xv_native_math_report(unsigned);
    xv_native_math_report(2);xv_native_math_report(0);
    xv_object_jobs_report(2);xv_object_jobs_report(0);xv_object_jobs_shutdown();
    free(g_xram);free(g_xpt);pthread_barrier_destroy(&concurrent);
    printf("PASS: 600 callbacks, context/spill/ownership checks; private point held-guard and owner-service proofs executed: %d\n",
           !!(point_enabled&&fast_path&&workers==2));
    printf("private quaternion held-guard and owner-service proofs executed: %d\n",!!(quat_enabled&&quat_constants_original&&fast_path&&workers==2));
}
