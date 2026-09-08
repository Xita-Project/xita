#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define XV_RT_SLOTS 8
#define XV_LOG(...) ((void)0)
#define XV_ONCE(flag, ...) ((void)0)
#define SCE_OK 0
#define SCE_GXM_TEXTURE_CUBE 2
#define SCE_GXM_TEXTURE_FILTER_LINEAR 1
#define SCE_GXM_TEXTURE_ADDR_CLAMP 1
#define SCE_GXM_TEXTURE_ADDR_REPEAT 2
#define SCE_GXM_TEXTURE_TYPE_SWIZZLED 3
typedef int SceGxmContext;
typedef struct { void *data; unsigned type, format, width, height; } SceGxmTexture;
typedef struct { unsigned ntex, pass, previous_frame; SceGxmTexture tex[4]; } cmd_t;
typedef struct { int tex_index[4]; } xv_fshader_t;
static struct { void *mem; } g_rt[8];
static SceGxmTexture g_previous_frame_texture, g_scene_backbuffer_texture;
static SceGxmTexture cube, flat, bound[4];
static unsigned binds;
static int fail_fallback, fail_face;
static void *sceGxmTextureGetData(const SceGxmTexture *t) { return t->data; }
static unsigned sceGxmTextureGetType(const SceGxmTexture *t) { return t->type; }
static unsigned sceGxmTextureGetFormat(const SceGxmTexture *t) { return t->format; }
static unsigned sceGxmTextureGetWidth(const SceGxmTexture *t) { return t->width; }
static unsigned sceGxmTextureGetHeight(const SceGxmTexture *t) { return t->height; }
static int sceGxmTextureInitSwizzled(SceGxmTexture *t, void *data, unsigned format,
    unsigned width, unsigned height, unsigned mips)
{
    if (fail_face) return -1;
    *t=(SceGxmTexture){data,SCE_GXM_TEXTURE_TYPE_SWIZZLED,format,width,height}; return 0;
}
static void sceGxmTextureSetMinFilter(SceGxmTexture *t,unsigned f) {}
static void sceGxmTextureSetMagFilter(SceGxmTexture *t,unsigned f) {}
static int sceGxmSetFragmentTexture(SceGxmContext *ctx,unsigned i,const SceGxmTexture *t)
{ assert(i<4 && t->data!=g_rt[0].mem); bound[i]=*t; binds++; return 0; }
static const SceGxmTexture *cube_fallback(void) { return fail_fallback ? NULL : &cube; }
static const SceGxmTexture *tex2d_fallback(void) { return fail_fallback ? NULL : &flat; }
#include "draw_textures.inc"
int main(void)
{
    int target, source, last, scene, cf, ff;
    g_rt[0].mem=&target;
    g_previous_frame_texture=(SceGxmTexture){.data=&last};
    g_scene_backbuffer_texture=(SceGxmTexture){.data=&scene};
    cube=(SceGxmTexture){.data=&cf,.type=SCE_GXM_TEXTURE_CUBE}; flat=(SceGxmTexture){.data=&ff};
    xv_fshader_t fs={{2,-1,-1,-1}};
    cmd_t c={.pass=1,.ntex=4}; c.tex[0].data=&source; c.tex[3].data=&target;
    assert(bind_draw_textures(NULL,&c,&fs,0) && binds==1 && bound[2].data==&source);
    /* Active color-target sampling is still rejected, including remapped units. */
    fs.tex_index[3]=0;
    assert(!bind_draw_textures(NULL,&c,&fs,0) && binds==2);
    /* Previous-frame replacement removes a stale alias before checking feedback. */
    c.previous_frame=8;
    assert(bind_draw_textures(NULL,&c,&fs,0) && bound[0].data==&scene);
    c.pass=0; assert(bind_draw_textures(NULL,&c,&fs,0) && bound[0].data==&last);
    /* Kind-mismatch fallback is the actual sampled texture. */
    c.pass=1; c.previous_frame=0;
    assert(bind_draw_textures(NULL,&c,&fs,8) && bound[0].data==&cf);
    /* Reading a cube face still reads the same target storage. */
    c.tex[3].type=SCE_GXM_TEXTURE_CUBE;
    assert(!bind_draw_textures(NULL,&c,&fs,0));
    c.tex[3].data=&source;
    assert(bind_draw_textures(NULL,&c,&fs,0) && bound[0].data==&source && bound[0].type==SCE_GXM_TEXTURE_TYPE_SWIZZLED);
    fail_face=1; assert(bind_draw_textures(NULL,&c,&fs,0) && bound[0].data==&ff);
    c.ntex=0; assert(bind_draw_textures(NULL,&c,&fs,8) && bound[0].data==&cf && bound[2].data==&ff);
    fail_fallback=1; assert(!bind_draw_textures(NULL,&c,&fs,8));
    for (unsigned i=0;i<4;i++) fs.tex_index[i]=-1;
    unsigned before=binds; assert(bind_draw_textures(NULL,&c,&fs,0) && binds==before);
    puts("PASS: unused bindings, active feedback, previous-frame substitution, sampler remapping, cube faces, and fallback failure");
}
