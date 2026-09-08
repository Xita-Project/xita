#define XV_RUN_RECOMP 1
#include <assert.h>
#include "../../runtime/xv_d3d.c"
static unsigned resolves, setters, options, links;
static int available=1, fail_link;
static SceGxmTexture source;
static uint32_t header[8];
void xv_logf(const char *fmt,...) { (void)fmt; }
void *xv_guest_ptr(uint32_t p) { (void)p;return header; }
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t h,uint32_t p)
{ (void)h;(void)p;resolves++;return available?&source:NULL; }
void xv_ui_gxm_apply_texture_options(SceGxmTexture *t) { (void)t;options++; }
int sceGxmTextureSetMinFilter(SceGxmTexture *t,SceGxmTextureFilter v) { (void)t;(void)v;setters++;return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t,SceGxmTextureFilter v) { (void)t;(void)v;setters++;return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { (void)t;(void)v;setters++;return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { (void)t;(void)v;setters++;return 0; }
int xv_fshader_load(xv_fshader_t *fs,const char *path,const xv_vshader_t *vs,const SceGxmBlendInfo *bi)
{
    (void)vs;assert(!strcmp(path,"builtin:xita-depth"));
    assert(bi&&bi->colorMask==SCE_GXM_COLOR_MASK_NONE);
    links++;if(fail_link)return -1;
    fs->fprog=(void *)2;for(unsigned i=0;i<4;i++)fs->tex_index[i]=-1;return 0;
}
int main(int argc,char **argv)
{
    int disabled=argc>1;(void)argv;
    setenv("XV_SAMPLER_CACHE",disabled?"0":"1",1);
    setenv("XV_DEPTH_ONLY_SHADER",disabled?"0":"1",1);
    S.tex_guest[0]=123;
    for(unsigned i=0;i<100;i++)assert(texture_for(0));
    assert(resolves==100);assert(options==(disabled?100u:1u));assert(setters==options*4);
    unsigned start=options;
    for(unsigned word=0;word<4;word++) { ((uint32_t *)&source)[word]++;assert(texture_for(0)); }
    S.tex_min[0]++;assert(texture_for(0));S.tex_mag[0]++;assert(texture_for(0));
    S.tex_addr_u[0]++;assert(texture_for(0));S.tex_addr_v[0]++;assert(texture_for(0));
    assert(options==start+8);
    /* Cache hits never bypass a missing/dirty/paletted texture resolution. */
    S.pal_guest[0]++;assert(texture_for(0));assert(resolves==109);
    available=0;assert(!texture_for(0));assert(resolves==110);available=1;
    assert(texture_for(0));
    S.tex_guest[1]=124;assert(texture_for(1));
    /* A render-target alias's new control word invalidates the stage too. */
    header[1]=g_rt[0].data=0x12000;g_rt[0].fmt=header[3]=0;
    memset(&g_rt[0].tex,0x67,sizeof source);assert(texture_for(0));
    assert(!memcmp(texture_for(0),&g_rt[0].tex,sizeof source));
    assert(resolves==112); /* both alias calls avoid the decoded texture path */
    cmd_t c={0};xv_fshader_t original={0};original.fprog=(void *)1;
    vs_slot_t *v=&g_vs[0];
    assert(depth_only_fragment(v,&c,&original)==&original);assert(!links);
    c.blend=3;g_blend_combo[3].mask=0;
    original.uses_discard=1;assert(depth_only_fragment(v,&c,&original)==&original);
    original.uses_discard=0;original.replaces_depth=1;assert(depth_only_fragment(v,&c,&original)==&original);
    original.replaces_depth=0;
    for(unsigned mask=1;mask<16;mask++){g_blend_combo[3].mask=mask;assert(depth_only_fragment(v,&c,&original)==&original);}
    g_blend_combo[3].mask=0;
    xv_fshader_t *expected=disabled?&original:&v->fs[FS_COLOR][BLEND_NOCOLOR];
    for(unsigned i=0;i<100;i++)assert(depth_only_fragment(v,&c,&original)==expected);
    assert(links==!disabled);
    if(!disabled)for(unsigned i=0;i<4;i++)assert(expected->tex_index[i]==-1);
    fail_link=1;v=&g_vs[1];
    assert(depth_only_fragment(v,&c,&original)==&original);
    assert(depth_only_fragment(v,&c,&original)==&original);assert(links==(disabled?0u:2u));
    assert(!depth_only_fragment(v,&c,NULL));c.blend=BLEND_MODES;
    assert(depth_only_fragment(v,&c,&original)==&original);
    puts("PASS: sampler reuse retains live texture/alias checks; depth-only path preserves all color masks, discard/depth replacement and failed shader links");
}
