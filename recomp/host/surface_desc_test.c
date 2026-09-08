/* Actual Xbox surface-description HLE; guards catch the old PC-layout overrun. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"
uint8_t *g_xram;
xk_thread *xk_cur;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
extern void xv_hle_D3DSurface_GetDesc(xctx *);
extern void xv_hle_Get2DSurfaceDesc(xctx *);
static uint32_t stack, surface, out;
static void call(unsigned level, int helper)
{
    xctx c={0}; c.r[4]=stack;
    X_M32(stack)=0x123456; X_M32(stack+4)=surface;
    X_M32(stack+8)=helper ? level : out; X_M32(stack+12)=out;
    if (helper) xv_hle_Get2DSurfaceDesc(&c); else xv_hle_D3DSurface_GetDesc(&c);
    assert(c.r[4]==stack+(helper?16:12) && X_M32(stack)==0x123456);
}
static void check(unsigned level,int helper,const uint32_t expected[7])
{
    uint8_t guard[40], actual[40]; memset(guard,0xA5,sizeof guard);
    x_guest_write(out-4,guard,sizeof guard);call(level,helper);x_guest_read(actual,out-4,sizeof actual);
    assert(!memcmp(actual,guard,4) && !memcmp(actual+32,guard+32,8));
    assert(!memcmp(actual+4,expected,28));
}
int main(void)
{
    xk_mem_setup(0x10000,0x400000);g_xram=calloc(1,xk_mem_arena_size());assert(g_xram);
    xk_mem_bind_arena();stack=xk_kalloc(64);surface=xk_kalloc(64);out=xk_kalloc(16384)+4;
    /* Swizzled 128x128 A8R8G8B8: Halo reads width at +20, height at +24. */
    X_M32(surface)=0x01050001;X_M32(surface+12)=1u|(2u<<4)|(6u<<8)|(1u<<16)|(7u<<20)|(7u<<24);X_M32(surface+16)=0;
    const uint32_t rgba[]={6,1,1,65536,0x11,128,128};check(0,0,rgba);
    /* A nondefault linear pitch must contribute to byte size. */
    X_M32(surface+12)=1u|(2u<<4)|(0x12u<<8);X_M32(surface+16)=16u|(18u<<12)|(3u<<24);
    const uint32_t linear[]={0x12,1,1,256*19,0x11,17,19};check(0,0,linear);
    X_M32(surface+12)=1u|(2u<<4)|(0x2Eu<<8);
    const uint32_t depth[]={0x2E,1,2,256*19,0x11,17,19};check(0,0,depth);
    /* Get2DSurfaceDesc's mip level is an input, not an unused argument. */
    X_M32(surface)=0x01040001;X_M32(surface+12)=1u|(2u<<4)|(6u<<8)|(8u<<16)|(7u<<20)|(6u<<24);X_M32(surface+16)=0;
    const uint32_t mip[]={6,3,0,32*16*4,0x11,32,16};check(2,1,mip);
    X_M32(surface+12)=(X_M32(surface+12)&~(15u<<24))|(7u<<24)|4u;const uint32_t cube[]={6,5,0,32*32*4,0x11,32,32};check(2,1,cube);
    X_M32(surface+12)=1u|(2u<<4)|(0xCu<<8)|(8u<<16)|(7u<<20)|(6u<<24);
    const uint32_t dxt[]={0xC,3,0,8,0x11,2,1};check(6,1,dxt);
    /* Every alignment across a noncontiguous guest page boundary, with guards. */
    uint32_t page=((out+4095)&~4095u)>>12, saved=g_xpt[page+1];g_xpt[page+1]=g_xpt[page+2];
    for (unsigned split=1;split<28;split++) { out=(page+1)*4096-split;check(6,1,dxt); }
    g_xpt[page+1]=saved;
    free(g_xram);free(g_xpt);
    puts("PASS: Xbox 28-byte surface ABI, guards, viewport dimensions, pitch, usage, multisampling, mip levels, compressed size, and cross-page writes");
}
