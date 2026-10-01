#ifndef XV_BC_LAYOUT_H
#define XV_BC_LAYOUT_H
#include <stdint.h>
#include <string.h>

static inline unsigned xv_bc_level_bytes(unsigned w,unsigned h,unsigned block)
{
    return ((w+3)/4)*((h+3)/4)*block;
}
static inline unsigned xv_bc_chain_bytes(unsigned w,unsigned h,unsigned levels,unsigned block)
{
    unsigned bytes=0;
    while (levels--) {
        bytes+=xv_bc_level_bytes(w,h,block);
        w=w>1 ? w/2 : 1; h=h>1 ? h/2 : 1;
    }
    return bytes;
}
static inline uint32_t xv_bc_compact(uint32_t x)
{
    x&=0x55555555u;x=(x|(x>>1))&0x33333333u;x=(x|(x>>2))&0x0f0f0f0fu;
    x=(x|(x>>4))&0x00ff00ffu;return (x|(x>>8))&0xffffu;
}
/* Xbox BC blocks are row-major; GXM interleaves Y first, then X, with
 * remaining bits along the long dimension. Ranges own consecutive output
 * blocks, allowing disjoint cache-line-aligned worker writes. */
static inline int xv_bc_reorder_range(uint8_t *dst,const uint8_t *src,unsigned w,
    unsigned h,unsigned block,unsigned first,unsigned last)
{
    unsigned bw=(w+3)/4,bh=(h+3)/4;
    if (!w || !h || (bw&(bw-1)) || (bh&(bh-1)) ||
        (block!=8 && block!=16) || first>last || last>bw*bh) return -1;
    unsigned minimum=bw<bh ? bw : bh, bits=0;
    while ((1u<<bits)<minimum) ++bits;
    for (unsigned i=first;i<last;++i) {
        unsigned x=xv_bc_compact(i>>1)&(minimum-1),y=xv_bc_compact(i)&(minimum-1);
        unsigned upper=(i>>(2*bits))<<bits;
        if (bw>=bh) x|=upper; else y|=upper;
        memcpy(dst+i*block,src+(y*bw+x)*block,block);
    }
    return 0;
}
#endif
