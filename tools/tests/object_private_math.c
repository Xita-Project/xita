#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations,completed[300],workers;
static int release_enabled,fast_path;
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
    } else if(kind==2) {c->r[1]=0x40000+id*16;c->r[2]=area+128;}
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
void f_0008FB70(xctx *c)
{
    unsigned id=c->r[1];assert(id<300);
    check_admission(c,id);
    for(unsigned kind=0;kind<4;kind++)compare_helper(c,kind,id);
    completed[id]++;c->r[4]+=4;
}
int main(void)
{
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
    setenv("XV_NATIVE_OBJECT_BASIS","1",1);
    xctx c={0};c.r[4]=0x20000;c.fcw=0x37f;
    assert(!xv_object_math_available());
    xv_object_jobs_override(1);
    assert(xv_object_math_available()==(workers!=0&&fast_path));
    for(unsigned round=0;round<2;round++) {
        /* Restore default on the second pass without changing worker mode. */
        xv_object_math_override(round?-1:release_enabled);
        assert(xv_object_jobs_begin(&c));
        for(unsigned id=0;id<300;id++) {
            c.r[1]=id;c.r[4]=0x20000;X_M32(c.r[4])=0x90299;
            assert(xv_object_jobs_queue(&c));
        }
        xv_object_jobs_finish(&c);
    }
    for(unsigned id=0;id<300;id++)assert(completed[id]==2);
    xv_object_jobs_report(2);xv_object_jobs_shutdown();
    free(g_xram);free(g_xpt);pthread_barrier_destroy(&concurrent);
    puts("PASS: 600 callbacks, 2400 private math results/context/spill comparisons; nested/shared/foreign/remapped outputs retained, both lanes rendezvous outside guard");
}
