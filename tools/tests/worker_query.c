#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_jobs.h"
#include "kernel/xk_worker_query.h"
#include <assert.h>
#include <fenv.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "worker_query_axes.h"
/* Include the actual pool to inspect guard ownership during the test callback.
 * No production admission, mutex, parking or service function is replaced. */
#include "kernel/xk_object_jobs.c"
enum { RAM=4<<20,ARENA=8<<20,BSP=0x10000,COLL=0x11000,PLANES=0x12000,
 CLUSTERS=0x20000,ADJ=0x40000,PORTALS=0x50000,VERTS=0x70000,
 LISTS=0x90000,HEADS=0x91000,POOL0=0x92000,POOL1=0x93000,
 NODES0=0x94000,NODES1=0xa4000,LIGHTS=0xb4000 };
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
static unsigned allocations,comparisons,ready_count,mutation_count;
#ifdef XV_TYPED_CLUSTER_QUERY
static unsigned batch_case;
#endif
static unsigned lane_seen[2],service_ready,service_done,park_ack;
static unsigned query_held,query_tried;
static void wait_flag(unsigned *flag);
static pthread_barrier_t rendezvous;
static int mode;
#ifdef XV_QUERY_WORK_TEST
static xk_thread census_test_owner;
xk_thread *xk_cur=&census_test_owner;
xk_fiber *xk_os_fiber_current(void){return census_test_owner.fiber;}
static xv_query_work_stats expected_work[2];
static _Thread_local int reference_budget;
void query_work_reference_begin(xctx *c){reference_budget=c->preempt;}
void query_work_reference_end(xctx *c)
{
    if(mode!=0&&mode!=1)return;
    int lane=worker_lane();assert(lane>=0&&lane<2);
    unsigned n=c->r[0]&65535u,b=0;
    assert(n<=256&&reference_budget>0&&c->preempt>0&&reference_budget>=c->preempt);
    for(unsigned value=n;value;value>>=1)b++;
    unsigned cost=(unsigned)(reference_budget-c->preempt);
    expected_work[lane].counts[b]++;expected_work[lane].cost[b]+=cost;
}
static void *query_work_foreign(void *unused)
{
    (void)unused;unsigned depth=123;
    assert(!xv_object_query_work_lane((const xctx *)(uintptr_t)1,1,&depth)&&depth==123);
    assert(!xv_query_work_begin((xctx *)(uintptr_t)1,1).lane);
    return NULL;
}
static void query_work_owner_sample(unsigned count,unsigned cost,unsigned invalid)
{
    xctx *c=&census_test_owner.ctx;XV_OBJECT_MATH_GUARD();
    c->r[4]=0xc0000;c->preempt=10000;
    xv_query_work_token token=xv_query_work_begin(c,xv_object_math_locked_);
    assert(token.lane==1);
    c->r[0]=count;c->r[4]-=136;c->preempt-=cost;
    if(invalid==1)c->r[4]++;
    if(invalid==2)c->preempt=10001;
    if(invalid==3)c->preempt=0;
    xv_query_work_end(c,&token,xv_object_math_locked_);assert(!token.lane);
    xv_light_census_stats snapshot;
    assert(!xv_light_census_control(c,0,1)&&!xv_light_census_take(c,&snapshot,0));
}
static void query_work_owner_test(void)
{
    query_work_owner_sample(0,0,0);query_work_owner_sample(1,1,0);
    {XV_OBJECT_MATH_GUARD();query_work_owner_sample(256,2000,0);}
    query_work_owner_sample(7,3,1);query_work_owner_sample(7,3,2);
    query_work_owner_sample(7,3,3);query_work_owner_sample(65535,3,0);
    xv_light_census_stats snapshot;assert(xv_light_census_take(&census_test_owner.ctx,&snapshot,0));
    xv_query_work_stats *s=&snapshot.query_work[0];
    assert(s->entered==7&&s->finished==7&&s->invalid==4&&s->depth_one==2&&s->nested==1&&!s->unknown_depth);
    assert(s->counts[0]==1&&s->counts[1]==1&&s->counts[9]==1&&s->cost[9]==2000&&s->max_cost[9]==2000);
    assert(s->single_counts[0]==1&&s->single_counts[1]==1&&!s->single_counts[9]);
    assert(s->budgets[0]==1&&s->budgets[1]==1&&s->budgets[11]==1);
    assert(xv_light_census_control(&census_test_owner.ctx,1,1));
}
#endif
static _Thread_local unsigned mutation;
static pthread_mutex_t oracle=PTHREAD_MUTEX_INITIALIZER;
static unsigned char *before,*expected;
void f_00056670(xctx *),ref_00056670(xctx *),ref_commit(xctx *);
void f_000565E0(xctx *);
int xd3d_object_jobs_ready(void){return 1;}
int xk_object_io_step(void){assert(0);return -1;}
unsigned xk_mem_arena_size(void){return ARENA;}
uint32_t xk_mem_image_lo(void){return 0;}
uint32_t xk_mem_image_hi(void){return RAM;}
uint64_t xk_os_monotonic_us(void)
{struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
void xk_os_log(const char *fmt,...)
{va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t l,uint32_t h,int t)
{(void)a;(void)l;(void)h;(void)t;assert(n==XV_OBJECT_JOB_STACK_BYTES);return 0x100000+allocations++*n;}
int xk_mem_free(uint32_t a){(void)a;return 1;}
static void put(uint32_t a,const void*p,unsigned n){x_guest_write(a,p,n);}
static void w32(uint32_t a,uint32_t x){put(a,&x,4);}
static void w16(uint32_t a,uint16_t x){put(a,&x,2);}
static void fp32(uint32_t a,float f){put(a,&f,4);}
static void clear(uint32_t a,unsigned n){for(unsigned i=0;i<n;i++)X_M8(a+i)=0;}
static void graph(unsigned n,unsigned capacity)
{
    clear(BSP,0x15c);clear(COLL,0x14);X_IMG32(0x39be58)=BSP;X_IMG32(0x39be50)=COLL;
    w32(BSP+0xb0,1);w32(BSP+0xb4,COLL);w32(COLL+0xc,1);w32(COLL+0x10,PLANES);
    w32(BSP+0x134,n);w32(BSP+0x138,CLUSTERS);w32(BSP+0x154,n-1);w32(BSP+0x158,PORTALS);
    float plane[]={0,0,1,0};put(PLANES,plane,sizeof plane);fp32(0x1f0a68,0);put(0x1eaf30,axes,24);
    for(unsigned k=0;k<n;k++){
        clear(CLUSTERS+k*104,104);unsigned adj=0;
        if(k)w16(ADJ+k*64+2*adj++,k-1);
        if(k+1<n)w16(ADJ+k*64+2*adj++,k);
        w32(CLUSTERS+k*104+0x5c,adj);w32(CLUSTERS+k*104+0x60,ADJ+k*64);
        w32(0x2d2fb0+4*k,0x12340000+k);w32(HEADS+4*k,UINT32_MAX);
        if(k+1==n)continue;
        clear(PORTALS+k*64,64);w16(PORTALS+k*64,k);w16(PORTALS+k*64+2,k+1);
        float bound[]={0,0,0,100};put(PORTALS+k*64+8,bound,sizeof bound);
        w32(PORTALS+k*64+0x34,4);w32(PORTALS+k*64+0x38,VERTS+k*128);
        float vertices[]={-10,-10,0,10,-10,0,10,10,0,-10,10,0};put(VERTS+k*128,vertices,sizeof vertices);
    }
    X_IMG32(0x2d2fac)=100;X_IMG8(0x2d2fa9)=0;
    w32(LISTS,HEADS);w32(LISTS+4,POOL0);w32(LISTS+8,POOL1);
    for(unsigned j=0;j<2;j++){
        unsigned p=j?POOL1:POOL0,nodes=j?NODES1:NODES0;clear(p,0x38);clear(nodes,1024*12);
        w16(p+0x20,capacity);w16(p+0x22,12);w16(p+0x32,0x8001);w32(p+0x34,nodes);
    }
    for(unsigned k=0;k<8;k++)w32(LIGHTS+4*k,UINT32_MAX);
}
static void prepare(xctx *c,unsigned sp,unsigned k)
{
    for(unsigned i=0;i<8;i++){c->r[i]=0x13570000+k*71+i*13;c->st[i]=i+0.25;}
    c->r[0]=sp+64;c->r[4]=sp;c->r[7]=LISTS;c->fsp=k&7;c->fcw=0x37f;c->fsw=(k*0x9123)&65535;c->preempt=1000000;
    c->f_kind=XK_SUB;c->f_bits=32;c->f_op1=7;c->f_op2=9;c->f_res=(uint32_t)-2;
    c->df=k&1;c->fs_base=k*935;c->eip_hint=k*619;c->scratch=k*97;
    if(k&1)for(unsigned i=0;i<8;i++){uint64_t bits=UINT64_C(0x7ff8123456780000)+k+i;memcpy(&c->st[i],&bits,8);}
    for(unsigned i=0;i<8;i++){c->mm[i]=(uint64_t)k*7654321+i;for(unsigned j=0;j<4;j++)c->xmm[i][j]=k+i+j;}
    w32(sp,0x925b0);w32(sp+4,0x80000000);w32(sp+8,LIGHTS);w32(sp+12,sp+80);
    float radii[]={100,0,-1,1,INFINITY,NAN,-0.0f};fp32(sp+16,radii[k%7]);
    w32(sp+64,0);w16(sp+68,k%13==0?65535:0);float center[]={k%11==0?200:0,0,0};put(sp+80,center,12);
}
static void mutate(xctx *c,unsigned kind)
{
    switch(kind){
    case 1:X_IMG32(0x2d2fac)++;break;
    case 2:fp32(VERTS,77);break;
    case 3:w32(0x2d2fb0,101);break;
    case 4:g_xpt[VERTS>>12]^=4096;break;
    case 5:fp32(c->r[4]+80,30);break;
    case 6:X_IMG32(0x39be58)=COLL;break; /* simulated reset, discard only */
    case 7:g_xpt[(c->r[4]-4096)>>12]^=4096;break;
    case 8:c->fsp^=1;break;
    case 9:w32(0x2d2fb0+4*6,101);break; /* last live stamp, first page */
    case 10:w32(0x2d2fb0+4*30,101);break; /* last live stamp, second page */
    }
}
void xv_worker_query_test_ready(xctx *c,unsigned lane)
{
    assert(lane<2&&worker_lane()==(int)lane&&c==&contexts[lane]&&math_depth[lane]==1);
    ready_count++;lane_seen[lane]++;
    if(mutation){
#ifdef XV_TYPED_CLUSTER_QUERY
        /* Root retirement is explicit. In-place vertex writes and remapping
         * must be detected without a cooperative invalidation callback. */
        if(mutation==6)xv_cluster_runtime_invalidate(0xfeed);
#endif
        mutate(c,mutation);mutation_count++;
    }
    if(mode==6&&!__atomic_load_n(&query_held,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&query_held,1,__ATOMIC_RELEASE);
        uint64_t end=xk_os_monotonic_us()+5000000;
        while(!__atomic_load_n(&query_tried,__ATOMIC_ACQUIRE))assert(xk_os_monotonic_us()<end);
    }
#ifdef XV_TYPED_CLUSTER_QUERY
    if(mode==8){
        __atomic_store_n(&query_held,1,__ATOMIC_RELEASE);
        wait_flag(&service_done);
    }
#endif
}
static void equal(const void *a,const void*b,unsigned n,const char *what,unsigned k)
{
    if(memcmp(a,b,n)){for(unsigned i=0;i<n;i++)if(((const unsigned char*)a)[i]!=((const unsigned char*)b)[i]){
      fprintf(stderr,"case %u %s byte %x expected %02x actual %02x\n",k,what,i,((const unsigned char*)a)[i],((const unsigned char*)b)[i]);abort();}}
}
static void wait_flag(unsigned *flag)
{uint64_t end=xk_os_monotonic_us()+5000000;while(!__atomic_load_n(flag,__ATOMIC_ACQUIRE)){assert(xk_os_monotonic_us()<end);struct timespec t={0,1000};nanosleep(&t,NULL);}}
static void service(xctx*c)
{assert(worker_lane()<0);assert(!xv_object_query_lane(c,2,c->r[4]-16384,16404));assert(__atomic_load_n(&park_ack,__ATOMIC_ACQUIRE));c->r[4]+=4;__atomic_store_n(&service_done,1,__ATOMIC_RELEASE);}
#ifdef XV_TYPED_CLUSTER_QUERY
static void invalidating_service(xctx*c)
{assert(worker_lane()<0);c->r[0]=0;c->r[4]+=8;__atomic_store_n(&service_done,1,__ATOMIC_RELEASE);}
#endif
void f_0008FB70(xctx *c)
{
    xctx saved=*c;unsigned id=c->r[1],sp=c->r[4]-256;int lane=worker_lane();assert(lane>=0);
#ifdef XV_TYPED_CLUSTER_QUERY
    if(mode==0){
        /* Exercise direct publication through first/last partial stack pages,
         * including a six-fragment capture when the arguments cross a page. */
        static const unsigned offsets[]={0,4,64,2048,4016,4076,4092};
        sp=(sp&~4095u)-4096+offsets[(batch_case/7)%7];
    }
#endif
    if(mode!=5){int b=pthread_barrier_wait(&rendezvous);assert(!b||b==PTHREAD_BARRIER_SERIAL_THREAD);}
#ifdef XV_TYPED_CLUSTER_QUERY
    if(mode==8){
        if(id){
            wait_flag(&query_held);X_M32(c->r[4])=0x32084;
            xv_object_job_hle(c,0x184a20,invalidating_service);
        }else{
            XV_OBJECT_MATH_GUARD();prepare(c,sp,7);xctx entry=*c;
            x_guest_read(before,sp-16384,16404);
            assert(!xv_worker_query(c,xv_object_math_locked_));
            x_guest_read(expected,sp-16384,16404);
            equal(&entry,c,sizeof *c,"in-flight context",0);
            equal(before,expected,16404,"in-flight scratch",0);
            assert(X_IMG32(0x2d2fac)==100&&X_IMG8(0x2d2fa9)==0);
        }
    }else
#endif
    if(mode==4){
        if(!id){
            XV_OBJECT_MATH_GUARD();X_M32(c->r[4])=0x291ef;
            __atomic_store_n(&service_ready,1,__ATOMIC_RELEASE);
            xv_object_job_hle(c,0x193c1b,service);c->r[4]-=4;
        }else{
            wait_flag(&service_ready);while(!__atomic_load_n(&pause_workers,__ATOMIC_ACQUIRE)){}
            __atomic_store_n(&park_ack,1,__ATOMIC_RELEASE);
            XV_OBJECT_MATH_GUARD();assert(__atomic_load_n(&service_done,__ATOMIC_ACQUIRE));
            prepare(c,sp,7);
#ifdef XV_TYPED_CLUSTER_QUERY
            /* Owner service invalidates the whole batch, including queries
             * begun after it. The original guarded route remains available. */
            assert(!xv_worker_query(c,xv_object_math_locked_));ref_00056670(c);
#else
            assert(xv_worker_query(c,xv_object_math_locked_));ref_commit(c);
#endif
        }
    }else if(mode==6){
        if(id){wait_flag(&query_held);assert(xv_object_mutex_try(&math_mutex)!=0);__atomic_store_n(&query_tried,1,__ATOMIC_RELEASE);}
        for(unsigned k=0;k<64;k++){
            c->r[4]=sp;c->r[3]=LISTS;X_PUSH32(LIGHTS+id*4);X_PUSH32(0x80000000u+id);X_PUSH32(0x8d7ff);f_000565E0(c);assert(c->r[4]==sp);
            prepare(c,sp,7);c->df=0;w32(sp+4,0x80000000u+id);w32(sp+8,LIGHTS+id*4);f_00056670(c);assert(c->r[4]==sp+20);
        }
    }else if(mode==5){
        prepare(c,sp,7);c->preempt=1;f_00056670(c);abort();
#ifdef XV_TYPED_CLUSTER_QUERY
    }else for(unsigned k=batch_case;k==batch_case;k++){
        /* Only one lane injects a conflict. Invalidated immutable storage is
         * retained until the other lane completes, then rebuilt next batch. */
        if(mode==3&&id)break;
#else
    }else for(unsigned k=0;k<(mode==0?168:mode==1?12:8);k++){
#endif
        pthread_mutex_lock(&oracle);
#ifndef XV_TYPED_CLUSTER_QUERY
        unsigned sizes[]={1,7,65,256},caps[]={0,1,8,1024};graph(sizes[(k/7)%4],caps[(k/28)%4]);prepare(c,sp,k);
        if(mode==2||mode==3){graph(7,1024);prepare(c,sp,7);}
#else
        prepare(c,sp,mode==2||mode==3?7:k);
#endif
        if(k%17==0)X_IMG32(0x2d2fac)=UINT32_MAX;
        unsigned old_page=0,changed_page=0;
        if(mode==7){
            xctx entry=*c;memcpy(before,g_xram,ARENA);XV_OBJECT_MATH_GUARD();
            assert(!xv_worker_query(c,xv_object_math_locked_));
            equal(&entry,c,sizeof *c,"source context",k);equal(before,g_xram,ARENA,"source memory",k);
        }else if(mode==2){
            switch(k%4){
            case 0:changed_page=(sp-4096)>>12;old_page=g_xpt[changed_page];g_xpt[changed_page]=g_xpt[PORTALS>>12];break;
            case 1:w32(sp+12,0x2d2fb0);break;
            case 2:c->r[0]=sp-64;break;
            case 3:c->r[4]=stacks[lane]+1024;break;
            }
            xctx entry=*c;memcpy(before,g_xram,ARENA);XV_OBJECT_MATH_GUARD();
            assert(!xv_worker_query(c,xv_object_math_locked_));equal(&entry,c,sizeof *c,"alias context",k);equal(before,g_xram,ARENA,"alias memory",k);
            if(changed_page)g_xpt[changed_page]=old_page;
        }else if(mode==3){
            unsigned page=(sp-4096)>>12,old=g_xpt[page],oldvert=g_xpt[VERTS>>12];
            xctx entry=*c;memcpy(before,g_xram,ARENA);mutate(c,k+1);xctx changed=*c;memcpy(expected,g_xram,ARENA);
            *c=entry;memcpy(g_xram,before,ARENA);g_xpt[page]=old;g_xpt[VERTS>>12]=oldvert;
            {XV_OBJECT_MATH_GUARD();mutation=k+1;assert(!xv_worker_query(c,xv_object_math_locked_));mutation=0;}
            equal(&changed,c,sizeof *c,"rejected context",k);equal(expected,g_xram,ARENA,"rejected memory",k);
            g_xpt[page]=old;g_xpt[VERTS>>12]=oldvert;
        }else{
            int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};fesetround(rounds[(k/42)%4]);
            xctx entry=*c,ref=*c;memcpy(before,g_xram,ARENA);feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
            ref_00056670(&ref);int flags=fetestexcept(FE_ALL_EXCEPT);memcpy(expected,g_xram,ARENA);
            memcpy(g_xram,before,ARENA);feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
            f_00056670(c);equal(&ref,c,sizeof *c,"full context",k);equal(expected,g_xram,ARENA,"full memory",k);assert(flags==fetestexcept(FE_ALL_EXCEPT));
            *c=entry;
            {XV_OBJECT_MATH_GUARD();{XV_OBJECT_MATH_GUARD();assert(!xv_worker_query(c,xv_object_math_locked_));}}
        }
        comparisons++;*c=saved;pthread_mutex_unlock(&oracle);
    }
    *c=saved;c->r[4]+=4;
}
int main(int argc,char **argv)
{
    assert(argc==2);char *names[]={"normal","disabled","alias","mutation","parking","budget","concurrent","source","inflight"};for(mode=0;mode<9&&strcmp(argv[1],names[mode]);mode++){}assert(mode<9);
    setenv("XV_WORKER_QUERY",mode==1?"invalid":"1",1);
    g_xram=calloc(1,ARENA);g_img_base=g_xram+RAM;g_xpt=calloc(1<<20,4);before=malloc(ARENA);expected=malloc(ARENA);
    for(unsigned i=0;i<RAM/4096;i++)g_xpt[i]=(i^1u)*4096;
    assert(!pthread_barrier_init(&rendezvous,NULL,2));graph(7,1024);
    xctx owner_ctx={0};xv_object_jobs_override(1);
#ifdef XV_QUERY_WORK_TEST
    census_test_owner.fiber=(xk_fiber*)&census_test_owner;
    assert(initialize());
    assert(!xv_query_work_begin((xctx *)(uintptr_t)1,0).lane); /* OFF reads no context. */
    assert(xv_light_census_control(&census_test_owner.ctx,1,1));
    assert(!xv_query_work_begin(&contexts[0],2).lane); /* Borrowed worker on owner. */
    pthread_t foreign;assert(!pthread_create(&foreign,NULL,query_work_foreign,NULL));assert(!pthread_join(foreign,NULL));
    query_work_owner_test();
#endif
#ifdef XV_TYPED_CLUSTER_QUERY
    unsigned cases=mode==0?168:mode==1?12:mode==3?10:mode==2?8:mode==7?6:1;
    for(batch_case=0;batch_case<cases;batch_case++){
        unsigned sizes[]={1,7,65,256},caps[]={0,1,8,1024};
        graph(mode==0?sizes[(batch_case/7)%4]:mode==3&&batch_case==9?31:7,
              mode==0?caps[(batch_case/28)%4]:1024);
        if(mode==7)switch(batch_case){
        case 0:w32(0x1f0a68,1);break; /* modified constant */
        case 1:w32(BSP+0xb0,0);break; /* missing collision block */
        case 2:w32(BSP+0x138,stacks[0]);break; /* private-stack source */
        case 3:w32(COLL+0x10,0x2d2fb0);break; /* mutable visited source */
        case 4:w32(PORTALS+20,0x7f800001);break; /* signaling NaN */
        case 5:w32(BSP+0x134,257);break; /* unsupported graph size */
        }
        fesetround(FE_DOWNWARD);feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO|FE_INEXACT);
        int owner_flags=fetestexcept(FE_ALL_EXCEPT);
#endif
    assert(xv_object_jobs_begin(&owner_ctx));
    for(unsigned i=0;i<(mode==5?1:2);i++){owner_ctx.r[1]=i;owner_ctx.r[4]=0xc0000;X_M32(owner_ctx.r[4])=0x90299;assert(xv_object_jobs_queue(&owner_ctx));}
    xv_object_jobs_finish(&owner_ctx);
#ifdef XV_TYPED_CLUSTER_QUERY
    assert(fegetround()==FE_DOWNWARD&&fetestexcept(FE_ALL_EXCEPT)==owner_flags);
    }
#endif
    if(mode==0){assert(ready_count>
#ifdef XV_TYPED_CLUSTER_QUERY
        64 /* Nonpositive/nonfinite and invalid-start inputs use the original. */
#else
        100
#endif
        &&lane_seen[0]&&lane_seen[1]);}
    if(mode==1)assert(!ready_count);
    if(mode==3)assert(mutation_count==
#ifdef XV_TYPED_CLUSTER_QUERY
        10
#else
        16
#endif
    );
    if(mode==4)assert(service_done);
    if(mode==8)assert(service_done&&ready_count==1&&query_held);
    if(mode==6){
        assert(query_tried&&ready_count==128&&X_IMG32(0x2d2fac)==228);
        assert(X_M16(POOL0+0x30)==14&&X_M16(POOL1+0x30)==14);
        for(unsigned k=0;k<7;k++){
            unsigned node=X_M32(HEADS+4*k),seen=0;
            while(node!=UINT32_MAX){unsigned p=NODES0+(node&65535)*12;unsigned id=X_M32(p+4)-0x80000000;assert(id<2&&!(seen&(1u<<id)));seen|=1u<<id;node=X_M32(p+8);}
            assert(seen==3);
        }
    }
#ifdef XV_QUERY_WORK_TEST
    xv_light_census_stats measured;assert(xv_light_census_take(&census_test_owner.ctx,&measured,0));
    if(mode==0||mode==1||mode==6){
        unsigned total=0;
        for(unsigned i=1;i<3;i++){
            const xv_query_work_stats *s=&measured.query_work[i];
            assert(s->entered==s->finished&&!s->invalid&&s->depth_one==s->finished&&!s->nested&&!s->unknown_depth);
            if(mode==0||mode==1){
                equal(s->counts,expected_work[i-1].counts,sizeof s->counts,"query count buckets",i);
                equal(s->cost,expected_work[i-1].cost,sizeof s->cost,"query backedges by count",i);
            }
            total+=s->finished;
        }
        assert(!measured.query_work[0].entered&&total==(mode==6?128:comparisons));
        if(mode==0)assert(measured.query_work[1].counts[9]+measured.query_work[2].counts[9]>0); /* Before 64-result clamp. */
        printf("query-work exact prefix counts/backedges: %u completed, no invalid/unmatched samples\n",total);
    }
    assert(xv_light_census_control(&census_test_owner.ctx,0,1));
    assert(xv_light_census_take(&census_test_owner.ctx,&measured,0));
    for(unsigned i=0;i<3;i++)assert(!measured.query_work[i].entered);
#endif
    xv_object_jobs_report(1);xv_object_jobs_shutdown();
    printf("%u exact comparisons; %u private-ready visits; lanes %u/%u; mutations %u; guard retained\n",comparisons,ready_count,lane_seen[0],lane_seen[1],mutation_count);
    free(before);free(expected);free(g_xram);free(g_xpt);return 0;
}
