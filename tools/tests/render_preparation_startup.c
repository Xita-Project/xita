/* Real depth recording/replay and texture resolver/cache bodies, host GXM stubs.
 * Each invocation is a fresh process because production configuration is lazy. */
#define XV_LOG(...) printf(__VA_ARGS__)
#define main retained_depth_regression_main
#include "depth_prepare.c"
#undef main
#define XV_ONCE(flag, ...) ((void)0)
#define XV_RT_SLOTS 8
#define SCE_GXM_MAX_TEXTURE_UNITS 16
#define SCE_OK 0
#define SCE_GXM_TEXTURE_CUBE 2
#define SCE_GXM_TEXTURE_FILTER_LINEAR 1
#define SCE_GXM_TEXTURE_ADDR_CLAMP 1
#define SCE_GXM_TEXTURE_ADDR_REPEAT 2
#define SCE_GXM_TEXTURE_TYPE_SWIZZLED 3
typedef int SceGxmContext;
static struct { void *mem; } g_rt[8];
static SceGxmTexture g_previous_frame_texture, g_scene_backbuffer_texture;
static SceGxmTexture flat, cube_fallback_texture, bindings[16];
static unsigned binds;
static void *sceGxmTextureGetData(const SceGxmTexture *t) { return (void *)(uintptr_t)t->words[0]; }
static unsigned sceGxmTextureGetType(const SceGxmTexture *t) { return t->words[1]; }
static unsigned sceGxmTextureGetFormat(const SceGxmTexture *t) { return t->words[2]; }
static unsigned sceGxmTextureGetWidth(const SceGxmTexture *t) { return 16; }
static unsigned sceGxmTextureGetHeight(const SceGxmTexture *t) { return 16; }
static int sceGxmTextureInitSwizzled(SceGxmTexture *t,void *data,unsigned format,unsigned w,unsigned h,unsigned mips)
{ *t=(SceGxmTexture){{(uint32_t)(uintptr_t)data,SCE_GXM_TEXTURE_TYPE_SWIZZLED,format,0}};return 0; }
static void sceGxmTextureSetMinFilter(SceGxmTexture *t,unsigned f) {}
static void sceGxmTextureSetMagFilter(SceGxmTexture *t,unsigned f) {}
static int sceGxmSetFragmentTexture(SceGxmContext *ctx,unsigned unit,const SceGxmTexture *t)
{ assert(unit<16);bindings[unit]=*t;binds++;return 0; }
static const SceGxmTexture *cube_fallback(void) { return &cube_fallback_texture; }
static const SceGxmTexture *tex2d_fallback(void) { return &flat; }
static void xv_render_profile_texture(int skipped,int failed) { assert(!failed); }
#include "xv_texture_state.h"
#include "draw_textures.inc"
#include "texture_config.inc"
#include "render_startup.inc"

int main(int argc,char **argv)
{
    assert(argc==4);
    int want_cache=atoi(argv[1]),want_prepare=atoi(argv[2]),compatible=atoi(argv[3]);
    xv_d3d_configure_render_preparation(); /* Actual startup function; no setters. */
    assert(texture_state_enabled()==want_cache);
    assert(xv_depth_prepare_available()==compatible && depth_prepare_enabled()==want_prepare);
    for(unsigned v=0;v<XV_MAX_VS;v++)for(unsigned b=0;b<BLEND_MODES;b++)for(unsigned f=0;f<FS_KINDS;f++) {
        g_vs[v].fs[f][b].fprog=(void *)(uintptr_t)(1+v*128+f*24+b);
        for(unsigned t=0;t<4;t++)g_vs[v].fs[f][b].tex_index[t]=-1;
    }
    linked_program=(xv_fshader_t){.fprog=(void *)1,.alpha_test_mode=1};
    cmd_t c=query(),before=c;
#if XV_PACKED_VERTEX_LAYOUT
    c.packed_vertex=1;before=c;
#endif
    assert(record_material(&c,NULL,1)==7 && !memcmp(&before,&c,sizeof c));
    unsigned cube=0;
    if(compatible) {
        assert(replay(&c,&cube)==&g_vs[c.vs].fs[FS_COLOR][BLEND_NOCOLOR]);
        assert(xv_depth_proof_read(&g_depth_proofs,depth_prepare_key(&c)));
    } else {
        /* Even a previously published proof cannot overrule incompatible modes. */
        xv_depth_proof_publish(&g_depth_proofs,depth_prepare_key(&c));
    }
    c=before;
    assert(record_material(&c,NULL,1)==(want_prepare?0u:7u));
    before.depth_prepared=want_prepare;
    assert(!memcmp(&before,&c,sizeof c)); /* includes packed streams, query ID/depth/stencil. */
    xv_texture_state cache={0};xv_texture_state *state=want_cache?&cache:NULL;
    cmd_t textured=query();textured.pass=1;textured.ntex=1;textured.tex[0].words[0]=0x765400;
    xv_fshader_t fs={.tex_index={15,-1,-1,-1}};
    assert(bind_draw_textures(state,NULL,&textured,&fs,0) && binds==1);
    if(compatible) {
        xv_fshader_t *depth=replay(&c,&cube);
        unsigned old=binds;
        assert(bind_draw_textures(state,NULL,&c,depth,cube) && binds==old);
        assert(bind_draw_textures(state,NULL,&textured,&fs,0));
        assert(binds==old+!want_cache); /* depth draw leaves actual prior binding intact. */
    }
    /* Feedback is checked before equality, even after a prepared draw. */
    g_rt[0].mem=sceGxmTextureGetData(&textured.tex[0]);
    unsigned old=binds;assert(!bind_draw_textures(state,NULL,&textured,&fs,0)&&binds==old);
    g_rt[0].mem=NULL;
    /* Clear/new range reset used by render_range; actual scene/UI boundaries
     * are separately covered by retained render-target replay fixtures. */
    cache.valid=0;
    assert(bind_draw_textures(state,NULL,&textured,&fs,0)&&binds==old+1);
    assert(!memcmp(&bindings[15],&textured.tex[0],sizeof bindings[15]));
    for(int mode=-2;mode<=2;mode++) {
        xv_d3d_texture_state_override(mode);xv_depth_prepare_override(mode);
        assert(texture_state_enabled()==(mode<0?want_cache:!!mode));
        assert(depth_prepare_enabled()==(mode<0?want_prepare:compatible&&!!mode));
    }
    xv_d3d_texture_state_override(-1);xv_depth_prepare_override(-1);
    assert(texture_state_enabled()==want_cache && depth_prepare_enabled()==want_prepare);
    puts("PASS production startup/cold-proof/record/replay/cache/feedback/full-command retention/override restoration");
}
