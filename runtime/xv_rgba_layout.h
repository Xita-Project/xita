#ifndef XV_RGBA_LAYOUT_H
#define XV_RGBA_LAYOUT_H
#include <stdint.h>

/* Uncompressed GXM swizzled mips are tightly packed. Y occupies the low
 * interleaved bit; the longer axis continues above the interleaved square.
 * Input uses GXM linear's eight-texel row alignment at every mip level. */
static inline uint32_t xv_rgba_swizzled_bytes(unsigned w, unsigned h, unsigned levels)
{
    if (!w || !h || w > 4096 || h > 4096 || (w & (w-1)) ||
        (h & (h-1)) || !levels || levels > 13) return 0;
    uint32_t bytes=0;
    for (unsigned l=0;l<levels;l++) {
        bytes += w*h*4u;
        if (l+1<levels && (w==1 || h==1)) return 0;
        w>>=1; h>>=1;
    }
    return bytes;
}
static inline unsigned xv_rgba_compact_bits(unsigned v)
{
    v &= 0x55555555u;
    v = (v | (v>>1)) & 0x33333333u;
    v = (v | (v>>2)) & 0x0f0f0f0fu;
    v = (v | (v>>4)) & 0x00ff00ffu;
    return (v | (v>>8)) & 0x0000ffffu;
}
/* Nonoverlapping, word-aligned buffers with lengths given by their layouts. */
static inline void xv_rgba_swizzle(const uint32_t *linear, uint32_t *gpu,
                                   unsigned w, unsigned h, unsigned levels)
{
    for (unsigned l=0;l<levels;l++,w>>=1,h>>=1) {
        unsigned short_axis=w<h?w:h, bits=0;
        for (unsigned t=short_axis;t>1;t>>=1) bits++;
        unsigned mask=short_axis-1, stride=(w+7u)&~7u;
        for (unsigned i=0;i<w*h;i++) {
            unsigned x=xv_rgba_compact_bits(i>>1)&mask;
            unsigned y=xv_rgba_compact_bits(i)&mask;
            unsigned upper=(i>>(bits*2))<<bits;
            if (w>=h) x|=upper; else y|=upper;
            gpu[i]=linear[y*stride+x];
        }
        gpu+=w*h; linear+=stride*h;
    }
}
#endif
