#include "menu_texture.c"
#include <assert.h>
#include <stdio.h>

static uint8_t A(uint32_t p){return p>>24;} static uint8_t R(uint32_t p){return (p>>16)&0xFF;}
static uint8_t G(uint32_t p){return (p>>8)&0xFF;} static uint8_t B(uint32_t p){return p&0xFF;}

static void test_dxt1_solid(void)
{
    /* c0=white(0xFFFF) c1=black(0x0000), all indices 0 -> all white */
    uint8_t blk[8] = {0xFF,0xFF, 0x00,0x00, 0,0,0,0};
    uint32_t out[16];
    menu_dxt1_block(blk, out, 4);
    for (unsigned i=0;i<16;++i){ assert(R(out[i])==255&&G(out[i])==255&&B(out[i])==255&&A(out[i])==255); }
}
static void test_dxt1_indices(void)
{
    /* c0=red(0xF800) c1=blue(0x001F); index 1 everywhere (bits=0x55555555) -> blue */
    uint8_t blk[8] = {0x00,0xF8, 0x1F,0x00, 0x55,0x55,0x55,0x55};
    uint32_t out[16];
    menu_dxt1_block(blk, out, 4);
    for (unsigned i=0;i<16;++i){ assert(B(out[i])==255 && R(out[i])==0); }
}
static void test_dxt3_alpha(void)
{
    /* alpha bytes: 0x0F,0x00... -> texel0 alpha=15->255, texel1 alpha=0 */
    uint8_t blk[16];
    for(int i=0;i<8;++i) blk[i]=0;
    blk[0]=0x0F;   /* texel0 nib=0xF, texel1 nib=0x0 */
    blk[8]=0xFF; blk[9]=0xFF; blk[10]=0; blk[11]=0; blk[12]=0;blk[13]=0;blk[14]=0;blk[15]=0; /* white color */
    uint32_t out[16];
    menu_dxt3_block(blk, out, 4);
    assert(A(out[0])==255 && R(out[0])==255);   /* texel0 opaque white */
    assert(A(out[1])==0);                        /* texel1 transparent */
}
int main(void)
{
    test_dxt1_solid();
    test_dxt1_indices();
    test_dxt3_alpha();
    printf("menu_texture_test: all assertions passed\n");
    return 0;
}
