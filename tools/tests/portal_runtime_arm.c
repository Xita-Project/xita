/* ARM fixture supplies owner/arena boundaries, not an emulated Vita scheduler.
 * Actual backend admission is checked separately with real pthread workers. */
#include "kernel/xk.h"
#include "kernel/xk_light_census.h"
static xk_thread fixture_thread;
xk_thread *xk_cur=&fixture_thread;
unsigned portal_test_decline;
unsigned xv_object_census_boundary(const xctx *c)
{
 (void)c;
#ifdef XV_PORTAL_SCENE_FIXTURE
 return XV_LC_CONTEXT; /* A copied helper context is never the owner. */
#else
 return portal_test_decline;
#endif
}
uint32_t xk_mem_arena_size(void) { return 8u<<20; }
void portal_fixture_boot(void)
{ fixture_thread.stack_limit=0x740000;fixture_thread.stack_base=0x790000; }
int *__errno(void) { static int e;return &e; }

#ifdef XV_PORTAL_SCENE_FIXTURE
/* Platform doubles only: actual generated caller, adapter and math execute. */
void *portal_test_context;
int xv_owner_thread_id(void){return 17;}
void xv_scene_phase_begin(uint32_t a){(void)a;}
void xv_scene_phase_end(uint32_t a){(void)a;}
void xv_object_clip_release(xctx *c,int *guard){(void)c;(void)guard;}
int xv_scene_thread_owns_context(const void *c){return c==portal_test_context;}
uint32_t xv_scene_thread_context_generation(const void *c)
{return c==portal_test_context&&!portal_test_decline?1:0;}
int xv_scene_thread_stack_bounds(const void *c,uint32_t *lo,uint32_t *hi)
{if(c!=portal_test_context)return 0;*lo=0x740000;*hi=0x790000;return 1;}
int xv_render_view_owns_scene_context(const void *c){return c==portal_test_context;}
int xv_benchmark_active(void){return 0;}
#endif
