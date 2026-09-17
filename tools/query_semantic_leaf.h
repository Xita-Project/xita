#pragma once
/* Private query-only experiment. No guest loads/stores or callbacks occur
 * inside this interval. Input XMM0/XMM1 were populated by the unchanged
 * captured-root guest loads. Reconstruct every final XMM0 lane and XMM2.low;
 * XMM1 and XMM2.high remain unchanged. The caller keeps the original guest
 * distance store immediately after this helper.
 *
 * Let squared differences be (xx, padding, yy, zz). SHUFPS 14 then 57 leave
 * XMM0 = (zz, xx, xx, yy), and the scalar sum is (xx + yy) + zz. Retained
 * GCC emits the first addition as yy + xx; explicit VFP preserves that order
 * even for two distinct NaN payloads. All four subtractions/multiplications
 * remain, including the zero padding lane, preserving exception behavior.
 * No change to x87 precision, TOP, stale slots, flags, or guest GPRs.
 */
static inline __attribute__((always_inline)) void
nq_semantic_vertex_distance(xctx *c)
{
#if defined(__thumb2__) && defined(__ARM_FP) && (__ARM_FP & 4)
    uint32_t fpscr;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(fpscr) :: "memory");
    /* Preserve the original intermediate stores if native traps are enabled. */
    if (!(fpscr & 0x00009f00u)) {
        float xx=c->xmm[0][0], pad=c->xmm[0][1];
        float yy=c->xmm[0][2], zz=c->xmm[0][3], sum;
        __asm__ volatile(
            "vsub.f32 %[xx], %[xx], %[cx]\n\t"
            "vsub.f32 %[pad], %[pad], %[cp]\n\t"
            "vsub.f32 %[yy], %[yy], %[cy]\n\t"
            "vsub.f32 %[zz], %[zz], %[cz]\n\t"
            "vmul.f32 %[xx], %[xx], %[xx]\n\t"
            "vmul.f32 %[pad], %[pad], %[pad]\n\t"
            "vmul.f32 %[yy], %[yy], %[yy]\n\t"
            "vmul.f32 %[zz], %[zz], %[zz]\n\t"
            "vadd.f32 %[sum], %[yy], %[xx]\n\t"
            "vadd.f32 %[sum], %[sum], %[zz]"
            : [xx] "+&t"(xx), [pad] "+&t"(pad),
              [yy] "+&t"(yy), [zz] "+&t"(zz), [sum] "=&t"(sum)
            : [cx] "t"(c->xmm[1][0]), [cp] "t"(c->xmm[1][1]),
              [cy] "t"(c->xmm[1][2]), [cz] "t"(c->xmm[1][3]));
        c->xmm[0][0]=zz; c->xmm[0][1]=xx;
        c->xmm[0][2]=xx; c->xmm[0][3]=yy;
        c->xmm[2][0]=sum;
        return;
    }
#endif
    for(unsigned i=0;i<4;i++)c->xmm[0][i]-=c->xmm[1][i];
    for(unsigned i=0;i<4;i++)c->xmm[0][i]*=c->xmm[0][i];
    c->xmm[2][0]=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],14);
    c->xmm[2][0]+=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],57);
    c->xmm[2][0]+=c->xmm[0][0];
}
