/* Production scene identity, generation and bounds, with actual pthread IDs. */
#include "../../recomp/kernel/xk_scene_thread.c"
#include <assert.h>
static void *foreign(void *p)
{ (void)p;uint32_t lo=3,hi=4;assert(!xv_scene_thread_stack_bounds(&ctx,&lo,&hi));assert(lo==3&&hi==4);return NULL; }
int main(void)
{
    helper=pthread_self();helper_valid=1;depth=1;overlap=1;scene_stack=0x100000;cache_generation=1;
    uint32_t lo,hi;assert(xv_scene_thread_stack_bounds(&ctx,&lo,&hi));
    assert(lo==scene_stack&&hi==scene_stack+SCENE_STACK_BYTES);
    assert(xv_scene_thread_context_generation(&ctx)==1);
    cache_generation=UINT32_MAX;assert(!xv_scene_thread_context_generation(&ctx));
    assert(!xv_scene_thread_stack_bounds(NULL,&lo,&hi));
    depth=0;assert(!xv_scene_thread_stack_bounds(&ctx,&lo,&hi));depth=1;
    overlap=0;assert(!xv_scene_thread_stack_bounds(&ctx,&lo,&hi));overlap=1;
    scene_stack=UINT32_MAX-100;assert(!xv_scene_thread_stack_bounds(&ctx,&lo,&hi));scene_stack=0x100000;
    pthread_t t;assert(!pthread_create(&t,NULL,foreign,NULL));assert(!pthread_join(t,NULL));
    puts("PASS production scene helper stack/identity/generation guards");return 0;
}
