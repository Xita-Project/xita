/* Exercise the production upload/cache, plus an independent coordinate-to-
 * Morton reference (encoder, whereas production iterates output addresses). */
#include <stdlib.h>
static int fail_scratch;
static void *texture_malloc(size_t n) { return fail_scratch ? NULL : malloc(n); }
#define malloc texture_malloc
#define main original_cache_tests_main
#include "texture_cache_test.c"
#undef main
#undef malloc

static unsigned reference_address(unsigned x,unsigned y,unsigned w,unsigned h)
{
    unsigned index=0,position=0;
    for(unsigned bit=1;bit<w || bit<h;bit<<=1) {
        if(bit<h) { if(y&bit) index|=1u<<position;position++; }
        if(bit<w) { if(x&bit) index|=1u<<position;position++; }
    }
    return index;
}
static void compare(const uint32_t *linear,const uint32_t *swizzled,unsigned w,unsigned h,unsigned levels)
{
    unsigned bytes=0;
    for(unsigned l=0;l<levels;l++,w>>=1,h>>=1) {
        unsigned stride=(w+7u)&~7u;
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)
            assert(linear[y*stride+x]==swizzled[reference_address(x,y,w,h)]);
        linear+=stride*h;swizzled+=w*h;bytes+=w*h*4;
    }
    assert(bytes);
}
static void cache_layout_case(unsigned logw,unsigned logh,int chain,int coverage,unsigned fmt)
{
    memset(&g,0,sizeof g);memset(g.texhash,0xff,sizeof g.texhash);
    g.dec_cap=2u<<20;g.dec_base=malloc(g.dec_cap);assert(g.dec_base);memset(g.dec_base,0xa5,g.dec_cap);
    unsigned w=1u<<logw,h=1u<<logh,levels=chain?(logw<logh?logw:logh)+1:1;
    unsigned hdr=0x1000,data=0x10000;
    X_M32(hdr+4)=data;X_M32(hdr+12)=(fmt<<8)|(logw<<20)|(logh<<24)|((chain?2:1)<<16);X_M32(hdr+16)=0;
    uint32_t seed=0x5923a12;
    for(unsigned i=0;i<w*h*4;i++){seed=seed*1664525u+1013904223u;((uint8_t *)X_G(data))[i]=seed>>24;}
    xv_ui_gxm_rgba_layout_override(0);
    const SceGxmTexture *a=ui_texture_for(hdr,coverage);assert(a && upload_layout==SCE_GXM_TEXTURE_LINEAR);
    assert(upload_mips==levels);
    unsigned before=g.dec_off,start_uploads=uploads;
    uint8_t *snapshot=malloc(before);assert(snapshot);memcpy(snapshot,g.dec_base,before);
    int opaque=xv_ui_gxm_texture_opaque(a);
    xv_ui_gxm_rgba_layout_override(1);
    const SceGxmTexture *b=ui_texture_for(hdr,coverage);assert(b&&b!=a&&upload_layout==SCE_GXM_TEXTURE_SWIZZLED);
    assert(upload_mips==levels && test_mip==SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
    assert(xv_ui_gxm_texture_opaque(b)==opaque);
    unsigned bytes=xv_rgba_swizzled_bytes(w,h,levels);assert(bytes);
    assert(g.dec_off-before==ALIGN_UP(bytes,64));
    assert(g.dec_base[g.dec_off]==0xa5 && !memcmp(snapshot,g.dec_base,before));
    compare(sceGxmTextureGetData(a),sceGxmTextureGetData(b),w,h,levels);
    for(unsigned i=0;i<20;i++){
        xv_ui_gxm_rgba_layout_override(i%2);
        assert(ui_texture_for(hdr,coverage)==(i%2?b:a));assert(uploads==start_uploads+1);
    }
    /* Both layout identities must be invalidated by writes, without changing
     * memory referenced by an earlier frame's copied descriptor. */
    SceGxmTexture old=*b;unsigned old_size=g.dec_off;
    uint8_t *all=malloc(old_size);assert(all);memcpy(all,g.dec_base,old_size);
    ((uint8_t *)X_G(data))[0]^=0xff;xv_ui_gxm_invalidate_range(data,1);g.rec_frame++;
    assert(ui_texture_for(hdr,coverage)==b && sceGxmTextureGetData(b)!=sceGxmTextureGetData(&old));
    xv_ui_gxm_rgba_layout_override(0);assert(ui_texture_for(hdr,coverage)==a);
    compare(sceGxmTextureGetData(a),sceGxmTextureGetData(b),w,h,levels);
    assert(!memcmp(all,g.dec_base,old_size));
    free(all);free(snapshot);free(g.dec_base);
}
static void large_layout_case(unsigned w,unsigned h,unsigned levels)
{
    unsigned linear_bytes=0,swizzled_bytes=xv_rgba_swizzled_bytes(w,h,levels);
    for(unsigned l=0,x=w,y=h;l<levels;l++,x>>=1,y>>=1)linear_bytes+=((x+7u)&~7u)*y*4u;
    assert(swizzled_bytes);
    uint32_t *a=malloc(linear_bytes),*b=malloc(swizzled_bytes+4);assert(a&&b);
    for(unsigned i=0;i<linear_bytes/4;i++)a[i]=i*2654435761u;
    b[swizzled_bytes/4]=0x1234abcd;
    xv_rgba_swizzle(a,b,w,h,levels);compare(a,b,w,h,levels);
    assert(b[swizzled_bytes/4]==0x1234abcd);free(a);free(b);
}
static void failures_and_exclusions(void)
{
    memset(&g,0,sizeof g);memset(g.texhash,0xff,sizeof g.texhash);
    g.dec_cap=8192;g.dec_base=malloc(g.dec_cap);assert(g.dec_base);memset(g.dec_base,0x61,g.dec_cap);
    unsigned hdr=0x1000,data=0x10000;
    X_M32(hdr+4)=data;X_M32(hdr+12)=(0x06<<8)|(3<<20)|(3<<24)|(1<<16);X_M32(hdr+16)=0;
    memset(X_G(data),255,1024);xv_ui_gxm_rgba_layout_override(1);fail_scratch=1;
    const SceGxmTexture *t=ui_texture_for(hdr,0);assert(t&&upload_layout==SCE_GXM_TEXTURE_LINEAR);
    unsigned start=uploads;fail_scratch=0;assert(ui_texture_for(hdr,0)==t&&uploads==start);
    /* Allocation fallback is cached for this version, so memory pressure does
     * not trigger an allocation attempt at every draw. */
    xv_ui_gxm_rgba_layout_override(0);g.dec_cap=g.dec_off;
    assert(!ui_texture_for(hdr,0)&&g.tex_purge);assert(ui_texture_for(hdr,0)==NULL);
    assert(xv_ui_gxm_texture_opaque(t));
    g.dec_cap=8192;g.tex_purge=0;
    X_M32(hdr+4)=data+1024;X_M32(hdr+12)=(0x12<<8)|(1<<16);X_M32(hdr+16)=7u|(4u<<12); /* 8x5, row pitch 64 */
    memset(X_G(data+1024),0x12,320);
    xv_ui_gxm_rgba_layout_override(1);t=ui_texture_for(hdr,0);assert(t&&upload_layout==SCE_GXM_TEXTURE_LINEAR);
    start=uploads;xv_ui_gxm_rgba_layout_override(0);assert(ui_texture_for(hdr,0)==t&&uploads==start);
    X_M32(hdr+4)=data+2048;X_M32(hdr+12)=(0x0c<<8)|(3<<20)|(3<<24)|(1<<16);X_M32(hdr+16)=0;
    t=ui_texture_for(hdr,0);assert(t&&upload_layout==SCE_GXM_TEXTURE_SWIZZLED);
    start=uploads;xv_ui_gxm_rgba_layout_override(1);assert(ui_texture_for(hdr,0)==t&&uploads==start);
    free(g.dec_base);
}
int main(void)
{
    xv_ui_gxm_rgba_layout_override(0);original_cache_tests_main();
    xv_ui_gxm_rgba_layout_override(1);original_cache_tests_main();
    assert(!xv_rgba_swizzled_bytes(0,4,1));assert(!xv_rgba_swizzled_bytes(3,4,1));
    assert(!xv_rgba_swizzled_bytes(8192,4,1));assert(!xv_rgba_swizzled_bytes(4,4,4));
    assert(!xv_rgba_swizzled_bytes(4,2,3));assert(!xv_rgba_swizzled_bytes(4,4,0));
    g_xram=calloc(1,8u<<20);g_xpt=calloc(1u<<20,sizeof *g_xpt);assert(g_xram&&g_xpt);
    for(unsigned i=0;i<(8u<<20)/4096;i++)g_xpt[i]=g_xpt[0x80000+i]=i*4096;
    for(unsigned x=0;x<=8;x++)for(unsigned y=0;y<=8;y++)for(unsigned mip=0;mip<2;mip++)
        cache_layout_case(x,y,mip,0,0x06);
    cache_layout_case(5,4,1,1,0x00);cache_layout_case(3,4,0,1,0x06);
    failures_and_exclusions();free(g_xram);free(g_xpt);
    large_layout_case(4096,1,1);large_layout_case(1,4096,1);
    large_layout_case(4096,8,4);large_layout_case(8,4096,4);
    large_layout_case(1024,512,10);large_layout_case(4096,4096,13);
    puts("PASS: decoded RGBA Morton coordinates and every mip match linear uploads across 164 cases; layout toggles, immutable versions, opacity, allocation fallback, BC and non-power-of-two exclusions");
    return 0;
}
