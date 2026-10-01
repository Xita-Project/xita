/* Conservative screen rejection for Halo VS56's four affine position rows.
 * Only x/y planes are tested. Near-plane crossings, nonfinite arithmetic, and
 * uncertain boundary cases retain the original GPU draw. */
#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

static inline int xv_flare_quad_outside(const void *vertices, unsigned stride,
                                        const float (*rows)[4])
{
    unsigned common=15u;
    for (unsigned i=0;i<4;i++) {
        float p[3], clip[3], bound[3];
        memcpy(p,(const uint8_t *)vertices+i*stride,sizeof p);
        for (unsigned j=0;j<3;j++) {
            const float *r=rows[j==2 ? 3 : j];
            float a=p[0]*r[0], b=p[1]*r[1], c=p[2]*r[2];
            clip[j]=(a+b)+(c+r[3]);
            bound[j]=fabsf(a)+fabsf(b)+fabsf(c)+fabsf(r[3]);
            if (!isfinite(clip[j]) || !isfinite(bound[j])) return 0;
        }
        float w=clip[2];
        if (w<=0) return 0;
        /* Leave room for GPU dot-product rounding, including cancellation. */
        float mx=0.0002f*(bound[0]+bound[2]+1.0f);
        float my=0.0002f*(bound[1]+bound[2]+1.0f);
        unsigned code=(clip[0]>w+mx ? 1u : 0u) | (clip[0]<-w-mx ? 2u : 0u) |
                      (clip[1]>w+my ? 4u : 0u) | (clip[1]<-w-my ? 8u : 0u);
        common&=code;
        if (!common) return 0;
    }
    return common!=0;
}
