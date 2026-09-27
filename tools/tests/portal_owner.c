/* Reuse the real worker pool and platform fixture; this main adds the adapter. */
#define main clip_startup_fixture_main
#include "clip_region_startup.c"
#undef main
#include "../../recomp/kernel/xk_portal_polygon.h"
#include "../../recomp/kernel/xk_portal_polygon_math.h"

uint32_t xk_mem_arena_size(void) { return ARENA_BYTES; }
static unsigned checked;
static void decline(void)
{
    xctx before=t_guest.ctx;
    unsigned char *ram=malloc(ARENA_BYTES);uint32_t pages[ARENA_BYTES/PAGE_BYTES];
    assert(ram);memcpy(ram,g_xram,ARENA_BYTES);memcpy(pages,g_xpt,sizeof(pages));
    int rounding=fegetround(),exceptions=fetestexcept(FE_ALL_EXCEPT);
    assert(!xv_portal_polygon(&t_guest.ctx));
    assert(!memcmp(&before,&t_guest.ctx,sizeof before));
    assert(!memcmp(ram,g_xram,ARENA_BYTES));assert(!memcmp(pages,g_xpt,sizeof pages));
    assert(rounding==fegetround() && exceptions==fetestexcept(FE_ALL_EXCEPT));
    free(ram);checked++;
}
static void geometry(void)
{
    memset(&t_guest.ctx,0,sizeof(t_guest.ctx));
    memset(g_xram,0xa5,ARENA_BYTES);
    for(unsigned i=0;i<ARENA_BYTES/PAGE_BYTES;i++)g_xpt[i]=i*PAGE_BYTES;
    t_guest.stack_limit=0x88000;t_guest.stack_base=0x95000;
    uint32_t sp=0x90000;
    t_guest.ctx.r[4]=sp;t_guest.ctx.r[1]=4;t_guest.ctx.r[2]=sp+0x4c;t_guest.ctx.preempt=10000;
    const uint32_t args[]={0x534da,4,0x80000,256,sp+0x850,0x38d1b717};
    const xp_point input[]={{-.5,-.5},{.5,-.5},{.5,.5},{-.5,.5}};
    const xp_point boundary[]={{-1,-1},{1,-1},{1,1},{-1,1}};
    x_guest_write(sp,args,sizeof args);x_guest_write(sp+0x4c,input,sizeof input);
    x_guest_write(0x80000,boundary,sizeof boundary);
    X_M32(0x1f0a68)=0;X_M32(0x1f0a78)=0x3f800000;
    X_M64(0x1f0af8)=UINT64_C(0x3f1a36e2e0000000);
}
int main(void)
{
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
    assert(g_xram&&g_xpt);
    t_guest.fiber=(xk_fiber*)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    setenv("XV_OBJECT_JOB_WORKERS","2",1);setenv("XV_NATIVE_CLIP","1",1);setenv("XV_CLIP_REGISTERS","1",1);
    geometry();assert(!xv_portal_polygon((xctx*)(uintptr_t)1));
    assert(initialize());xv_native_clip_region_init();xv_native_clip_region_override(1);
    /* Even with the native owner identity, a copied scene context cannot use
     * the guest-owner contract. A helper alias does not change this check. */
    xctx copied=t_guest.ctx, copied_before=copied;
    unsigned char *copied_ram=malloc(ARENA_BYTES);assert(copied_ram);
    memcpy(copied_ram,g_xram,ARENA_BYTES);
    assert(xv_object_census_boundary(&copied)==XV_LC_CONTEXT);
    assert(!xv_portal_polygon(&copied));
    assert(!memcmp(&copied,&copied_before,sizeof copied));
    assert(!memcmp(copied_ram,g_xram,ARENA_BYTES));free(copied_ram);
    checked++;
    assert(xv_portal_polygon(&t_guest.ctx));assert((int16_t)t_guest.ctx.r[0]==4);
    assert(t_guest.ctx.r[4]==0x90018&&t_guest.ctx.preempt==9981);
    assert(!memcmp(g_xram+0x9004c,g_xram+0x90850,32));
    geometry();count=1;decline();count=0;
    running=1;decline();running=0;
    owner=&t_guest.ctx;decline();owner=NULL;
    pause_workers=1;decline();pause_workers=0;
    owner_notice=1;decline();owner_notice=0;
    xv_watch_n=1;decline();xv_watch_n=0;
    xv_trace_funcs=1;decline();xv_trace_funcs=0;
    xv_light_census_enabled=1;decline();xv_light_census_enabled=0;
    xv_native_clip_region_override(0);decline();xv_native_clip_region_override(1);
    t_guest.ctx.preempt=1029;decline();geometry();
    t_guest.ctx.df=1;decline();geometry();
    X_M32(0x90000)=0x561ea;decline();geometry();
    X_M32(0x9000c)=64;decline();geometry();
    X_M32(0x90014)=0x7f801234;decline();geometry();
    X_M32(0x9004c)=0x7f801234;decline();geometry();
    X_M32(0x9004c)=0x47800001;decline();geometry();
    X_M32(0x1f0a78)=0;decline();geometry();
    X_M32(0x90008)=0x9004c;decline();geometry();
    g_xpt[0x80]=0x8d000;decline();geometry();
    g_xpt[0x8e]=0x8d000;decline();geometry();
    g_xpt[0x80]=ARENA_BYTES-4096;decline();geometry();
    X_M32(0x90008)=0xfffffffc;decline();geometry();
    t_guest.stack_limit=0x90000;decline();geometry();
    t_guest.stack_base=0x90850;decline();geometry();
    xv_object_jobs_shutdown();free(g_xpt);free(g_xram);
    printf("PASS native owner acceptance and %u complete-state decline checks\n",checked);
    return 0;
}
