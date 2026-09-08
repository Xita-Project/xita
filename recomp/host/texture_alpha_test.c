#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_texture_alpha.h"
#include "../../runtime/xv_texture_decode.h"

static void compare(const uint8_t b[16], unsigned fmt)
{
    uint32_t decoded[16]; ui_dxt(b,fmt,decoded);
    int opaque=1;
    for(unsigned i=0;i<16;i++)opaque &= decoded[i]>>24 == 255;
    assert(xv_alpha_bc_opaque(b,fmt==0x0c?8:16,fmt)==opaque);
}
int main(void)
{
    uint8_t b[16]; unsigned cases=0;
    for(unsigned a=0;a<256;a++)for(unsigned z=0;z<256;z++)for(unsigned code=0;code<8;code++) {
        memset(b,0,sizeof b);b[0]=a;b[1]=z;uint64_t bits=0;
        for(unsigned i=0;i<16;i++)bits|=(uint64_t)code<<(3*i);
        for(unsigned i=0;i<6;i++)b[2+i]=(uint8_t)(bits>>(8*i));
        compare(b,0x0f);cases++;
    }
    for(unsigned byte=0;byte<8;byte++)for(unsigned v=0;v<256;v++) {
        memset(b,255,sizeof b);b[byte]=v;compare(b,0x0e);cases++;
    }
    uint32_t rng=0x51618;
    for(unsigned n=0;n<50000;n++) {
        for(unsigned i=0;i<16;i++){rng=rng*1664525u+1013904223u;b[i]=rng>>24;}
        compare(b,0x0c);cases++;
        b[2]=b[0];b[3]=b[1];compare(b,0x0c);cases++;
    }
    assert(!xv_alpha_bc_opaque(NULL,8,0x0c));assert(!xv_alpha_bc_opaque(b,0,0x0c));
    assert(!xv_alpha_bc_opaque(b,7,0x0c));assert(!xv_alpha_bc_opaque(b,16,0x0d));
    /* Every block, including a lower mip, contributes to the proof. */
    uint8_t chain[24]={0};assert(xv_alpha_bc_opaque(chain,sizeof chain,0x0c));
    chain[20]=3;assert(!xv_alpha_bc_opaque(chain,sizeof chain,0x0c));
    uint32_t pixels[56]={0};
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)pixels[y*8+x]=0xff123456;
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)pixels[32+y*8+x]=0xffabcdef;
    pixels[48]=0xffffffff;
    assert(xv_alpha_rgba_opaque(pixels,4,4,3));
    for(unsigned i=0;i<56;i++)if(pixels[i]>>24==255) {
        uint32_t old=pixels[i];pixels[i]&=0xffffff;
        assert(!xv_alpha_rgba_opaque(pixels,4,4,3));pixels[i]=old;
    }
    assert(!xv_alpha_rgba_opaque(NULL,4,4,3));assert(!xv_alpha_rgba_opaque(pixels,0,4,3));
    /* Compare against the actual shader predicate, including its equality epsilon. */
    for(unsigned enable=0;enable<2;enable++)for(unsigned f=0;f<8;f++)for(unsigned ref=0;ref<256;ref++) {
        float a=1,r=ref/255.0f;
        int pass=!enable || f>6.5f || (f>3.5f&&f<4.5f&&a>r) || (f>5.5f&&f<6.5f&&a>=r)
            || (f>0.5f&&f<1.5f&&a<r) || (f>2.5f&&f<3.5f&&a<=r)
            || (f>1.5f&&f<2.5f&&fabsf(a-r)<.002f) || (f>4.5f&&f<5.5f&&fabsf(a-r)>=.002f);
        assert(xv_alpha_accepts_opaque((enable<<16)|(f<<8)|ref)==pass);
    }
    printf("PASS: %u BC blocks against decoded alpha; all BC3 endpoints/selectors, mip tails, row padding and 4096 alpha predicates\n",cases);
}
