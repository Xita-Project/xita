/* Full guest capture + the real native-data queue + ordered publication.
 * Original lifted bodies are supplied privately by the Python runner. */
#define main startup_fixture_main
#include "visibility_capture_backend.c"
#undef main
#include "../../recomp/kernel/xk_visibility_jobs.h"
#include "../../recomp/xv_phase.h"
extern int xv_visibility_pass(xctx *);
extern void f_00052E10(xctx *);
extern xs_bounds_result __real_xs_bounds(const xs_frustum *,const xs_box *);
static unsigned comparisons,declines,items[3];
static int compare,block_worker;
static sem_t entered,release_worker;
static unsigned char *before_memory;
static xctx before_context;
int xv_benchmark_compare_native_bounds(void) { return compare; }
void xv_phase_begin(xv_phase_scope *s,void *c,unsigned id) { (void)s;(void)c;(void)id;abort(); }
void xv_phase_end(xv_phase_scope *s) { (void)s;abort(); }
/* Execute the complete original lifted leaf, with its optional fast gate off. */
int xv_math_bounds(xctx *c) { (void)c;return 0; }
void __wrap_xv_preempt(xctx *c) { (void)c;abort(); }
xs_bounds_result __wrap_xs_bounds(const xs_frustum *f,const xs_box *b)
{
    int lane=worker_lane();items[lane<0?2:(unsigned)lane]++;
    if(lane==0 && __atomic_exchange_n(&block_worker,0,__ATOMIC_ACQ_REL)) {
        assert(!sem_post(&entered));wait_sem(&release_worker);
    }
    return __real_xs_bounds(f,b);
}
static void *observe(void *unused)
{
    (void)unused;wait_sem(&entered);
    /* No guest publication/context mutation before the final worker joins. */
    assert(!memcmp(before_memory,g_xram,ARENA_BYTES));
    assert(!memcmp(&before_context,&t_guest.ctx,sizeof(before_context)));
    assert(!xv_visibility_pass((xctx *)(uintptr_t)1));
    assert(!sem_post(&release_worker));return NULL;
}
static void put(unsigned a,unsigned v) { x_guest_write(a,&v,4); }
static void frustum(unsigned a,unsigned variant)
{
    float plane[16]={1,0,0,1,-1,0,0,1,0,1,0,1,0,-1,0,1};
    const float bounds[6]={-2,2,-2,2,-2,2};
    if(variant)plane[3]=-.25f;
    x_guest_write(a+0x78,plane,sizeof plane);x_guest_write(a+0x128,bounds,sizeof bounds);
}
static void setup(unsigned n,unsigned variant)
{
    memset(g_xram,0,ARENA_BYTES);g_img_base=g_xram;
    for(unsigned i=0;i<(1u<<20);++i)g_xpt[i]=ARENA_BYTES-4096;
    for(unsigned i=0;i<ARENA_BYTES/4096;++i)g_xpt[i]=i*4096;
    memset(&t_guest.ctx,0,sizeof(t_guest.ctx));t_guest.stack_limit=0x740000;t_guest.stack_base=0x790000;
    xctx *c=&t_guest.ctx;
    for(unsigned i=0;i<8;++i){c->r[i]=0x15150000+i;c->st[i]=i+.375;}
    c->r[4]=0x780000;c->fsp=3;c->fcw=0x37f;c->preempt=100000;c->f_kind=3;c->f_bits=32;
    put(0x780000,0x53af7);put(0x780004,0x400000);
    put(0x4000f8,4096);put(0x400134,2);put(0x400138,0x410000);
    X_IMG16(0x30be0c)=variant==2?3:2;X_IMG16(0x38be10)=variant==3?16382:0;
    X_IMG8(0x39cc15)=variant==1;frustum(0x2febe4,1);
    for(unsigned v=0;v<X_IMG16(0x30be0c);++v) {
        X_M16(0x2fee0c+v*0x1a0)=v%2;frustum(0x2fee20+v*0x1a0,v%2);
    }
    unsigned split=n/2;
    put(0x410034,split);put(0x410038,0x420000);
    put(0x41009c,n-split);put(0x4100a0,0x430000);
    for(unsigned i=0;i<n;++i) {
        unsigned sub=i<split?0x420000+i*36:0x430000+(i-split)*36;
        float box[6]={-.5,.5,-.5,.5,-.5,.5};
        if(i%3==1)for(unsigned a=0;a<3;++a){box[2*a]=3;box[2*a+1]=4;}
        if(i%3==2)box[1]=1.5;
        x_guest_write(sub,box,24);put(sub+24,4);put(sub+28,0x450000+i*16);
        unsigned ids[4]={i%4096,(i+31)%4096,(i+31)%4096,(i+512)%4096};
        x_guest_write(0x450000+i*16,ids,16);
    }
    if(variant==4) {
        /* Noncontiguous physical pages for input, output and guest stack. */
        unsigned pages[]={0x420,0x430,0x450,0x30b,0x77f,0x780};
        for(unsigned i=0;i<sizeof(pages)/sizeof(*pages);++i) {
            unsigned dst=0x680000+i*4096;memcpy(g_xram+dst,g_xram+pages[i]*4096,4096);g_xpt[pages[i]]=dst;
        }
    }
    if(variant==5) {
        /* Direct-image globals may have a distinct physical base. */
        g_img_base=g_xram+0x100000;X_IMG16(0x30be0c)=2;X_IMG16(0x38be10)=0;
        X_IMG8(0x39cc15)=0;X_IMG32(0x2fedc4)=0;X_IMG32(0x1f0a68)=0;
    }
}
static void run(unsigned n,unsigned variant,int observe_worker)
{
    setup(n,variant);xctx entry=t_guest.ctx;
    unsigned char *before=malloc(ARENA_BYTES),*expected=malloc(ARENA_BYTES);assert(before&&expected);
    memcpy(before,g_xram,ARENA_BYTES);fenv_t fp;assert(!fegetenv(&fp));
    f_00052E10(&t_guest.ctx);xctx reference=t_guest.ctx;memcpy(expected,g_xram,ARENA_BYTES);
    memcpy(g_xram,before,ARENA_BYTES);t_guest.ctx=entry;assert(!fesetenv(&fp));
    int rounding=fegetround(),exceptions=fetestexcept(FE_ALL_EXCEPT);pthread_t observer;
    if(observe_worker) {
        before_memory=before;before_context=entry;block_worker=1;
        assert(!pthread_create(&observer,NULL,observe,NULL));
    }
    assert(xv_visibility_pass(&t_guest.ctx));
    if(observe_worker)assert(!pthread_join(observer,NULL));
    assert(rounding==fegetround()&&exceptions==fetestexcept(FE_ALL_EXCEPT));
    /* Only the original dead stack scratch is excluded, including remapping. */
    unsigned scratch=g_xpt[(entry.r[4]-256)>>12]+((entry.r[4]-256)&4095);
    memcpy(expected+scratch,g_xram+scratch,256);
    assert(!memcmp(expected,g_xram,ARENA_BYTES));
    for(unsigned i=3;i<8;++i)assert(reference.r[i]==t_guest.ctx.r[i]);
    assert(reference.fsp==t_guest.ctx.fsp&&reference.preempt==t_guest.ctx.preempt);
    free(expected);free(before);++comparisons;
}
static void decline(void)
{
    xctx before=t_guest.ctx;unsigned char *ram=malloc(ARENA_BYTES);uint32_t *pt=malloc(4u<<20);assert(ram&&pt);
    memcpy(ram,g_xram,ARENA_BYTES);memcpy(pt,g_xpt,4u<<20);
    int rounding=fegetround(),exceptions=fetestexcept(FE_ALL_EXCEPT);
    assert(!xv_visibility_pass(&t_guest.ctx));assert(!memcmp(&before,&t_guest.ctx,sizeof before));
    assert(!memcmp(ram,g_xram,ARENA_BYTES)&&!memcmp(pt,g_xpt,4u<<20));
    assert(rounding==fegetround()&&exceptions==fetestexcept(FE_ALL_EXCEPT));free(ram);free(pt);++declines;
}
#define REJECT(change) do { setup(48,0);change;decline(); } while(0)
int main(void)
{
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=malloc(4u<<20);assert(g_xram&&g_xpt);
    t_guest.fiber=(xk_fiber*)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    assert(!sem_init(&entered,0,0)&&!sem_init(&release_worker,0,0));
    setenv("XV_OBJECT_JOB_WORKERS","2",1);assert(initialize());
    unsigned sizes[]={1,23,24,25,181,340,512};int rounds[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(unsigned workers=0;workers<=2;++workers) {
        active_workers=workers;
        for(unsigned mode=0;mode<4;++mode) {
            assert(!fesetround(rounds[mode]));
            for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)run(sizes[i],0,0);
            for(unsigned v=1;v<=5;++v)run(48,v,0);
        }
    }
    run(181,0,1);
    REJECT(count=1);count=0;REJECT(running=1);running=0;REJECT(owner=&t_guest.ctx);owner=NULL;
    REJECT(owner_notice=1);owner_notice=0;REJECT(pause_workers=1);pause_workers=0;
    REJECT(xv_watch_n=1);xv_watch_n=0;REJECT(xv_trace_funcs=1);xv_trace_funcs=0;
    REJECT(xv_light_census_enabled=1);xv_light_census_enabled=0;REJECT(xv_phase_enabled=1);xv_phase_enabled=0;
    REJECT(compare=1);compare=0;REJECT(override=0);override=-1;
    REJECT(t_guest.ctx.df=1);REJECT(t_guest.ctx.preempt=1);REJECT(t_guest.ctx.r[4]++);
    REJECT(t_guest.stack_limit=0x780010);REJECT(t_guest.stack_base=0x770000);
    REJECT(put(0x780000,0x1234));REJECT(put(0x780004,0xfffffffc));
    REJECT(X_IMG16(0x30be0c)=0);REJECT(X_IMG16(0x30be0c)=129);REJECT(X_IMG16(0x38be10)=16384);
    REJECT(put(0x4000f8,0));REJECT(put(0x4000f8,131073));REJECT(put(0x400134,0));
    REJECT(put(0x400134,513));REJECT(put(0x400138,0xfffffffc));REJECT(X_M16(0x2fee0c)=2);
    REJECT(X_M16(0x2fee0c)=0xffff);REJECT(put(0x410034,513));REJECT(put(0x410038,0xfffffffc));
    REJECT(put(0x420018,4097));REJECT(put(0x42001c,0xfffffffc));REJECT(put(0x450000,4096));
    REJECT(X_IMG32(0x1f0a68)=1);REJECT(put(0x420000,0x7f801234));REJECT(put(0x420000,0x40000000));
    REJECT(put(0x2fee98,0x7f800000));REJECT(g_xpt[0x420]=ARENA_BYTES-4096);
    REJECT(g_xpt[0x420]=1);REJECT(g_xpt[0x30b]=ARENA_BYTES-4096);
    REJECT(put(0x42001c,0x450e10);g_xpt[0x450]=0x30b000);
    REJECT(put(0x410038,0x77fff0));REJECT(g_xpt[0x30b]=0x38b000);
    setup(48,0);assert(!xv_visibility_pass((xctx *)(uintptr_t)1));
    assert(items[0]&&items[1]&&items[2]);xv_object_jobs_shutdown();
    printf("PASS %u full original/pass comparisons, %u unchanged-state declines; actual lane items %u/%u/%u\n",comparisons,declines,items[0],items[1],items[2]);
    sem_destroy(&entered);sem_destroy(&release_worker);free(g_xpt);free(g_xram);return 0;
}
