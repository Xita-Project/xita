#ifndef H2_KERNEL_STACK_H
#define H2_KERNEL_STACK_H
#include "xv_x86rt.h"
/* An isolated guest system-address window; these are not host pointers. */
#define H2_STACK_WINDOW_BEGIN 0xD0000000u
#define H2_STACK_WINDOW_END   0xD4000000u
int h2_kernel_stack_unmapped(uint32_t address);
void h2_kernel_stack_fault(xctx *c, const char *reason, uint32_t first, uint32_t second);
void __wrap_xk_MmCreateKernelStack(xctx *c);
void __wrap_xk_MmDeleteKernelStack(xctx *c);
#endif
