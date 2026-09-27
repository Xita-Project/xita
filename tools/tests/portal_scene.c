/* Real portal adapter/math + worker backend; scene identity/binding doubles. */
#define PORTAL_OWNER_MAIN portal_owner_fixture_main
#include "portal_owner.c"
#undef PORTAL_OWNER_MAIN
__thread uint32_t *xv_host_page_table;
static xctx scene_ctx;
static pthread_t scene_tid;
static unsigned scene_generation=1,scene_view=1,scene_stack_valid=1;
int xv_scene_thread_owns_context(const void *c)
{ return pthread_equal(pthread_self(),scene_tid) && c==&scene_ctx; }
uint32_t xv_scene_thread_context_generation(const void *c)
{ return xv_scene_thread_owns_context(c)?scene_generation:0; }
int xv_scene_thread_stack_bounds(const void *c,uint32_t *lo,uint32_t *hi)
{ if(!xv_scene_thread_owns_context(c)||!scene_stack_valid)return 0;*lo=0x88000;*hi=0x95000;return 1; }
int xv_render_view_owns_scene_context(const void *c)
{ return xv_scene_thread_owns_context(c)&&scene_view; }
static void helper_decline(void)
{
    xctx before=scene_ctx;unsigned char *ram=malloc(ARENA_BYTES);assert(ram);
    memcpy(ram,g_xram,ARENA_BYTES);int round=fegetround(),fp=fetestexcept(FE_ALL_EXCEPT);
    assert(!xv_portal_polygon(&scene_ctx));assert(!memcmp(&before,&scene_ctx,sizeof before));
    assert(!memcmp(ram,g_xram,ARENA_BYTES));assert(round==fegetround()&&fp==fetestexcept(FE_ALL_EXCEPT));free(ram);
}
static void *foreign(void *unused)
{ (void)unused;xv_host_page_table=g_xpt;helper_decline();return NULL; }
int main(int argc,char **argv)
{
    (void)argv;scene_tid=pthread_self();setenv("XV_SCENE_PORTAL",argc>1?"0":"1",1);
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=calloc(1<<20,4);assert(g_xram&&g_xpt);
    xv_host_page_table=g_xpt;
    t_guest.fiber=(xk_fiber*)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    setenv("XV_OBJECT_JOB_WORKERS","2",1);setenv("XV_NATIVE_CLIP","1",1);setenv("XV_CLIP_REGISTERS","1",1);
    geometry();scene_ctx=t_guest.ctx;
    if(argc>1){helper_decline();puts("PASS scene portal disabled preserves state");return 0;}
    /* Helper path does not need, initialize or enable the owner controller. */
    assert(!xv_native_clip_region_available());assert(xv_portal_polygon(&scene_ctx));
    assert(!xv_native_clip_region_available());assert((int16_t)scene_ctx.r[0]==4);
    assert(scene_ctx.r[4]==0x90018&&scene_ctx.preempt==9981);
    assert(!memcmp(g_xram+0x9004c,g_xram+0x90850,32));
    geometry();scene_ctx=t_guest.ctx;
    t_benchmark=1;helper_decline();t_benchmark=0;
    scene_generation=0;helper_decline();scene_generation=1;
    scene_view=0;helper_decline();scene_view=1;
    scene_stack_valid=0;helper_decline();scene_stack_valid=1;
    scene_ctx.preempt=1029;helper_decline();scene_ctx=t_guest.ctx;
    scene_ctx.df=1;helper_decline();scene_ctx=t_guest.ctx;
    X_M32(0x90000)=0;helper_decline();geometry();scene_ctx=t_guest.ctx;
    X_M32(0x9004c)=0x7f801234;helper_decline();geometry();scene_ctx=t_guest.ctx;
    g_xpt[0x80]=0x8d000;helper_decline();geometry();scene_ctx=t_guest.ctx;
    /* The helper table, not the global live table, governs validation. */
    uint32_t *view=malloc((1u<<20)*4u);assert(view);
    memcpy(view,g_xpt,(1u<<20)*4u);xv_host_page_table=view;
    view[0x80]=0x8d000;helper_decline(); /* alias exists only in helper */
    view[0x80]=0x80000;g_xpt[0x80]=0x8d000;
    assert(xv_portal_polygon(&scene_ctx)); /* live alias must not reject view */
    xv_host_page_table=g_xpt;free(view);geometry();scene_ctx=t_guest.ctx;
    assert(initialize());
    pthread_t other;assert(!pthread_create(&other,NULL,foreign,NULL));assert(!pthread_join(other,NULL));
    /* Compare all live adapter outputs and arena against the old owner path. */
    xv_native_clip_region_init();xv_native_clip_region_override(1);
    for(unsigned i=0;i<64;i++) {
        geometry();X_M32(0x9004c)=0xbf000000u+i*8192u;scene_ctx=t_guest.ctx;
        unsigned char *initial=malloc(ARENA_BYTES),*expected=malloc(ARENA_BYTES);assert(initial&&expected);
        fenv_t fp;assert(!fegetenv(&fp));
        memcpy(initial,g_xram,ARENA_BYTES);assert(xv_portal_polygon(&t_guest.ctx));
        int expected_round=fegetround(),expected_flags=fetestexcept(FE_ALL_EXCEPT);
        xctx expected_ctx=t_guest.ctx;memcpy(expected,g_xram,ARENA_BYTES);
        memcpy(g_xram,initial,ARENA_BYTES);assert(!fesetenv(&fp));assert(xv_portal_polygon(&scene_ctx));
        assert(expected_round==fegetround()&&expected_flags==fetestexcept(FE_ALL_EXCEPT));
        assert(!memcmp(&scene_ctx,&expected_ctx,sizeof scene_ctx));assert(!memcmp(expected,g_xram,ARENA_BYTES));
        free(initial);free(expected);
    }
    xv_object_jobs_shutdown();free(g_xpt);free(g_xram);
    puts("PASS scene portal ownership declines and 64 owner/helper output equivalence cases");return 0;
}
