/* ARM fixture supplies owner/arena boundaries, not an emulated Vita scheduler.
 * Actual backend admission is checked separately with real pthread workers. */
#include "kernel/xk.h"
#include "kernel/xk_light_census.h"
static xk_thread fixture_thread;
xk_thread *xk_cur=&fixture_thread;
unsigned portal_test_decline;
unsigned xv_object_census_boundary(const xctx *c)
{ (void)c;return portal_test_decline; }
uint32_t xk_mem_arena_size(void) { return 8u<<20; }
void portal_fixture_boot(void)
{ fixture_thread.stack_limit=0x740000;fixture_thread.stack_base=0x790000; }
int *__errno(void) { static int e;return &e; }
