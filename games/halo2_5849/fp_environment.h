#pragma once
#include "xv_x86rt.h"
/* Native FP environment used by H2's direct-C SSE execution. This is not a
 * separately virtualized x87/SSE exception history. Only masked, nearest,
 * gradual-underflow mode is supported; other controls stop explicitly. */
uint32_t h2_platform_fpscr_read(void);
void h2_platform_fpscr_write(uint32_t value);
void h2_fp_environment_fault(xctx *, uint32_t ip, uint32_t address, uint32_t value);
void h2_stmxcsr(xctx *, uint32_t ip, uint32_t address);
void h2_ldmxcsr(xctx *, uint32_t ip, uint32_t address);
