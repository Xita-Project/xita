#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "xv_material_specialize.h"
int main(void)
{
    uint32_t upload[8*4 + 8*2 + 8];
    for (unsigned i=0; i<sizeof upload/sizeof *upload; ++i) upload[i]=0xffabcdef;
    unsigned off=0;
    for (unsigned w=4; w; w>>=1) {
        for (unsigned y=0;y<w;++y) for(unsigned x=0;x<w;++x)
            upload[off+y*8+x]=(x+y)<<24; /* Arbitrary alpha; poisoned padding. */
        off+=8*w;
    }
    assert(xv_material_black_rgba(upload,4,4,3));
    unsigned starts[]={0,32,48}, widths[]={4,2,1};
    for(unsigned l=0;l<3;++l) for(unsigned y=0;y<widths[l];++y)
        for(unsigned x=0;x<widths[l];++x) for(unsigned bit=0;bit<24;++bit) {
            unsigned i=starts[l]+y*8+x; uint32_t old=upload[i];
            upload[i]|=1u<<bit; assert(!xv_material_black_rgba(upload,4,4,3)); upload[i]=old;
        }
    assert(!xv_material_black_rgba(NULL,4,4,3));
    assert(!xv_material_black_rgba(upload,8,4,3));
    assert(!xv_material_black_rgba(upload,4,4,4));
    assert(!xv_material_black_rgba(upload,4,4,0));
    float psc[18][4]={{0}}; psc[0][0]=psc[8][1]=1;
    assert(xv_material_axis_constants(psc));
    unsigned rows[]={0,8,5};
    for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        float old=psc[rows[i]][j];
        psc[rows[i]][j]=nextafterf(old,2);assert(!xv_material_axis_constants(psc));
        psc[rows[i]][j]=NAN;assert(!xv_material_axis_constants(psc));
        psc[rows[i]][j]=INFINITY;assert(!xv_material_axis_constants(psc));
        psc[rows[i]][j]=old;
    }
    psc[0][3]=NAN; psc[8][3]=INFINITY; /* Unused components impose no condition. */
    assert(xv_material_axis_constants(psc));
    puts("material proofs: all mip RGB samples, padding, alpha, exact constants and nonfinite rejection pass");
}
