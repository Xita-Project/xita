/* xv_x87reg.h - helpers for shards emitted with xita_recomp.py --x87-regs (recompiler/x87_regs.py).
 *
 * In that mode a converted function keeps the x87 slots and the status word in C locals and passes
 * the TOP value it knows statically. These are the local-status-word twins of x87_compare and
 * x87_fxam in xv_x86rt.h: the same condition codes, the same (fsw & ~0x4700) | cc | top << 11
 * update (TOP is OR-ed in, exactly like the memory lowering) and the same fcomi EFLAGS write.
 * xv_x86rt.h is left untouched so the default lowering and its pinned hashes do not move. */
#pragma once
#include "xv_x86rt.h"

/* A call whose x87 effect was only assumed moved c->fsp differently: the function continues in its
 * memory-lowering copy (exact). Counted/logged by xv_x86rt.c when linked; weak so shards link without. */
void xv_x87reg_miss(xctx *c, uint32_t call_ip) __attribute__((weak));

static inline uint16_t x87r_compare(xctx *c, uint16_t fsw, double a, double b, int eflags, uint32_t top)
{
    uint16_t cc;
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 8)
    uint32_t result;
    __asm__ volatile("vcmp.f64 %P1, %P2\n\tvmrs APSR_nzcv, fpscr\n\t"
                     "mov %0, #0\n\tit mi\n\tmovmi %0, #256\n\t"
                     "it eq\n\tmoveq %0, #16384\n\t"
                     "it vs\n\tmovvs %0, #17664"
                     : "=r"(result) : "w"(a), "w"(b) : "cc");
    cc = (uint16_t)result;
#else
    if (isnan(a) || isnan(b)) cc = 0x4500;                          /* C3|C2|C0 */
    else if (a < b)           cc = 0x0100;                          /* C0 */
    else if (a == b)          cc = 0x4000;                          /* C3 */
    else                      cc = 0;
#endif
    fsw = (uint16_t)((fsw & ~0x4700u) | cc | ((top & 7u) << 11));
    if (eflags) {                                                    /* fcomi: ZF=C3 PF=C2 CF=C0 */
        uint32_t f = ((cc & 0x4000) ? 0x40u : 0) | ((cc & 0x0400) ? 0x04u : 0) | ((cc & 0x0100) ? 0x01u : 0);
        xf_set_eflags(c, (xf_eflags(c) & ~0x45u) | f);
    }
    return fsw;
}

static inline uint16_t x87r_fxam(uint16_t fsw, double v)
{
    uint16_t cc;
    if (isnan(v)) cc = 0x0100; else if (isinf(v)) cc = 0x0500; else if (v == 0.0) cc = 0x4000; else cc = 0x0400;
    if (signbit(v)) cc |= 0x0200;
    return (uint16_t)((fsw & ~0x4700u) | cc);
}
