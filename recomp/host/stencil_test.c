#include <assert.h>
#include <stdio.h>
#include "../../xv_stencil_gxm.h"

static SceGxmStencilFunc func;
static SceGxmStencilOp pass;
static unsigned ref, read_mask, write_mask, calls;
void sceGxmSetFrontStencilFunc(SceGxmContext *c, SceGxmStencilFunc f,
    SceGxmStencilOp a, SceGxmStencilOp b, SceGxmStencilOp p, unsigned char r, unsigned char w)
{ (void)c; (void)a; (void)b; func=f; pass=p; read_mask=r; write_mask=w; ++calls; }
void sceGxmSetBackStencilFunc(SceGxmContext *c, SceGxmStencilFunc f,
    SceGxmStencilOp a, SceGxmStencilOp b, SceGxmStencilOp p, unsigned char r, unsigned char w)
{ (void)c; (void)a; (void)b; assert(f==func && p==pass && r==read_mask && w==write_mask); ++calls; }
void sceGxmSetFrontStencilRef(SceGxmContext *c, unsigned r) { (void)c; ref=r; ++calls; }
void sceGxmSetBackStencilRef(SceGxmContext *c, unsigned r) { (void)c; assert(r==ref); ++calls; }

/* Tiny attachment oracle: assert the emitted GXM state protects a weapon pixel
 * against a closer wall, yet allows the wall at the neighboring unmarked pixel. */
static int pixel(unsigned *stencil)
{
    assert(func==SCE_GXM_STENCIL_FUNC_ALWAYS || func==SCE_GXM_STENCIL_FUNC_EQUAL);
    if (func==SCE_GXM_STENCIL_FUNC_EQUAL && (*stencil&read_mask)!=(ref&read_mask)) return 0;
    if (pass==SCE_GXM_STENCIL_OP_REPLACE) *stencil=(*stencil&~write_mask)|(ref&write_mask);
    else assert(pass==SCE_GXM_STENCIL_OP_KEEP);
    return 1;
}
int main(void)
{
    unsigned gun=255, wall=255;
    xv_stencil_clear(NULL,1,0); assert(pixel(&gun) && pixel(&wall) && gun==0 && wall==0);
    xv_stencil s=xv_stencil_default();
    xv_stencil_method(&s,0x4032c,1); xv_stencil_method(&s,0x40364,0x207);
    xv_stencil_method(&s,0x40378,0x1e01); xv_stencil_method(&s,0x40368,1);
    xv_stencil_method(&s,0x40360,1); xv_stencil_method(&s,0x4036c,1);
    xv_stencil captured=s, next=s;
    xv_stencil_method(&next,0x364,0x202); xv_stencil_method(&next,0x368,0);
    xv_stencil_method(&next,0x360,0); xv_stencil_method(&next,0x378,0x1e00);
    xv_stencil_bind(NULL,&captured); assert(pixel(&gun) && gun==1);
    xv_stencil_bind(NULL,&next); assert(!pixel(&gun) && pixel(&wall) && gun==1 && wall==0);
    xv_stencil_bind(NULL,NULL); assert(pixel(&gun) && gun==1); /* UI ignores and preserves mask. */
    xv_stencil_clear(NULL,0,0); assert(pixel(&gun) && gun==1); /* Color/depth clear preserves stencil. */
    xv_stencil_clear(NULL,1,0); assert(pixel(&gun) && gun==0); /* No stale mask on following frame. */
    xv_stencil_cache cache={0}; unsigned before=calls;
    for(unsigned i=0;i<100;i++) xv_stencil_bind_cached(&cache,NULL,&captured);
    assert(calls==before+4); cache.valid=0;
    xv_stencil_bind_cached(&cache,NULL,&captured); assert(calls==before+8);
    const uint32_t tokens[]={0x1e00,0,0x1e01,0x1e02,0x1e03,0x150a,0x8507,0x8508};
    for(unsigned i=0;i<8;i++)assert(xv_stencil_op(tokens[i])==i);
    assert(!xv_stencil_method(&s,0x344,0));
    puts("PASS: weapon/wall mask, clears, UI isolation, retained snapshots, state cache, NV2A operations");
}
