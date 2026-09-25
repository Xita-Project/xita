#ifndef XV_CLIP_DIAGNOSTIC_H
#define XV_CLIP_DIAGNOSTIC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
/* Diagnostic only, never draw admission. Caller proves F32 xyz layout, shader
 * position equations, immutable capture bounds, and four constant rows.
 * Only x/y planes: depth convention, depth clamp and viewport remain separate.
 * A common outside plane is sufficient; absence is NOT proof of visibility. */
static unsigned xv_clip_diagnostic(const void *vertices, size_t bytes,
    size_t stride, size_t offset, const void *indices, size_t index_bytes,
    size_t count, const float rows[4][4])
{
    if (!vertices || !indices || !rows || !count || stride < 12 ||
        offset > stride-12 || count > index_bytes/2) return 0;
    unsigned common=15;
    for(size_t i=0;i<count;i++) {
        uint16_t index; float xyz[3];
        memcpy(&index,(const unsigned char*)indices+2*i,2);
        if ((size_t)index > SIZE_MAX/stride) return 0;
        size_t start=(size_t)index*stride;
        if(start>bytes || offset>bytes-start || bytes-start-offset<12) return 0;
        memcpy(xyz,(const unsigned char*)vertices+start+offset,12);
        double q[3],err[3]; const unsigned r[3]={0,1,3};
        for(unsigned k=0;k<3;k++) {
            double v=rows[r[k]][3],mag=fabs(v);
            if(!isfinite(v))return 0;
            for(unsigned a=0;a<3;a++) {
                if(!isfinite(xyz[a]) || !isfinite(rows[r[k]][a]))return 0;
                double t=(double)xyz[a]*rows[r[k]][a]; v+=t;mag+=fabs(t);
            }
            /* Deliberately broad margin; not a certified SGX error bound. */
            if(!isfinite(v) || mag>1e30)return 0;
            q[k]=v;err[k]=1e-5*(1+mag);
        }
        unsigned mask=0;
        if(q[0]+q[2]<-(err[0]+err[2]))mask|=1;
        if(q[2]-q[0]<-(err[0]+err[2]))mask|=2;
        if(q[1]+q[2]<-(err[1]+err[2]))mask|=4;
        if(q[2]-q[1]<-(err[1]+err[2]))mask|=8;
        common &= mask;
        if(!common)return 0;
    }
    return common;
}
#endif
