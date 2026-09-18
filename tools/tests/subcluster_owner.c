/* Real worker backend plus bounded native operation admission checks. */
#define main clip_startup_fixture_main
#include "subcluster_backend_fixture.c"
#undef main
#include "../../recomp/kernel/xk_subcluster.h"
static unsigned checked;
static int which,compare;
int xv_benchmark_compare_native_bounds(void) { return compare; }
static int invoke(xctx *c) { return which ? xv_subcluster_publish(c) : xv_subcluster_bounds(c); }
static void decline(void)
{
    xctx before=t_guest.ctx;unsigned char *ram=malloc(ARENA_BYTES);uint32_t *pages=malloc(4u<<20);
    assert(ram&&pages);memcpy(ram,g_xram,ARENA_BYTES);memcpy(pages,g_xpt,4u<<20);
    int rounding=fegetround(),exceptions=fetestexcept(FE_ALL_EXCEPT);
    assert(!invoke(&t_guest.ctx));assert(!memcmp(&before,&t_guest.ctx,sizeof before));
    assert(!memcmp(ram,g_xram,ARENA_BYTES));assert(!memcmp(pages,g_xpt,4u<<20));
    assert(rounding==fegetround()&&exceptions==fetestexcept(FE_ALL_EXCEPT));free(ram);free(pages);++checked;
}
static void setup(void)
{
    memset(&t_guest.ctx,0,sizeof(t_guest.ctx));memset(g_xram,0,ARENA_BYTES);
    for(unsigned i=0;i<ARENA_BYTES/PAGE_BYTES;++i)g_xpt[i]=i*PAGE_BYTES;
    t_guest.stack_limit=0x740000;t_guest.stack_base=0x790000;
    xctx *c=&t_guest.ctx;c->r[1]=0x410000;c->r[7]=0x420000;c->preempt=10000;
    const float planes[16]={1,0,0,1,-1,0,0,1,0,1,0,1,0,-1,0,1};
    const float enclosing[6]={-2,2,-2,2,-2,2},box[6]={-.5,.5,-.5,.5,-.5,.5};
    x_guest_write(0x410078,planes,sizeof planes);x_guest_write(0x410128,enclosing,sizeof enclosing);x_guest_write(0x420000,box,sizeof box);
    if(!which) { c->r[4]=0x780000;X_M32(0x780000)=0x52ec6; }
    else {
        c->r[4]=0x77ffe4;c->r[6]=0x430000;
        X_M32(0x780000)=0x53af7;X_M32(0x780004)=0x400000;X_M32(0x4000f8)=1024;
        X_M32(0x420018)=6;X_M32(0x42001c)=0x430000;
        const uint32_t ids[6]={0,0,31,32,511,512};x_guest_write(0x430000,ids,sizeof ids);
    }
}
int main(void)
{
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
    t_guest.fiber=(xk_fiber*)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    setenv("XV_OBJECT_JOB_WORKERS","2",1);assert(initialize());
    for(which=0;which<2;++which) {
        setup();assert(!invoke((xctx*)(uintptr_t)1));assert(invoke(&t_guest.ctx));
        if(!which)assert((t_guest.ctx.r[0]&0xffff)==2&&t_guest.ctx.preempt==9993&&t_guest.ctx.r[4]==0x780008);
        else assert(X_IMG16(0x38be10)==5&&t_guest.ctx.preempt==9995&&X_M32(0x30be10)==0x80000001);
        setup();count=1;decline();count=0;running=1;decline();running=0;
        owner=&t_guest.ctx;decline();owner=NULL;pause_workers=1;decline();pause_workers=0;
        owner_notice=1;decline();owner_notice=0;xv_watch_n=1;decline();xv_watch_n=0;
        xv_trace_funcs=1;decline();xv_trace_funcs=0;xv_light_census_enabled=1;decline();xv_light_census_enabled=0;
        compare=1;decline();compare=0;
        t_guest.ctx.df=1;decline();setup();t_guest.ctx.preempt=1;decline();setup();
        t_guest.ctx.r[4]++;decline();setup();t_guest.stack_limit=0x780010;decline();setup();
        t_guest.stack_base=0x770000;decline();setup();X_M32(0x780000)=0x1234;decline();setup();
        if(!which) {
            X_M32(0x780004)=1;decline();setup();X_M32(0x1f0a68)=1;decline();setup();
            X_M32(0x410078)=0x7f801234;decline();setup();X_M32(0x420000)=0x7f800000;decline();setup();
            X_M32(0x420000)=0x3f800000;decline();setup();X_M32(0x410128)=0x40800000;decline();setup();
            t_guest.ctx.r[1]=0xfffffffc;decline();setup();t_guest.ctx.r[7]=0x77fff0;decline();setup();
            t_guest.ctx.r[7]=0x410078;decline();setup();g_xpt[0x420]=ARENA_BYTES-4096;decline();setup();
        } else {
            t_guest.ctx.r[5]=1;decline();setup();X_M32(0x420018)=4097;decline();setup();
            X_M32(0x42001c)=0x440000;decline();setup();X_M32(0x4000f8)=0;decline();setup();
            X_M32(0x430014)=1024;decline();setup();X_M16(0x38be10)=0x8000;decline();setup();
            X_M32(0x780004)=0xfffffffc;decline();setup();t_guest.ctx.r[6]=0xfffffffc;X_M32(0x42001c)=0xfffffffc;decline();setup();
            g_img_base=(uint8_t*)((uintptr_t)g_xram-4096);decline();g_img_base=g_xram;setup();
            /* Bitset begins at page offset E10; overlap the list with it. */
            t_guest.ctx.r[6]=0x430e10;X_M32(0x42001c)=0x430e10;g_xpt[0x430]=0x30b000;decline();setup();
            g_xpt[0x30b]=ARENA_BYTES-4096;decline();setup();
        }
    }
    xv_object_jobs_shutdown();free(g_xpt);free(g_xram);
    printf("PASS both native operations and %u full guest-state/FP decline checks\n",checked);return 0;
}
