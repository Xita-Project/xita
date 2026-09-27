#define SPRITE_ARM_FIXTURE
#define main fixture_host_main
#include "sprite_stack.c"
#undef main
#include <stddef.h>
static xctx context;
xctx *arm_context_ptr=&context;
xctx *arm_trace_ptr=trace;
unsigned arm_events;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void arm_bind(uint32_t *pages){
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
 static uint32_t live_pages[PAGES];g_xpt=live_pages;
 __asm__ volatile("mcr p15, 0, %0, c13, c0, 2" :: "r"(pages) : "memory");
#else
 g_xpt=pages;
#endif
}
void arm_prepare(unsigned k){context=prepare(k);yields=calls=arm_events=0;}
void arm_original(void){yields=calls=0;original(&context);arm_events=yields+calls;}
void arm_candidate(void){yields=calls=0;candidate(&context);arm_events=yields+calls;}
void test_boot(void){}
