#ifndef XV_NEUTRAL_DETAIL_H
#define XV_NEUTRAL_DETAIL_H
#include <stdint.h>
/* Only a tiny single-level decoded RGBA upload is eligible. Inspect actual
 * texels, excluding padded row bytes. Larger textures/chains keep sampling. */
static inline int xv_neutral_detail_rgba(const uint32_t *pixels,
    unsigned width, unsigned height, unsigned levels)
{
    if (!pixels || !width || !height || width>4 || height>4 || levels!=1) return 0;
    for(unsigned y=0;y<height;y++)
        for(unsigned x=0;x<width;x++)
            if(pixels[y*8+x]!=0xff808080u) return 0;
    return 1;
}
#endif
