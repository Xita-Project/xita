#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "../../runtime/xv_visibility.h"
#include "../../runtime/xv_render_profile.h"
#include "../../runtime/xv_stencil.h"
#define XV_NUM_LISTS 3
#define UI_FRAMES 3
#define XV_MAX_CMDS 32
#define XV_RT_SLOTS 8
#define XV_CLEAR_SLOTS 64
#define XV_LEGACY_CLEAR_SLOTS 16
#define XV_MAX_VS 67
#define FS_KINDS 4
#define XV_PS_TABLE_COUNT 8
#define SCE_OK 0
#define SCE_GXM_FRAGMENT_PROGRAM 1
#define SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED 1
#define SCE_GXM_DEPTH_STENCIL_FORCE_STORE_DISABLED 0
#define SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED 1
typedef struct {int valid,type,depth;} SceGxmProgram;
static unsigned metadata_calls;
static int sceGxmProgramCheck(const SceGxmProgram *p){metadata_calls++;return p->valid?0:-1;}
static int sceGxmProgramGetType(const SceGxmProgram *p){return p->type;}
static int sceGxmProgramIsDepthReplaceUsed(const SceGxmProgram *p){return p->depth;}
#include "programs.h"
#include "shader.inc"
static const unsigned ui_program=1;
static struct {
 int ready;
 struct {const void *fprog;unsigned replaces_depth;} clear_fs,settings_fs;
} g;
#include "ui_tail.inc"
static const char *FS_GXP[]={"app0:shaders/f0.frag.gxp","app0:shaders/f1.frag.gxp","app0:shaders/f2.frag.gxp","app0:shaders/f3.frag.gxp"};
typedef struct {const char *gxp;unsigned ps_key;} xv_ps_entry_t;
static const xv_ps_entry_t xv_ps_table[]={
 {"app0:shaders/p0.frag.gxp",0},{"app0:shaders/p1.frag.gxp",0},{"app0:shaders/p2.frag.gxp",0},
 {"app0:shaders/p3.frag.gxp",0x154066FD},{"app0:shaders/p4.frag.gxp",0},{"app0:shaders/p5.frag.gxp",0},
 {"app0:shaders/p6.frag.gxp",0},{"app0:shaders/p7.frag.gxp",0x154066FD}};
typedef struct {volatile unsigned *address;unsigned value;} SceGxmNotification;
typedef int SceGxmContext;
typedef int SceGxmSyncObject;
typedef int SceGxmRenderTarget;
typedef struct {unsigned id;} SceGxmColorSurface;
typedef struct {unsigned load,store,id,sentinel;} SceGxmDepthStencilSurface;
typedef struct {
 unsigned kind,pass,visibility,index_count,depth_write,clear_flags,value,fs_kind,vs;
 int ps_entry;xv_stencil stencil;
 /* Oracle-only actual fragment behavior, independent of the admission check. */
 unsigned export_depth;
} cmd_t;
typedef struct {
 cmd_t cmds[XV_MAX_CMDS];unsigned ncmds;
 struct {unsigned before,frame,batch,target;} ui[16];unsigned nui,ui_frame;
 struct {uint16_t result_slot;uint32_t serial,guest_area,render_area;} visibility[512];
 unsigned nvisibility,active_visibility;
} cmdlist_t;
static cmdlist_t lists[3],*g_lists[]={lists,lists+1,lists+2};
typedef struct {SceGxmRenderTarget *rt;SceGxmColorSurface color;SceGxmDepthStencilSurface depth;void *mem;unsigned w,h;} rt_alias_t;
static SceGxmRenderTarget targets[9];static rt_alias_t g_rt[8];
void xv_logf(const char *fmt,...){(void)fmt;}
#define XV_LOG(...) xv_logf(__VA_ARGS__)
#include "../../runtime/xv_visibility_placement.h"
#include "../../runtime/xv_query_boundary.h"
static void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *d,int v){d->store=v;}
static void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *d,int v){d->load=v;}
#include "../../runtime/xv_depth_store.h"
typedef struct {unsigned kind,target;SceGxmDepthStencilSurface depth;cmd_t command;SceGxmNotification fence;} event_t;
static event_t events[256];static unsigned nevents,executed,opened,begins,ends,finishes,fail_begin,fail_end;
static unsigned active,mem_depth[9],mem_stencil[9],colors[9],tile_depth,tile_stencil,queries[512],notice;
static unsigned no_store;
static void event(unsigned kind,unsigned target){assert(nevents<256);events[nevents++]=(event_t){.kind=kind,.target=target};}
static void gpu(void)
{
 for(;executed<nevents;executed++) {
  event_t *e=&events[executed];cmd_t *c=&e->command;
  if(e->kind==1){active=e->target;tile_depth=e->depth.load?mem_depth[active]:1;tile_stencil=e->depth.load?mem_stencil[active]:0;}
  else if(e->kind==2) {
   if(e->depth.store){mem_depth[active]=tile_depth;mem_stencil[active]=tile_stencil;}
   if(e->fence.address)*e->fence.address=e->fence.value;
  } else if(e->kind==3) {
   if(c->kind){if(c->clear_flags&2)tile_depth=c->value;if(c->clear_flags&4)tile_stencil=c->value;}
   else {if(c->depth_write || c->export_depth)tile_depth=c->value;if(c->stencil.enabled)tile_stencil=c->value;
     if(c->visibility)queries[c->visibility-1]=tile_depth*257+tile_stencil;}
   colors[active]=colors[active]*33+c->value;
  } else if(e->kind==4) {
   colors[active]=colors[active]*33+e->target;
   if(c->export_depth)tile_depth=e->target;
  }
 }
}
static SceGxmDepthStencilSurface current_depth;
static int sceGxmBeginScene(SceGxmContext *c,int f,SceGxmRenderTarget *rt,void *a,void *b,SceGxmSyncObject *sync,const SceGxmColorSurface *color,const SceGxmDepthStencilSurface *depth)
{
 assert(!opened);begins++;if(begins==fail_begin)return -1;
 opened=1;current_depth=*depth;event(1,color->id);events[nevents-1].depth=*depth;
 no_store+=!depth->store;return 0;
}
static int sceGxmEndScene(SceGxmContext *c,void *v,const SceGxmNotification *f)
{
 assert(opened);opened=0;ends++;event(2,0);events[nevents-1].depth=current_depth;
 if(f && ends!=fail_end)events[nevents-1].fence=*f;
 return ends==fail_end?-1:0;
}
static void sceGxmFinish(SceGxmContext *c){assert(!opened);finishes++;gpu();}
static void sceGxmSetViewport(SceGxmContext *c,float x,float xs,float y,float ys,float z,float zs){assert(opened&&x==xs&&y==-ys);}
static void visibility_draw_state(SceGxmContext *c,cmdlist_t *l,const cmd_t *cmd){assert(!cmd);}
static void render_range(SceGxmContext *c,cmdlist_t *l,unsigned a,unsigned b,unsigned *slot,uint32_t frame,unsigned limit)
{assert(opened);for(unsigned i=a;i<b;i++){event(3,i);events[nevents-1].command=l->cmds[i];}}
void xv_ui_gxm_replay_batch(SceGxmContext *c,unsigned frame,unsigned batch,const void *target){assert(opened);event(4,batch);}
void xv_ui_gxm_replay_overlay(SceGxmContext *c,unsigned frame)
{assert(opened);if(g.ready && frame<UI_FRAMES){event(4,99);events[nevents-1].command.export_depth=g.clear_fs.replaces_depth;}}
#include "replay.inc"
typedef struct {unsigned depth[9],stencil[9],color[9],query[512],notice,events,finishes,failed;uint64_t trace;unsigned accepted;} receipt;
static receipt run(unsigned mode,unsigned variant,unsigned frame,unsigned scaled)
{
 memset(lists,0,sizeof lists);memset(events,0,sizeof events);memset(queries,0,sizeof queries);memset(colors,0,sizeof colors);
 memset(&g,0,sizeof g);g.ready=1;g.clear_fs.fprog=g.settings_fs.fprog=&ui_program;
 nevents=executed=opened=begins=ends=finishes=fail_begin=fail_end=no_store=notice=0;
 for(unsigned i=0;i<9;i++)mem_depth[i]=100+i,mem_stencil[i]=200+i;
 for(unsigned i=0;i<8;i++)g_rt[i]=(rt_alias_t){.rt=&targets[i+1],.color={i+1},.depth={1,1,i+1,0xaaaa},.w=64,.h=64};
 cmdlist_t *l=g_lists[frame%3];unsigned pass[]={0,1,0,0,1,0};l->ncmds=6;
 for(unsigned i=0;i<6;i++)l->cmds[i]=(cmd_t){.pass=pass[i],.value=7+i,.index_count=3};
 l->cmds[0].depth_write=1;l->cmds[1].depth_write=1;l->cmds[4].depth_write=1;
 l->cmds[0].visibility=1;l->nvisibility=1;l->visibility[0].serial=17;
 /* Queries in later packets can read the depth left by earlier scenes. */
 l->cmds[2].visibility=2;l->cmds[5].visibility=3;l->nvisibility=3;l->visibility[1].serial=18;l->visibility[2].serial=19;
 if(variant==1)l->cmds[3].depth_write=1;
 if(variant==2)l->cmds[3].stencil.enabled=1;
 if(variant==3)l->cmds[3].kind=1,l->cmds[3].clear_flags=2;
 if(variant==4)l->cmds[3].kind=1,l->cmds[3].clear_flags=4;
 if(variant==5)l->cmds[3].kind=1,l->cmds[3].clear_flags=1;
 if(variant>=6 && variant<=11)l->cmds[3].ps_entry=variant-5,l->cmds[3].export_depth=1;
 if(variant==12)l->cmds[3].fs_kind=1,l->cmds[3].export_depth=1;
 if(variant==13)l->nui=1,l->ui[0].before=3,l->ui[0].batch=8;
 if(variant==14)l->cmds[3].ps_entry=-1;
 if(variant==15)l->cmds[3].kind=2;
 if(variant==16)l->cmds[3].vs=XV_MAX_VS;
 if(variant==17)fail_begin=4;
 if(variant==18)fail_end=3;
 if(variant==19)fail_end=1;
 if(variant==20)l->nui=1,l->ui[0].before=6,l->ui[0].batch=8; /* final UI */
 if(variant==21)l->cmds[2].ps_entry=l->cmds[3].ps_entry=7;
 if(variant==22)l->cmds[0].pass=1; /* first backbuffer appears later */
 if(variant==23)l->cmds[5].depth_write=1;
 if(variant==24)setenv("XV_SHADER_OVERRIDE","1",1);
 if(variant==25)setenv("XV_FS_FORCE","tex0",1);
 if(variant==26)l->cmds[3].fs_kind=FS_KINDS;
 if(variant==27)l->cmds[3].ps_entry=XV_PS_TABLE_COUNT;
 if(variant==28)l->cmds[4].pass=9; /* invalid later target */
 if(variant==29)l->nui=2,l->ui[0].before=6,l->ui[1].before=3;
 if(variant>=30 && variant<35) {
   /* Several read-only returns must keep the earlier stored memory valid.
    * A writable return in the middle establishes a new value for later loads. */
   unsigned passes[]={0,1,0,1,0,2,0,1,0};l->ncmds=9;
   for(unsigned j=0;j<9;j++)l->cmds[j]=(cmd_t){.pass=passes[j],.value=10+j,.index_count=3,
     .depth_write=j==0 || passes[j]!=0,.visibility=passes[j]?0:1};
   if(variant==31)l->cmds[4].depth_write=1;
   if(variant>=32){l->nui=1;l->ui[0].before=variant==32?2:3;l->ui[0].target=variant==33?0:2;l->ui[0].batch=8;}
 }
 int readonly_tail=variant>=35;
 if(variant==36)readonly_tail=0;
 if(variant==37)g.clear_fs.replaces_depth=1;
 if(variant==38)g.settings_fs.replaces_depth=1;
 if(variant==39)g.settings_fs.fprog=NULL;
 if(variant==40)l->cmds[5].depth_write=1;
 if(variant==41)l->nui=1,l->ui[0].before=6,l->ui[0].batch=8;
 if(variant==42)l->cmds[5].ps_entry=-1;
 if(variant==43)l->ui_frame=UI_FRAMES;
 if(variant==44)g.ready=0;
 if(variant==45)l->ncmds=5; /* final span contains only the proved tail */
 if(variant==46)g.clear_fs.fprog=NULL;
 if(variant==47)l->cmds[5].stencil.enabled=1;
 cmdlist_t before=*l;
 /* mode2 deliberately exercises the production static startup initializer. */
 if(mode<2)xv_depth_store_override(mode);
#ifdef XV_QUERY_BOUNDARY
 if(variant==48) {
   /* Last writer is in the admitted intermediate continuation (commands2/3),
    * not the first scene whose store is mandatory. */
   l->cmds[5].visibility=0;l->nvisibility=2;
 } else {
   l->cmds[2].visibility=l->cmds[5].visibility=0;l->nvisibility=1;
 }
 before=*l;
 if(xv_d3d_query_boundary_prepare(frame,1)){SceGxmNotification f={&notice,999};xv_d3d_query_boundary_arm(frame,&f);}
#endif
 SceGxmDepthStencilSurface depth={0,0,0,0x11223344},original=depth;SceGxmColorSurface color={0};
 int failed=xv_d3d_render_targets(NULL,frame,targets,NULL,&color,&depth,scaled?640:960,scaled?360:544,readonly_tail)<0;
 if(opened){/* Independent external settings operation; unknown depth exporters
              must keep stores even when the caller requests final admission. */
   event(4,123);events[nevents-1].command.export_depth=g.settings_fs.replaces_depth;sceGxmEndScene(NULL,NULL,NULL);}
 sceGxmFinish(NULL);assert(!memcmp(&depth,&original,sizeof depth)&&!memcmp(l,&before,sizeof before));
#ifdef XV_QUERY_BOUNDARY
 if(variant==48) {
   unsigned query_ends=0;
   assert(g_query_boundary_plans[frame%3].command==4 && notice==999);
   for(unsigned j=0;j<nevents;j++)if(events[j].kind==2 && events[j].fence.address) {
     query_ends++;assert(events[j].depth.store==!xv_depth_store_enabled());
   }
   assert(query_ends==1);
 }
#endif
 receipt r={.notice=notice,.events=nevents,.finishes=finishes,.failed=failed,.accepted=no_store,.trace=1469598103934665603ull};
 memcpy(r.depth,mem_depth,sizeof mem_depth);memcpy(r.stencil,mem_stencil,sizeof mem_stencil);
 memcpy(r.color,colors,sizeof colors);memcpy(r.query,queries,sizeof queries);
 for(unsigned i=0;i<nevents;i++){event_t e=events[i];e.depth.store=0;e.fence.address=NULL;
   const unsigned char *p=(const void *)&e;for(unsigned j=0;j<sizeof e;j++)r.trace=(r.trace^p[j])*1099511628211ull;}
 unsetenv("XV_SHADER_OVERRIDE");unsetenv("XV_FS_FORCE");return r;
}
int main(void)
{
#ifdef TEST_DEPTH_STORE_STARTUP
 /* No control call or shader metadata warmup precedes this real replay. */
 assert(xv_depth_store_enabled()==XV_DEPTH_STORE_DEFAULT);
 receipt initial=run(2,35,0,0);
 assert(initial.accepted==(XV_DEPTH_STORE_DEFAULT?2u:0u) && !initial.failed);
 for(unsigned variant=24;variant<=25;variant++) {
   receipt declined=run(2,variant,0,0);assert(!declined.accepted && !declined.failed);
   assert(xv_depth_store_enabled()==XV_DEPTH_STORE_DEFAULT);
 }
 receipt original=run(0,35,0,0);initial.accepted=0;assert(!memcmp(&initial,&original,sizeof initial));
 receipt a=run(0,48,UINT32_MAX,0),b=run(1,48,UINT32_MAX,0);
 assert(!a.accepted && b.accepted==2);b.accepted=0;assert(!memcmp(&a,&b,sizeof a));
 printf("PASS actual startup default%d without setter, unavailable configurations, original replay equality, query fence on omitted continuation\n",XV_DEPTH_STORE_DEFAULT);
 return 0;
#endif
 assert(!xv_depth_store_enabled());xv_depth_store_override(1);xv_depth_store_override(-1);assert(!xv_depth_store_enabled());
 assert(!xv_fshader_embedded_no_depth(NULL)&&!xv_fshader_embedded_no_depth("missing"));
 unsigned frames[]={0,2,3,UINT32_MAX,0};unsigned checked=0;
 for(unsigned k=0;k<sizeof frames/sizeof frames[0];k++)for(unsigned scaled=0;scaled<2;scaled++)for(unsigned variant=0;variant<49;variant++) {
   receipt a=run(0,variant,frames[k],scaled),b=run(1,variant,frames[k],scaled);
   assert(!a.accepted);unsigned accepted=b.accepted;b.accepted=0;
   if(memcmp(&a,&b,sizeof a)){fprintf(stderr,"mismatch variant %u frame %u scaled %u\n",variant,frames[k],scaled);abort();}
   if(variant==0||variant==20||variant==21||variant==23)assert(accepted==1);
   if(variant==1||variant==2||variant==3||variant==4||variant==5||(variant>=6&&variant<=16)||variant==19||variant==22||(variant>=24&&variant<30))assert(!accepted);
   if(variant>=30 && variant<35)assert(accepted==((variant==31 || variant==33)?2:3));
   if(variant>=35)assert(accepted==((variant==35 || variant==45 || variant==48)?2:1));
   checked++;
 }
 /* Bounds failure is declined before any command access by the proof. The
  * old renderer's malformed-list behavior itself is not changed here. */
 lists[0].ncmds=XV_MAX_CMDS+1;assert(!ds_list_valid(lists));lists[0].ncmds=0;
 lists[0].nui=17;assert(!ds_list_valid(lists));lists[0].nui=0;
 xv_d3d_depth_store_report();
 printf("PASS: %u production OFF/ON RTT comparisons: stored depth/stencil/color/query equality, ordered events, UI/clear/exports/fallbacks, first/final, wrap, native/scaled, failure drains; metadata calls %u\n",checked,metadata_calls);
}
