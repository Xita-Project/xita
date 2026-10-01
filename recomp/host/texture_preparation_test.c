/* Exercise production recording against its unfiltered path. Sampler metadata
 * is controlled here; shader_loader_test separately checks its real API. */
#define XV_RUN_RECOMP 1
#include <assert.h>
#include "../../runtime/xv_d3d.c"
static uint32_t headers[4][8];
static SceGxmTexture textures[4];
static unsigned missing, resolves[4], metadata_reads, override_shaders;
static unsigned fallback_masks[FS_KINDS] = {0, 1, 1, 5};
void xv_logf(const char *fmt,...) { (void)fmt; }
void *xv_guest_ptr(uint32_t addr) { assert(addr >= 1 && addr <= 4); return headers[addr-1]; }
uint32_t xd3d_backbuffer_data(void) { return 0x123400; }
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t h,uint32_t palette)
{ assert(h>=1 && h<=4);resolves[h-1]++;return (missing & (1u<<(h-1))) ? NULL : &textures[h-1]; }
void xv_ui_gxm_apply_texture_options(SceGxmTexture *t) { (void)t; }
unsigned xv_ui_gxm_texture_options_key(void) { return 0; }
static int uploaded_neutral;
int xv_ui_gxm_texture_neutral_detail(const SceGxmTexture *t) { return t==&textures[1] && uploaded_neutral; }
static int uploaded_opaque;
int xv_ui_gxm_texture_opaque(const SceGxmTexture *t) { return t == &textures[0] && uploaded_opaque; }
int sceGxmTextureSetMinFilter(SceGxmTexture *t,SceGxmTextureFilter v) { (void)t;(void)v;return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t,SceGxmTextureFilter v) { (void)t;(void)v;return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { (void)t;(void)v;return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t,SceGxmTextureAddrMode v) { (void)t;(void)v;return 0; }
SceGxmTextureType sceGxmTextureGetType(const SceGxmTexture *t)
{ return ((const uint32_t *)t)[0] ? SCE_GXM_TEXTURE_CUBE : SCE_GXM_TEXTURE_LINEAR; }
/* Give alpha/cube variants different active samplers, and deliberately include
 * a missing-metadata family. No assumption about normal vs alpha subsets. */
static unsigned metadata(const char *path)
{
    if(override_shaders)return 15;
    for(unsigned f=0;f<FS_KINDS;f++)if(!strcmp(path,FS_GXP[f]))return fallback_masks[f];
    uint32_t h=2166136261u;for(const char *p=path;*p;p++)h=(h^(unsigned char)*p)*16777619u;
    if(strstr(path,"DE099B17"))return 15;
    return (h>>12)&15;
}
unsigned xv_fshader_embedded_texture_mask(const char *path) { metadata_reads++;return metadata(path); }
static unsigned required(const cmd_t *c)
{
    unsigned mask=metadata(FS_GXP[c->fs_kind]);
    if(c->ps_entry<0)return mask;
    const xv_ps_entry_t *e=&xv_ps_table[c->ps_entry];
    for(unsigned i=0;i<XV_PS_TABLE_COUNT;i++) {
        const xv_ps_entry_t *p=&xv_ps_table[i];
        if(p->vs_fnv!=e->vs_fnv || p->ps_key!=e->ps_key)continue;
        char variant[160];snprintf(variant,sizeof variant,"%.*s_na.frag.gxp",(int)strlen(p->gxp)-9,p->gxp);
        mask|=p->cube_modes|metadata(p->gxp)|metadata(variant);
        if(p->ps_key==0x154066FDu) {
            snprintf(variant,sizeof variant,"%.*s_gt.frag.gxp",(int)strlen(p->gxp)-9,p->gxp);
            mask|=metadata(variant);
        }
    }
    return mask;
}
static void compare(int entry,unsigned fallback,unsigned bound,unsigned cube,unsigned lost,unsigned back,int immediate)
{
    cmd_t before={.fs_kind=fallback,.ps_entry=entry},after=before;
    xv_vs_desc_t d={.func_hash=entry<0?0:xv_ps_table[entry].vs_fnv};
    for(unsigned t=0;t<4;t++) {
        S.tex_guest[t]=(bound&(1u<<t))?t+1:0;
        headers[t][1]=(back&(1u<<t))?xd3d_backbuffer_data():0;
        headers[t][4]=(t+7)|((t+9)<<12);
        ((uint32_t *)&textures[t])[0]=!!(cube&(1u<<t));
        ((uint32_t *)&textures[t])[1]++; /* Same guest address, different live control words. */
    }
    missing=lost;
    xv_unused_textures_override(0);record_textures(&before,&d,immediate);
    memset(resolves,0,sizeof resolves);
    unsigned skipped=texture_stages_skipped;
    xv_unused_textures_override(1);record_textures(&after,&d,immediate);
    unsigned mask=required(&before),want_skip=0;
    assert(before.ps_entry==after.ps_entry);
    assert(!memcmp(before.texscale,after.texscale,sizeof before.texscale));
    for(unsigned t=0;t<4;t++) {
        if(mask&(1u<<t)) {
            assert(!memcmp(&before.tex[t],&after.tex[t],sizeof before.tex[t]));
            assert((before.previous_frame&(1u<<t))==(after.previous_frame&(1u<<t)));
            if(before.ntex>t && memcmp(&before.tex[t],&(SceGxmTexture){0},sizeof before.tex[t]))assert(after.ntex>t);
        } else {
            assert(!resolves[t] && !(after.previous_frame&(1u<<t)));
            if(bound&(1u<<t))want_skip++;
        }
    }
    assert(texture_stages_skipped-skipped==want_skip);
}
int main(int argc,char **argv)
{
    int disabled=argc>1 && !strcmp(argv[1],"disabled");
    override_shaders=argc>1 && !strcmp(argv[1],"override");
    setenv("XV_UNUSED_TEXTURES",disabled?"0":"1",1);
    cmd_t c={.ps_entry=-1,.fs_kind=FS_TEXMOD};
    assert(draw_texture_mask(&c)==(disabled||override_shaders?15:1));
    unsigned tests=0;
    for(unsigned f=0;f<FS_KINDS;f++)for(unsigned bound=0;bound<16;bound++)
        for(unsigned lost=0;lost<16;lost++)for(unsigned back=0;back<16;back++) {
            compare(-1,f,bound,back,lost,back,1);tests++;
        }
    for(unsigned entry=0;entry<XV_PS_TABLE_COUNT;entry++)for(unsigned f=0;f<FS_KINDS;f++)
        for(unsigned cube=0;cube<16;cube++) {
            compare(entry,f,15,cube,cube^15,cube,cube&1);tests++;
        }
    /* Warmed families have no repeated parameter queries. */
    unsigned reads=metadata_reads;
    for(unsigned entry=0;entry<XV_PS_TABLE_COUNT;entry++) {
        c.ps_entry=entry;assert(draw_texture_mask(&c)==required(&c));
    }
    assert(metadata_reads==reads);
    c.ps_entry=-1;
    xv_unused_textures_override(0);assert(draw_texture_mask(&c)==15);
    xv_unused_textures_override(-1);assert(draw_texture_mask(&c)==(disabled||override_shaders?15:1));
    xv_unused_textures_override(1);c.ps_entry=XV_PS_TABLE_COUNT;assert(draw_texture_mask(&c)==15);
    c.fs_kind=FS_KINDS;assert(draw_texture_mask(&c)==15);
    printf("PASS: %u differential recordings; all shader families, fallback/alpha/cube variants, missing/live textures, backbuffer substitutions, dependent scales and overrides\n",tests);
}
