/* Sequential platform substitute for ARM consumer comparisons. The actual
 * threaded backend/capture combination is qualified separately on the host. */
#include "../../recomp/kernel/xk_visibility_jobs.h"
extern int xv_visibility_pass(xctx *);
extern void f_00052E10(xctx *);
int xv_visibility_classify_jobs(xctx *c,const xv_visibility_input *input,unsigned n,
                                xs_bounds_result *out)
{
    (void)c;unsigned saved;__asm__ volatile("vmrs %0, fpscr":"=r"(saved)::"memory");
    for(unsigned i=0;i<n;++i)out[i]=xs_bounds(&input[i].frustum,&input[i].box);
    __asm__ volatile("vmsr fpscr, %0"::"r"(saved):"memory");return 1;
}
void test_visibility_pass(xctx *c)
{
    if(!xv_visibility_pass(c))f_00052E10(c);
}
