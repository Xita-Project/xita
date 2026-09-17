/* Pump-only proof over retained commands. Include after cmdlist_t/g_rt.
 * Stores are optional only after an earlier backbuffer scene stored the same
 * attachment. The final span also requires the caller's explicit tail contract
 * and the linked program proof for the replay-owned performance overlay. */
#pragma once
#ifdef XV_DEPTH_STORE
#ifndef XV_DEPTH_STORE_DEFAULT
#define XV_DEPTH_STORE_DEFAULT 0
#endif
#if XV_DEPTH_STORE_DEFAULT != 0 && XV_DEPTH_STORE_DEFAULT != 1
#error XV_DEPTH_STORE_DEFAULT must be 0 or 1
#endif
enum { DS_OFF, DS_READY, DS_FIRST, DS_FINAL, DS_BOUNDS, DS_UI, DS_CLEAR,
       DS_WRITE, DS_STENCIL, DS_SHADER, DS_EMPTY, DS_READY_FINAL, DS_REASONS };
int xv_ui_gxm_depth_tail_readonly(unsigned frame);
/* Set before any rendering worker exists; live changes still require a drain. */
static unsigned g_depth_store_mode = XV_DEPTH_STORE_DEFAULT;
static uint32_t g_depth_store_reasons[DS_REASONS];
static uint64_t g_depth_store_pixels;
int xv_depth_store_available(void)
{
    const char *override=getenv("XV_SHADER_OVERRIDE"), *forced=getenv("XV_FS_FORCE");
    return (!override || atoi(override)==0) && (!forced || !*forced);
}
int xv_depth_store_enabled(void)
{ return __atomic_load_n(&g_depth_store_mode,__ATOMIC_ACQUIRE)!=0; }
/* Called by the recording owner only after the pump/GPU drain. */
void xv_depth_store_override(int enabled)
{ __atomic_store_n(&g_depth_store_mode,enabled>0,__ATOMIC_RELEASE); }

static int ds_draw_shader(const cmd_t *c)
{
    /* All possible fragment fallbacks are checked, without linking or I/O.
     * The captured entry already includes texture-kind routing. Alpha variants
     * can fall back to the base and then the heuristic program. */
    static uint8_t fallback[FS_KINDS], programs[XV_PS_TABLE_COUNT], depth;
    if(c->vs>=XV_MAX_VS || c->fs_kind>=FS_KINDS || c->ps_entry<0 ||
       (unsigned)c->ps_entry>=XV_PS_TABLE_COUNT)return 0;
    if(!depth)depth=1+xv_fshader_embedded_no_depth("app0:shaders/ps_28CF808C_07_na.frag.gxp");
    if(depth!=2)return 0;
    if(!fallback[c->fs_kind])fallback[c->fs_kind]=1+xv_fshader_embedded_no_depth(FS_GXP[c->fs_kind]);
    if(fallback[c->fs_kind]!=2)return 0;
    if(!programs[c->ps_entry]) {
        const xv_ps_entry_t *e=&xv_ps_table[c->ps_entry];
        size_t len=strlen(e->gxp);char path[160];
        int safe=xv_fshader_embedded_no_depth(e->gxp);
        if(len<9 || len+4>sizeof path || strcmp(e->gxp+len-9,".frag.gxp"))safe=0;
        else {
            snprintf(path,sizeof path,"%.*s_na.frag.gxp",(int)(len-9),e->gxp);
            safe=safe && xv_fshader_embedded_no_depth(path);
            if(e->ps_key==0x154066FDu) {
                snprintf(path,sizeof path,"%.*s_gt.frag.gxp",(int)(len-9),e->gxp);
                safe=safe && xv_fshader_embedded_no_depth(path);
            }
        }
        programs[c->ps_entry]=1+safe;
    }
    return programs[c->ps_entry]==2;
}
static int ds_list_valid(const cmdlist_t *l)
{
    if(l->ncmds>XV_MAX_CMDS || l->nui>sizeof l->ui/sizeof l->ui[0])return 0;
    for(unsigned i=0;i<l->ncmds;i++) {
        const cmd_t *c=&l->cmds[i];
        if(c->kind>1 || c->pass>XV_RT_SLOTS || (c->pass && !g_rt[c->pass-1].rt))return 0;
    }
    for(unsigned u=0;u<l->nui;u++) {
        unsigned t=l->ui[u].target;
        if(l->ui[u].before>l->ncmds || (u && l->ui[u].before<l->ui[u-1].before) ||
           t>XV_RT_SLOTS || (t && !g_rt[t-1].rt))return 0;
    }
    return 1;
}
static unsigned ds_scene(const cmdlist_t *l,unsigned i,unsigned u,int stored,int readonly_tail)
{
    if(!stored)return DS_FIRST;
    unsigned draws=0;
    for(;;) {
        int ui=u<l->nui && l->ui[u].before<=i;
        int done=i>=l->ncmds && !ui;
        unsigned target=ui?l->ui[u].target:done?0:l->cmds[i].pass;
        if(target)return draws?DS_READY:DS_EMPTY;
        /* No unrecorded writer may follow an accepted final span. Ordinary
         * recorded UI batches and clears remain on the original policy. */
        if(done)return readonly_tail && xv_ui_gxm_depth_tail_readonly(l->ui_frame) ? DS_READY_FINAL : DS_FINAL;
        if(ui)return DS_UI;
        const cmd_t *c=&l->cmds[i++];
        if(c->kind)return DS_CLEAR;
        if(c->depth_write)return DS_WRITE;
        if(c->stencil.enabled)return DS_STENCIL;
        if(!ds_draw_shader(c))return DS_SHADER;
        draws++;
    }
}
void xv_d3d_depth_store_report(void)
{
    if(!xv_depth_store_enabled() && !g_depth_store_reasons[DS_READY] && !g_depth_store_reasons[DS_READY_FINAL]) {
        memset(g_depth_store_reasons,0,sizeof g_depth_store_reasons);return;
    }
    XV_LOG("[depth-store] backbuffer scenes off/accepted/first/final/bounds/ui/clear/write/stencil/shader/empty/accepted-final %u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u; accepted logical samples %llu; forced-store disabled only, loads retained, explicit final-tail proof; no measured bandwidth claim\n",
        g_depth_store_reasons[DS_OFF],g_depth_store_reasons[DS_READY],g_depth_store_reasons[DS_FIRST],
        g_depth_store_reasons[DS_FINAL],g_depth_store_reasons[DS_BOUNDS],g_depth_store_reasons[DS_UI],
        g_depth_store_reasons[DS_CLEAR],g_depth_store_reasons[DS_WRITE],g_depth_store_reasons[DS_STENCIL],
        g_depth_store_reasons[DS_SHADER],g_depth_store_reasons[DS_EMPTY],g_depth_store_reasons[DS_READY_FINAL],(unsigned long long)g_depth_store_pixels);
    memset(g_depth_store_reasons,0,sizeof g_depth_store_reasons);g_depth_store_pixels=0;
}
#define XV_DS_SETUP(l,tail) int ds_enabled=xv_depth_store_enabled()&&xv_depth_store_available(); \
    int ds_valid=ds_enabled?ds_list_valid(l):0, ds_stored=0, ds_tail=!!(tail)
#define XV_DS_STORED(t) do {if(!(t))ds_stored=1;} while(0)
#define XV_DS_SURFACE(l,i,u,t,d,w,h) do {if(!(t)) { \
    unsigned why=!ds_enabled?DS_OFF:!ds_valid?DS_BOUNDS:ds_scene(l,i,u,ds_stored,ds_tail); \
    int accepted=why==DS_READY || why==DS_READY_FINAL; \
    g_depth_store_reasons[why]++; \
    if(accepted)g_depth_store_pixels+=(uint64_t)(w)*(h); \
    sceGxmDepthStencilSurfaceSetForceStoreMode(d,accepted? \
        SCE_GXM_DEPTH_STENCIL_FORCE_STORE_DISABLED:SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED); \
}} while(0)
#else
#define XV_DS_SETUP(l,tail) ((void)(tail))
#define XV_DS_STORED(t) ((void)0)
#define XV_DS_SURFACE(l,i,u,t,d,w,h) ((void)0)
#endif
