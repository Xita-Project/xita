#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_alpha_range.h"
#include "../../runtime/xv_texture_decode.h"

int main(void)
{
    uint8_t b[16];unsigned cases=0;
    for(unsigned a=0;a<256;a++)for(unsigned z=0;z<256;z++)for(unsigned code=0;code<8;code++) {
        memset(b,0,sizeof b);b[0]=a;b[1]=z;uint64_t bits=0;
        for(unsigned i=0;i<16;i++)bits|=(uint64_t)code<<(3*i);
        for(unsigned i=0;i<6;i++)b[2+i]=bits>>(8*i);
        double v;
        if(code<2)v=code?z:a;
        else if(a>z)v=((8-code)*a+(code-1)*z)/7.0;
        else if(code>=6)v=code==7?255:0;
        else v=((6-code)*a+(code-1)*z)/5.0;
        xv_alpha_range r;assert(xv_alpha_range_bc(b,16,0x0f,&r));
        assert(r.min==(unsigned)floor(v) && r.max==(unsigned)ceil(v));
        uint32_t decoded[16];ui_dxt(b,0x0f,decoded);
        for(unsigned i=0;i<16;i++)assert((decoded[i]>>24)>=r.min && (decoded[i]>>24)<=r.max);
        cases++;
    }
    uint32_t seed=0x196;uint8_t chain[48];
    for(unsigned fmt=0x0c;fmt<=0x0f;fmt++)if(fmt!=0x0d)
        for(unsigned n=0;n<40000;n++) {
            for(unsigned i=0;i<48;i++){seed=seed*1664525u+1013904223u;chain[i]=seed>>24;}
            xv_alpha_range r;assert(xv_alpha_range_bc(chain,48,fmt,&r));
            unsigned lo=255,hi=0,block=fmt==0x0c?8:16;
            for(unsigned off=0;off<48;off+=block){uint32_t pixels[16];ui_dxt(chain+off,fmt,pixels);
                for(unsigned i=0;i<16;i++){unsigned a=pixels[i]>>24;if(a<lo)lo=a;if(a>hi)hi=a;}}
            assert(r.min<=lo && r.max>=hi);
            if(fmt!=0x0f)assert(r.min==lo && r.max==hi);
            cases++;
        }
    uint32_t pixels[56]={0};
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)pixels[y*8+x]=0x70123456;
    for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)pixels[32+y*8+x]=0x80abcdef;
    pixels[48]=0x91ffffff;
    xv_alpha_range r;assert(xv_alpha_range_rgba(pixels,4,4,3,&r));assert(r.min==0x70&&r.max==0x91);
    for(unsigned i=0;i<56;i++)if(pixels[i]>>24){uint32_t old=pixels[i];pixels[i]&=0xffffff;
        assert(xv_alpha_range_rgba(pixels,4,4,3,&r)&&r.min==0);pixels[i]=old;}
    assert(!xv_alpha_range_rgba(NULL,4,4,3,&r));assert(!xv_alpha_range_rgba(pixels,0,4,3,&r));
    assert(!xv_alpha_range_rgba(pixels,4,4,0,&r));assert(!xv_alpha_range_rgba(pixels,4,4,3,NULL));
    assert(!xv_alpha_range_bc(b,7,0x0c,&r));assert(!xv_alpha_range_bc(b,16,0x0d,&r));
    assert(!xv_alpha_range_bc(NULL,16,0x0f,&r));assert(!xv_alpha_range_bc(b,16,0x0f,NULL));
    printf("PASS: %u BC interval cases cover rational and decoded alpha; all BC3 endpoints/selectors, block chains, RGBA mip tails and row padding\n",cases);
}
