/* ARM instruction experiment, not a scheduler or hardware timing model.
 * The actual pool's guard/admission and production query hook run unchanged.
 * Native thread identity and uncontended kernel mutex calls are fixture stubs;
 * ASan/TSan worker tests separately exercise actual host scheduling. */
#include "kernel/xk_object_jobs.c"
#include "worker_query_axes.h"
#include <stddef.h>
#include <reent.h>

enum { RAM=4<<20,ARENA=8<<20,BSP=0x10000,COLL=0x11000,PLANES=0x12000,
 CLUSTERS=0x20000,ADJ=0x40000,PORTALS=0x50000,VERTS=0x70000,
 LISTS=0x90000,HEADS=0x91000,POOL0=0x92000,POOL1=0x93000,
 NODES0=0x94000,NODES1=0xa4000,LIGHTS=0xb4000 };
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
int xv_phase_enabled;
unsigned arm_admitted,arm_allocations,arm_allocated_bytes,arm_applied;
unsigned arm_arena_bytes=ARENA;
xctx *const arm_context_ptr=&contexts[0];
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),
 offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void f_00056670(xctx *);
static void put(uint32_t a,const void*p,unsigned n){x_guest_write(a,p,n);}
static void w32(uint32_t a,uint32_t x){put(a,&x,4);}
static void w16(uint32_t a,uint16_t x){put(a,&x,2);}
static void fp32(uint32_t a,float f){put(a,&f,4);}
static void clear(uint32_t a,unsigned n){for(unsigned i=0;i<n;i++)X_M8(a+i)=0;}

void arm_prepare(unsigned n,unsigned capacity,unsigned tweak)
{
    initialized=1;running=1;math_fast_path=1;active_workers=2;
    threads[0]=17;threads[1]=18;math_depth[0]=math_depth[1]=0;
    math_mutex.ready=math_mutex.light_ready=math_mutex.use_light=1;
    for(unsigned lane=0;lane<LANES;lane++){
        stacks[lane]=0x100000+lane*STACK_BYTES;
        query_stack_low[lane]=UINT32_MAX;query_stack_high[lane]=0;
        for(unsigned p=0;p<STACK_BYTES/4096;p++){
            unsigned offset=g_xpt[(stacks[lane]>>12)+p];stack_pages[lane][p]=offset;
            if(offset<query_stack_low[lane])query_stack_low[lane]=offset;
            if(offset>query_stack_high[lane])query_stack_high[lane]=offset;
        }
    }
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
    X_IMG32(0x2d2fac)=tweak==4?UINT32_MAX:100;X_IMG8(0x2d2fa9)=7;
    w32(LISTS,HEADS);w32(LISTS+4,POOL0);w32(LISTS+8,POOL1);
    for(unsigned j=0;j<2;j++){
        unsigned p=j?POOL1:POOL0,nodes=j?NODES1:NODES0;clear(p,0x38);clear(nodes,1024*12);
        w16(p+0x20,capacity);w16(p+0x22,12);w16(p+0x32,0x8001);w32(p+0x34,nodes);
    }
    for(unsigned k=0;k<8;k++)w32(LIGHTS+4*k,UINT32_MAX);
    unsigned sp=stacks[0]+STACK_BYTES-512;xctx *c=&contexts[0];memset(c,0,sizeof *c);
    for(unsigned i=0;i<8;i++){c->r[i]=0x13570000+i*13;c->st[i]=i+.25;}
    c->r[0]=sp+64;c->r[4]=sp;c->r[7]=LISTS;c->fsp=tweak&7;c->fcw=0x37f;c->fsw=0x9123;
    c->preempt=1000000;c->fiber=(void*)&xv_object_job_marker;
    w32(sp,0x925b0);w32(sp+4,0x80000000);w32(sp+8,LIGHTS);w32(sp+12,sp+80);
    fp32(sp+16,tweak==1?0:tweak==2?-1:100);
    if(tweak==3)w32(sp+16,0x7fc01234);
    w32(sp+64,0);w16(sp+68,tweak==5?65535:0);
    float center[]={tweak==6?200:0,0,0};put(sp+80,center,12);arm_admitted=0;
}
void arm_original(void){query_enabled=0;f_00056670(&contexts[0]);}
void arm_candidate(void){query_enabled=1;f_00056670(&contexts[0]);}
void arm_attempt(void)
{XV_OBJECT_MATH_GUARD();arm_applied=xv_worker_query(&contexts[0],xv_object_math_locked_);}
void arm_snapshot(void);
void arm_finish(void){xv_cluster_runtime_end();}
void xv_worker_query_test_ready(xctx *c,unsigned lane){(void)c;(void)lane;arm_admitted++;}
unsigned xk_mem_arena_size(void){return arm_arena_bytes;}
uint32_t xk_mem_image_lo(void){return 0;}
uint32_t xk_mem_image_hi(void){return RAM;}
uint64_t xk_os_monotonic_us(void){return 0;}
void xk_os_log(const char *fmt,...){(void)fmt;}
void __wrap_xv_preempt(xctx *c){(void)c;__builtin_trap();}
void test_boot(void){}
void abort(void){__builtin_trap();}
int *__errno(void){static int value;return &value;}
struct _reent *__getreent(void){static struct _reent value;return &value;}
char *getenv(const char *name){(void)name;return NULL;}
void __assert_func(const char*f,int line,const char*fn,const char*text)
{(void)f;(void)line;(void)fn;(void)text;__builtin_trap();}

/* Bounded fixture allocator: counts include clearing allocations; real allocator
 * bookkeeping and fragmentation are excluded and need hardware measurement. */
static unsigned char heap[1<<20] __attribute__((aligned(64)));
static unsigned heap_used;
void *malloc(size_t bytes)
{
    if(bytes>sizeof heap-heap_used)return NULL;
    size_t rounded=(bytes+63)&~(size_t)63;
    if(rounded>sizeof heap-heap_used)return NULL;
    void *p=heap+heap_used;heap_used+=rounded;
    arm_allocations++;arm_allocated_bytes+=bytes;return p;
}
void *calloc(size_t count,size_t size)
{
    if(size&&count>SIZE_MAX/size)return NULL;
    size_t bytes=count*size;void *p=malloc(bytes);
    if(p)memset(p,0,bytes);return p;
}
void free(void *p){(void)p;}
void arm_snapshot(void)
{xv_cluster_runtime_end();heap_used=arm_allocations=arm_allocated_bytes=0;xv_cluster_runtime_begin();}
