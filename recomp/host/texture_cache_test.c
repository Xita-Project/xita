/* Run the actual texture selection/decoder/cache against synthetic mip chains.
 * Fake only GPU descriptors and I/O; count uploads, not private helper calls. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#ifndef XV_TEXTURE_TEST_SOURCE
#define XV_TEXTURE_TEST_SOURCE "../../runtime/xv_ui_gxm.c"
#endif
#include XV_TEXTURE_TEST_SOURCE

int xv_texture_decode(const xv_texture_job *j) { return xv_tex_decode_range(j, 0, xv_tex_units(j)); }
uint8_t *g_xram;
uint32_t *g_xpt;
static unsigned uploads;
static unsigned sampler_writes;
static SceGxmTextureFilter test_min, test_mag;
static SceGxmTextureMipFilter test_mip;
static SceGxmTextureType test_type;
static SceGxmTextureFormat upload_format;
static unsigned upload_mips;
static SceGxmTextureType upload_layout;
static uint32_t target_header;
static SceGxmTexture target_texture;
const SceGxmTexture *xv_d3d_render_target_texture(uint32_t hdr)
{ return target_header && hdr == target_header ? &target_texture : NULL; }
void xv_logf(const char *fmt, ...) { (void)fmt; }
void xv_gpu_flush(const void *p, uint32_t n) { (void)p; (void)n; }
uint64_t xk_os_monotonic_us(void) { static uint64_t now; return ++now; }
SceUID sceIoOpen(const char *p, int flags, SceMode mode) { (void)p; (void)flags; (void)mode; abort(); }
int sceIoClose(SceUID fd) { (void)fd; abort(); }
SceSSize sceIoWrite(SceUID fd, const void *p, SceSize n) { (void)fd; (void)p; (void)n; abort(); }
int sceIoMkdir(const char *p, SceMode mode) { (void)p; (void)mode; abort(); }
int sceClibSnprintf(char *p, SceSize n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vsnprintf(p, n, fmt, ap); va_end(ap); return r;
}
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *p, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) {
    upload_layout=SCE_GXM_TEXTURE_LINEAR; assert(w && h && m); upload_format=f; upload_mips=m; memset(t, 0, sizeof *t); memcpy(t, &p, sizeof p); uploads++; return SCE_OK;
}
int sceGxmTextureInitSwizzled(SceGxmTexture *t, const void *p, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) {
    int result=sceGxmTextureInitLinear(t, p, f, w, h, m);
    upload_layout=SCE_GXM_TEXTURE_SWIZZLED; return result;
}
int sceGxmTextureInitCube(SceGxmTexture *t, const void *p, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) {
    int result=sceGxmTextureInitLinear(t, p, f, w, h, m);
    upload_layout=SCE_GXM_TEXTURE_CUBE; return result;
}
void *sceGxmTextureGetData(const SceGxmTexture *t) { void *p; memcpy(&p, t, sizeof p); return p; }
int sceGxmTextureSetMinFilter(SceGxmTexture *t, SceGxmTextureFilter f) { (void)t; test_min = f; sampler_writes++; return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t, SceGxmTextureFilter f) { (void)t; test_mag = f; sampler_writes++; return 0; }
int sceGxmTextureSetMipFilter(SceGxmTexture *t, SceGxmTextureMipFilter f) { (void)t; test_mip = f; sampler_writes++; return 0; }
SceGxmTextureFilter sceGxmTextureGetMinFilter(const SceGxmTexture *t) { (void)t; return test_min; }
SceGxmTextureType sceGxmTextureGetType(const SceGxmTexture *t) { (void)t; return test_type; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode f) { (void)t; (void)f; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode f) { (void)t; (void)f; return 0; }

static void mip_case(unsigned fmt, unsigned logw, unsigned logh, int dynamic, int cube)
{
    memset(&g, 0, sizeof g); memset(g.texhash, 0xFF, sizeof g.texhash);
    g.dec_cap = 8u << 20; g.dec_base = malloc(g.dec_cap); assert(g.dec_base);
    const uint32_t hdr = dynamic ? 0x03D00000u : 0x1000u, data = 0x10000u;
    X_M32(hdr + 4) = data;
    X_M32(hdr + 12) = (fmt << 8) | (logw << 20) | (logh << 24) | (4u << 16) | (cube ? 4u : 0u);
    X_M32(hdr + 16) = 0;
    unsigned w = 1u << logw, h = 1u << logh, off = 0;
    for (unsigned l = 0; l < 4; ++l) {
        unsigned n = fmt == 0x0C ? ((w + 3) / 4) * ((h + 3) / 4) * 8 :
                     fmt == 0x0E || fmt == 0x0F ? ((w + 3) / 4) * ((h + 3) / 4) * 16 :
                     w * h * (fmt == 0 ? 1 : 4);
        memset(X_G(0x80000000u | (data + off)), 0x24 + 31 * l, n);
        off += n; w >>= 1; h >>= 1;
    }
    unsigned start = uploads;
    const SceGxmTexture *t = ui_texture_for(hdr, 0); assert(t && uploads == start + 1);
    /* Rebinding for thirty normal map-check cycles must reuse unchanged pixels,
     * including when level 0 differs from the smaller mip actually decoded. */
    for (unsigned f = 1; f <= 480; ++f) {
        g.rec_frame = f; assert(ui_texture_for(hdr, 0) == t);
        assert(uploads == start + 1);
    }
    ui_tex_entry *e = &g.texcache[0];
    uint32_t selected = e->source_data;
    uint32_t sum = e->sum, pool_used = g.dec_off;
    if (!cube && (logw > 8 || logh > 8)) {
        assert(selected > data);
        X_M32(0x80000000u | data) ^= 0xFF;
        xv_ui_gxm_invalidate_range(0x80000000u | data, 4);
        assert(!e->dirty); /* an unused mip does not invalidate the selected one */
    } else assert(selected == data);
    X_M32(0x80000000u | selected) ^= 0x1234;
    xv_ui_gxm_invalidate_range(0x80000000u | selected, 4);
    assert(e->dirty);
    g.rec_frame++;
    t = ui_texture_for(hdr, 0); assert(t && uploads == start + 2);
    if (!cube) { assert(e->sum != sum); assert(g.dec_off > pool_used); }
    for (unsigned f = 0; f < 32; ++f) {
        g.rec_frame++; assert(ui_texture_for(hdr, 0) == t); assert(uploads == start + 2);
    }
    free(g.dec_base);
}

static void alpha_case(void)
{
    uint8_t source[2] = {0x00, 0x7C}; uint32_t pixel;
    assert(ui_decode(source, 0x03, 1, 1, 2, 0, &pixel) == 0 && pixel == 0xFF0000FFu);
    assert(ui_decode(source, 0x02, 1, 1, 2, 0, &pixel) == 0 && pixel == 0x000000FFu);
    assert(ui_decode(source, 0x1C, 1, 1, 2, 1, &pixel) == 0 && pixel == 0xFF0000FFu);
    memset(&g, 0, sizeof g); memset(g.texhash, 0xFF, sizeof g.texhash);
    g.dec_cap = 4096; g.dec_base = malloc(g.dec_cap); assert(g.dec_base);
    const uint32_t hdr = 0x1000, data = 0x10000;
    X_M32(hdr + 4) = data; X_M32(hdr + 12) = (0x06 << 8) | (3 << 20) | (3 << 24) | (1 << 16);
    X_M32(hdr + 16) = 0;
    for (unsigned i = 0; i < 64; i++) X_M32(0x80000000u | (data + i * 4)) = 0x80000000u;
    const SceGxmTexture *material = ui_texture_for(hdr, 0); assert(material);
    assert(*(uint32_t *)sceGxmTextureGetData(material) == 0x80000000u);
    const SceGxmTexture *font = ui_texture_for(hdr, 1); assert(font && font != material);
    assert(*(uint32_t *)sceGxmTextureGetData(font) == 0x80FFFFFFu);
    assert(ui_texture_for(hdr, 0) == material && *(uint32_t *)sceGxmTextureGetData(material) == 0x80000000u);
    unsigned old_uploads = uploads, old_pool = g.dec_off;
    target_header = hdr;
    assert(ui_texture_for(hdr, 0) == &target_texture);
    assert(ui_texture_for(hdr, 1) == &target_texture);
    assert(uploads == old_uploads && g.dec_off == old_pool);
    target_header = 0;
    assert(ui_texture_for(hdr, 0) == material);
    free(g.dec_base);
}

static void compressed_cube_case(unsigned fmt, unsigned logw)
{
    memset(&g,0,sizeof g);memset(g.texhash,0xff,sizeof g.texhash);
    g.dec_cap=8u<<20;g.dec_base=malloc(g.dec_cap);assert(g.dec_base);
    const uint32_t hdr=0x1000,data=0x10000;
    unsigned levels=logw+1,w=1u<<logw,block=fmt==0x0c ? 8 : 16;
    unsigned source_stride=ALIGN_UP(xv_bc_chain_bytes(w,w,levels,block),128);
    X_M32(hdr+4)=data;X_M32(hdr+12)=(fmt<<8)|(logw<<20)|(logw<<24)|(levels<<16)|4;
    X_M32(hdr+16)=0;
    for(unsigned face=0;face<6;++face){
        unsigned offset=0;
        for(unsigned level=0,size=w;level<levels;++level,size>>=1){
            unsigned bytes=xv_bc_level_bytes(size,size,block);
            memset(X_G(0x80000000u|data)+face*source_stride+offset,1+face*16+level,bytes);
            offset+=bytes;
        }
    }
    const SceGxmTexture *t=ui_texture_for(hdr,0);assert(t);
    SceGxmTextureFormat expected;int linear,bc;
    assert(!xbox_fmt_to_gxm(fmt,&expected,&linear,&bc));
    if(ui_extended_bc()){
        assert(upload_format==expected&&upload_mips==levels);
        unsigned stride=xv_bc_chain_bytes(w,w,levels,block);
        if(w>=32)stride=ALIGN_UP(stride,2048);
        const uint8_t *pixels=sceGxmTextureGetData(t);
        for(unsigned face=0;face<6;++face){
            unsigned offset=0;
            for(unsigned level=0,size=w;level<levels;++level,size>>=1){
                unsigned bytes=xv_bc_level_bytes(size,size,block);
                for(unsigned b=0;b<bytes;++b)assert(pixels[face*stride+offset+b]==1+face*16+level);
                offset+=bytes;
            }
        }
        assert(g.dec_off==ALIGN_UP(stride*6,64));
    }else assert(upload_format==SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    free(g.dec_base);
}

static void palette_case(void)
{
    /* Changing each word, including the last one, must invalidate the cached
     * sum immediately. Alternate colliding slots and identical contents at
     * different addresses; equality is by all bytes, never an address alone. */
    uint32_t palettes[5][256];
    for(unsigned p=0;p<5;p++)for(unsigned i=0;i<256;i++)palettes[p][i]=0xff000000u+p*0x10000+i;
    for(unsigned i=0;i<256;i++)for(unsigned p=0;p<5;p++) {
        palettes[p][i]^=0x123456;
        assert(ui_palette_hash(palettes[p])==(ui_tex_hash(palettes[p],1024)|1u));
        assert(ui_palette_hash(palettes[p])==(ui_tex_hash(palettes[p],1024)|1u));
    }
    memcpy(palettes[1],palettes[0],sizeof palettes[0]);
    assert(ui_palette_hash(palettes[0])==ui_palette_hash(palettes[1]));
    const char *enabled=getenv("XV_PALETTE_HASH_CACHE");
    assert((g_palette_reused!=0)==(!enabled || atoi(enabled)!=0));
    for(int mode=0;mode<4;mode++) {
        int override=mode==3?-1:mode%2;
        xv_palette_cache_override(override);
        ui_palette_hash(palettes[0]);unsigned hits=g_palette_reused;
        assert(ui_palette_hash(palettes[0])==(ui_tex_hash(palettes[0],1024)|1u));
        assert(g_palette_reused-hits==(unsigned)(override<0?(!enabled||atoi(enabled)!=0):override));
        palettes[0][255]^=0x44332211;
        assert(ui_palette_hash(palettes[0])==(ui_tex_hash(palettes[0],1024)|1u));
    }

    memset(&g,0,sizeof g);memset(g.texhash,0xff,sizeof g.texhash);
    g.dec_cap=16384;g.dec_base=malloc(g.dec_cap);assert(g.dec_base);
    const uint32_t hdr=0x1000,data=0x10000,pal=0x30000;
    X_M32(hdr+4)=data;X_M32(hdr+12)=(0x0b<<8)|(3<<20)|(3<<24)|(1<<16);X_M32(hdr+16)=0;
    memset(X_G(data),255,64);memset(X_G(pal),0,1024);X_M32(pal+1020)=0xff223344;
    unsigned start=uploads;
    const SceGxmTexture *first=ui_texture_for_pal(hdr,0,pal);assert(first&&uploads==start+1);
    assert(*(uint32_t *)sceGxmTextureGetData(first)==0xff443322);
    for(unsigned i=0;i<50;i++)assert(ui_texture_for_pal(hdr,0,pal)==first);
    assert(uploads==start+1);
    /* No frame advance or file notification: the palette write alone must
     * change decoded output on the next bind. */
    X_M32(pal+1020)=0x80112233;
    const SceGxmTexture *changed=ui_texture_for_pal(hdr,0,pal);assert(changed&&uploads==start+2);
    assert(*(uint32_t *)sceGxmTextureGetData(changed)==0x80332211);
    assert(ui_texture_for_pal(hdr,0,0x80000000u|pal)==changed);
    X_M32(pal+1020)=0xff223344;
    assert(ui_texture_for_pal(hdr,0,pal)==first&&uploads==start+2);
    free(g.dec_base);
    puts("PASS: palette cache checks every word; same-frame writes, address aliases and restored palettes render current colors");
}

static void opaque_upload_case(unsigned fmt, int rectangle, int cube)
{
    memset(&g,0,sizeof g);memset(g.texhash,0xff,sizeof g.texhash);
    g.dec_cap=1u<<20;g.dec_base=malloc(g.dec_cap);assert(g.dec_base);
    const uint32_t hdr=0x1000,data=0x10000;
    unsigned lw=rectangle?4:3,lh=3;
    X_M32(hdr+4)=data;X_M32(hdr+12)=(fmt<<8)|(lw<<20)|(lh<<24)|(4u<<16)|(cube?4u:0u);
    X_M32(hdr+16)=0;
    memset(X_G(data),fmt==0x0c?0:255,16384);
    const SceGxmTexture *t=ui_texture_for(hdr,0);assert(t);
    if(cube){assert(!xv_ui_gxm_texture_opaque(t));free(g.dec_base);return;}
    SceGxmTexture old=*t;
    unsigned old_bytes=g.dec_off;
    uint8_t *copy=malloc(old_bytes);assert(copy);memcpy(copy,g.dec_base,old_bytes);
    assert(xv_ui_gxm_texture_opaque(t));
    assert(!xv_ui_gxm_texture_opaque(&old)); /* A descriptor copy is not cache ownership. */
    assert(!xv_ui_gxm_texture_opaque(NULL));
    assert(!xv_ui_gxm_texture_opaque(&target_texture));
    for(unsigned i=0;i<40;i++){g.rec_frame++;assert(ui_texture_for(hdr,0)==t);assert(xv_ui_gxm_texture_opaque(t));}
    assert(g.dec_off==old_bytes);
    ui_tex_entry *e=&g.texcache[0];
    uint8_t *changed=X_G(e->source_data);
    unsigned at=0;
    if(fmt==0x0c){at=e->bytes-8+4;changed[at]=3;}
    else if(fmt==0x0e){at=e->bytes-16;changed[at]=0;}
    else if(fmt==0x0f){at=e->bytes-16;changed[at]=0;changed[at+2]&=0xf8;}
    else {at=3;changed[at]=0;}
    xv_ui_gxm_invalidate_range(e->source_data+at,4);g.rec_frame++;
    t=ui_texture_for(hdr,0);assert(t);
    assert(!xv_ui_gxm_texture_opaque(t));
    assert(sceGxmTextureGetData(t)!=sceGxmTextureGetData(&old));
    assert(!memcmp(copy,sceGxmTextureGetData(&old),old_bytes));
    unsigned updated_bytes=g.dec_off;
    void *updated=sceGxmTextureGetData(t);
    memset(X_G(data),fmt==0x0c?0:255,16384);
    xv_ui_gxm_invalidate_range(data,16384);g.rec_frame++;
    t=ui_texture_for(hdr,0);assert(t && xv_ui_gxm_texture_opaque(t));
    assert(sceGxmTextureGetData(t)!=updated && g.dec_off>updated_bytes);
    /* Failure to allocate a new version must leave the pinned upload intact. */
    uint8_t *latest=malloc(updated_bytes);assert(latest);memcpy(latest,g.dec_base,updated_bytes);
    g.dec_cap=g.dec_off;
    X_M32(data)^=0x12345678u;xv_ui_gxm_invalidate_range(data,4);g.rec_frame++;
    assert(!ui_texture_for(hdr,0) && g.tex_purge);
    assert(!memcmp(latest,g.dec_base,updated_bytes));
    free(latest);free(copy);free(g.dec_base);
}

int main(void)
{
    SceGxmTexture sampler = {0};
    unsetenv("XV_TEX_FILTER"); unsetenv("XV_MIP_SMOOTH");
    sampler_writes = 0;
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(!sampler_writes); /* defaults preserve every sampler field */
    setenv("XV_TEX_FILTER", "1", 1); setenv("XV_MIP_SMOOTH", "0", 1);
    g_texture_filter = -1; test_min = SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR;
    test_type = SCE_GXM_TEXTURE_SWIZZLED; test_mip = SCE_GXM_TEXTURE_MIP_FILTER_ENABLED;
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(test_min == SCE_GXM_TEXTURE_FILTER_MIPMAP_POINT && test_mag == SCE_GXM_TEXTURE_FILTER_POINT);
    assert(test_mip == SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
    setenv("XV_TEX_FILTER", "2", 1); g_texture_filter = -1;
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(test_min == SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR && test_mag == SCE_GXM_TEXTURE_FILTER_LINEAR);
    test_type = SCE_GXM_TEXTURE_LINEAR_STRIDED; test_min = SCE_GXM_TEXTURE_FILTER_POINT;
    test_mip = SCE_GXM_TEXTURE_MIP_FILTER_ENABLED; sampler_writes = 0;
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(test_min == SCE_GXM_TEXTURE_FILTER_LINEAR && sampler_writes == 2);
    assert(test_mip == SCE_GXM_TEXTURE_MIP_FILTER_ENABLED); /* stride bits untouched */
    /* Live edits replace only the selected override; GAME/AUTO again leave
     * the next draw's original descriptor unchanged. No environment mutation. */
    xv_ui_gxm_set_texture_options(1,-1);
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(test_mag==SCE_GXM_TEXTURE_FILTER_POINT && g_mip_smooth==0);
    xv_ui_gxm_set_texture_options(0,1);sampler_writes=0;
    xv_ui_gxm_apply_texture_options(&sampler);assert(!sampler_writes);
    xv_ui_gxm_set_texture_options(2,-1);
    xv_ui_gxm_apply_texture_options(&sampler);
    assert(test_mag==SCE_GXM_TEXTURE_FILTER_LINEAR && g_mip_smooth==1);
    unsetenv("XV_TEX_FILTER"); unsetenv("XV_MIP_SMOOTH"); g_texture_filter = -1;
    puts("PASS: sampler defaults preserved, point/linear overrides retain mip selection, mip off respects strided textures");
    unsetenv("XV_TEXDUMP"); unsetenv("XV_P8_FLAT");
    setenv("XV_TEX_MAXDIM", "256", 1);
    g_xram = calloc(1, 8u << 20); g_xpt = calloc(1u << 20, sizeof *g_xpt); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < (8u << 20) / 4096; ++i) g_xpt[i] = g_xpt[0x80000 + i] = i * 4096;
    g_xpt[0x03D00] = 0x2000; /* separate synthetic dynamic resource header */
    palette_case();
    mip_case(0x0C, 10, 9, 0, 0); /* non-square DXT decoded to RGBA */
    mip_case(0x0E, 10, 8, 0, 0);
    mip_case(0x0F, 10, 9, 0, 0);
    mip_case(0x0C, 9, 9, 0, 0);  /* square DXT uploaded as BC blocks */
    mip_case(0x00, 9, 9, 0, 0);  /* uncompressed luminance mip chain */
    mip_case(0x07, 9, 9, 1, 0);  /* dynamic: checks every frame */
    mip_case(0x0C, 8, 7, 0, 0);  /* no skipped mip */
    mip_case(0x07, 3, 3, 0, 1);  /* cube source still starts at resource data */
    alpha_case();
    for(unsigned fmt=0x0c;fmt<=0x0f;++fmt)if(fmt!=0x0d)
        for(unsigned size=3;size<=8;++size)compressed_cube_case(fmt,size);
    for(unsigned fmt=0x0c;fmt<=0x0f;fmt++)if(fmt!=0x0d)
        for(unsigned rectangle=0;rectangle<2;rectangle++)opaque_upload_case(fmt,rectangle,0);
    opaque_upload_case(0x06,0,0);opaque_upload_case(0x06,1,0);
    opaque_upload_case(0x07,0,1);
    puts("PASS: uploaded opacity, BC/RGBA mip changes, pinned draw snapshots, foreign descriptors, cube rejection and allocation failure");
    free(g_xram); free(g_xpt);
    puts("PASS: unchanged selected mips reuse uploads; selected-mip writes refresh BC/RGBA/dynamic/cube textures");
    return 0;
}
