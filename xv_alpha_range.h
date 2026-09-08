#ifndef XV_ALPHA_RANGE_H
#define XV_ALPHA_RANGE_H
#include <stddef.h>
#include <stdint.h>

/* Conservative integer bounds on uploaded alpha in [0,255]. BC3 interpolated
 * values are bounded by floor/ceil, covering both rounding and exact fractional
 * decoding. Include all uploaded mips and even unused texels of partial blocks.
 * These bounds are diagnostic data, not permission to remove an alpha test. */
typedef struct { unsigned min, max; } xv_alpha_range;
static inline void xv_alpha_range_include(xv_alpha_range *r,unsigned lo,unsigned hi)
{
    if(lo<r->min)r->min=lo;
    if(hi>r->max)r->max=hi;
}
static inline int xv_alpha_range_bc(const uint8_t *p,size_t bytes,unsigned fmt,xv_alpha_range *r)
{
    unsigned block=fmt==0x0c?8:16;
    if(!r || !p || !bytes || bytes%block || (fmt!=0x0c && fmt!=0x0e && fmt!=0x0f))return 0;
    *r=(xv_alpha_range){255,0};
    for(size_t off=0;off<bytes;off+=block) {
        const uint8_t *b=p+off;
        if(fmt==0x0c) {
            unsigned a=b[0]|b[1]<<8,z=b[2]|b[3]<<8;
            if(a>z) xv_alpha_range_include(r,255,255);
            else for(unsigned i=0;i<16;i++) {
                unsigned v=((b[4+i/4]>>((i%4)*2))&3)==3?0:255;
                xv_alpha_range_include(r,v,v);
            }
        } else if(fmt==0x0e) {
            for(unsigned i=0;i<16;i++) {
                unsigned v=((b[i/2]>>((i%2)*4))&15)*17;
                xv_alpha_range_include(r,v,v);
            }
        } else {
            unsigned a=b[0],z=b[1],den=a>z?7:5;
            uint64_t bits=0;
            for(unsigned i=0;i<6;i++)bits|=(uint64_t)b[i+2]<<(8*i);
            for(unsigned i=0;i<16;i++) {
                unsigned code=(bits>>(3*i))&7,n;
                if(code<2)n=(code?z:a)*den;
                else if(a<=z && code>=6)n=code==7?255*den:0;
                else n=(den+1-code)*a+(code-1)*z;
                xv_alpha_range_include(r,n/den,(n+den-1)/den);
            }
        }
        if(!r->min && r->max==255)break;
    }
    return 1;
}
static inline int xv_alpha_range_rgba(const uint32_t *p,unsigned w,unsigned h,unsigned levels,xv_alpha_range *r)
{
    if(!r || !p || !w || !h || !levels || w>4096 || h>4096 || levels>12)return 0;
    *r=(xv_alpha_range){255,0};
    for(unsigned l=0;l<levels;l++) {
        unsigned stride=(w+7u)&~7u;
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++) {
            unsigned a=p[(size_t)y*stride+x]>>24;
            xv_alpha_range_include(r,a,a);
            if(!r->min && r->max==255)return 1;
        }
        p+=(size_t)stride*h;
        w=w>1?w>>1:1;h=h>1?h>>1:1;
    }
    return 1;
}
#endif
