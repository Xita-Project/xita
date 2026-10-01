#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define XV_RT_SLOTS 8
#define SCE_GXM_MAX_TEXTURE_UNITS 16
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
static SceGxmTexture cube, flat, driver[2][SCE_GXM_MAX_TEXTURE_UNITS];
#define bound driver[0]
static unsigned binds;
static int fail_fallback, fail_face, fail_unit=-1;
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
{ assert(i<SCE_GXM_MAX_TEXTURE_UNITS && t->data!=g_rt[0].mem); binds++;
  if ((int)i==fail_unit) return -1;
  driver[ctx?1:0][i]=*t; return 0; }
static const SceGxmTexture *cube_fallback(void) { return fail_fallback ? NULL : &cube; }
static const SceGxmTexture *tex2d_fallback(void) { return fail_fallback ? NULL : &flat; }
#include "xv_texture_bind_cache.h"
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
    assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && binds==1 && bound[2].data==&source);
    /* Active color-target sampling is still rejected, including remapped units. */
    fs.tex_index[3]=0;
    assert(!bind_draw_textures(NULL,&c,&fs,0,NULL) && binds==2);
    /* Previous-frame replacement removes a stale alias before checking feedback. */
    c.previous_frame=8;
    assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && bound[0].data==&scene);
    c.pass=0; assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && bound[0].data==&last);
    /* Kind-mismatch fallback is the actual sampled texture. */
    c.pass=1; c.previous_frame=0;
    assert(bind_draw_textures(NULL,&c,&fs,8,NULL) && bound[0].data==&cf);
    /* Reading a cube face still reads the same target storage. */
    c.tex[3].type=SCE_GXM_TEXTURE_CUBE;
    assert(!bind_draw_textures(NULL,&c,&fs,0,NULL));
    c.tex[3].data=&source;
    assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && bound[0].data==&source && bound[0].type==SCE_GXM_TEXTURE_TYPE_SWIZZLED);
    fail_face=1; assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && bound[0].data==&ff);
    c.ntex=0; assert(bind_draw_textures(NULL,&c,&fs,8,NULL) && bound[0].data==&cf && bound[2].data==&ff);
    fail_fallback=1; assert(!bind_draw_textures(NULL,&c,&fs,8,NULL));
    for (unsigned i=0;i<4;i++) fs.tex_index[i]=-1;
    unsigned before=binds; assert(bind_draw_textures(NULL,&c,&fs,0,NULL) && binds==before);
    puts("PASS: unused bindings, active feedback, previous-frame substitution, sampler remapping, cube faces, and fallback failure");

    fail_fallback=fail_face=0;
    xv_texture_bind_cache cache={.enabled=1};
    xv_texture_bind_cache uncached={0};
    SceGxmContext cached_context=1;
    memset(driver,0,sizeof driver);
    unsigned uncached_calls=0, cached_calls=0;
    for(unsigned draw=0;draw<4096;draw++) {
        c=(cmd_t){.ntex=4};
        for(unsigned t=0;t<4;t++) {
            c.tex[t]=(SceGxmTexture){.data=&source,.format=t,.width=64,.height=64};
            fs.tex_index[t]=t;
        }
        /* Most repeated draws share all bindings. Exercise every descriptor
         * field, higher remapped units, inactive stages, fallback and aliases. */
        switch((draw/32)%8) {
        case 1: c.tex[0].width=128; break;
        case 2: fs.tex_index[0]=15; fs.tex_index[1]=-1; break;
        case 3: c.previous_frame=1; break;
        case 4: c.ntex=0; break;
        case 5: c.tex[0].type=SCE_GXM_TEXTURE_CUBE; break;
        case 6: c.tex[0].data=&last; break;
        case 7: fs.tex_index[0]=fs.tex_index[1]=3; break;
        }
        if(draw%127==0) {
            /* Simulate external UI/clear/scene state, then start a new range. */
            cache.valid=0; memset(driver,0,sizeof driver);
        }
        before=binds;
        assert(bind_draw_textures(NULL,&c,&fs,0,&uncached));
        uncached_calls+=binds-before;
        before=binds;
        assert(bind_draw_textures(&cached_context,&c,&fs,0,&cache));
        cached_calls+=binds-before;
        assert(!memcmp(driver[0],driver[1],sizeof driver[0]));
    }
    assert(cached_calls<uncached_calls);
    assert(uncached.submitted==uncached_calls && !uncached.reused && !uncached.valid);
    assert(cache.submitted==cached_calls && cache.reused==uncached_calls-cached_calls);
    printf("PASS: 4096 differential draws, identical driver state; %u -> %u texture binds\n",uncached_calls,cached_calls);
    /* Feedback checks run even on a would-be cache hit. */
    c=(cmd_t){.ntex=1}; c.tex[0].data=&source;
    fs=(xv_fshader_t){{0,-1,-1,-1}};
    assert(bind_draw_textures(&cached_context,&c,&fs,0,&cache));
    g_rt[1].mem=&source; c.pass=2;
    assert(!bind_draw_textures(&cached_context,&c,&fs,0,&cache));
    c.pass=0;
    /* Setter failure invalidates the unit; the next attempt must retry. */
    c.tex[0].width=4096; fail_unit=0; before=binds;
    assert(!bind_draw_textures(&cached_context,&c,&fs,0,&cache));
    assert(!(cache.valid&1)); fail_unit=-1;
    assert(bind_draw_textures(&cached_context,&c,&fs,0,&cache) && binds==before+2);
    before=binds;
    assert(bind_draw_textures(&cached_context,&c,&fs,0,&cache) && binds==before);
    fs.tex_index[0]=16;
    assert(!bind_draw_textures(&cached_context,&c,&fs,0,&cache) && binds==before);
    puts("PASS: feedback on cache hits, failed binds retry, out-of-range units rejected");
}
