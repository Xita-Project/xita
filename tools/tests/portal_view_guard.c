/* Exercise the production admission function with distinct live/helper tables.
 * Other render-view machinery is discarded by --gc-sections. */
#include "../../recomp/kernel/xk_render_view.c"
#include <assert.h>
#include <pthread.h>
uint32_t *g_xpt;
__thread uint32_t *xv_host_page_table;
static pthread_t helper_tid;
static int helper_context;
int xv_scene_thread_owns_context(const void *c)
{ return pthread_equal(pthread_self(),helper_tid) && c==&helper_context; }
static void *foreign(void *p)
{ (void)p;assert(!xv_render_view_owns_scene_context(&helper_context));return NULL; }
int main(void)
{
    helper_tid=pthread_self();g_xpt=calloc(1u<<20,4);rt=calloc(1,sizeof *rt);assert(g_xpt&&rt);
    ready=bound=thread_mode=1;xv_host_page_table=rt->entries;
    assert(xv_render_view_owns_scene_context(&helper_context));
    assert(!xv_render_view_owns_scene_context(NULL));
    ready=0;assert(!xv_render_view_owns_scene_context(&helper_context));ready=1;
    bound=0;assert(!xv_render_view_owns_scene_context(&helper_context));bound=1;
    thread_mode=0;assert(!xv_render_view_owns_scene_context(&helper_context));thread_mode=1;
    xv_host_page_table=g_xpt;assert(!xv_render_view_owns_scene_context(&helper_context));
    xv_host_page_table=rt->entries;
    pthread_t t;assert(!pthread_create(&t,NULL,foreign,NULL));assert(!pthread_join(t,NULL));
    free(rt);free(g_xpt);puts("PASS production render-view admission with separate thread table");return 0;
}
