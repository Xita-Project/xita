/* Runs the real production replay functions. The independent oracle enumerates
 * scene events, while the observer receives only actual EndScene callbacks. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#define XV_NUM_LISTS 3
#define XV_MAX_CMDS 2048
#define XV_RT_SLOTS 8
#define XV_CLEAR_SLOTS 64
#define XV_LEGACY_CLEAR_SLOTS 16
#include "../../runtime/xv_visibility.h"
#include "../../runtime/xv_render_profile.h"
#define SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED 1
#define SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED 1
typedef struct { volatile unsigned *address;unsigned value; } SceGxmNotification;
typedef int SceGxmContext;
typedef int SceGxmSyncObject;
typedef int SceGxmRenderTarget;
typedef int SceGxmColorSurface;
typedef struct { unsigned store,load; } SceGxmDepthStencilSurface;
typedef struct { uint8_t kind,pass;uint16_t visibility;uint32_t index_count; } cmd_t;
typedef struct {
    cmd_t cmds[XV_MAX_CMDS];unsigned ncmds;
    struct { unsigned before,frame,batch,target; } ui[1024];unsigned nui,ui_frame;
    struct { uint16_t result_slot;uint32_t serial,guest_area,render_area; } visibility[512];
    unsigned nvisibility,active_visibility;int visibility_gpu_ready;
} cmdlist_t;
static cmdlist_t lists[XV_NUM_LISTS],before,*g_lists[]={&lists[0],&lists[1],&lists[2]};
typedef struct { SceGxmRenderTarget *rt;SceGxmColorSurface color;SceGxmDepthStencilSurface depth;void *mem;unsigned w,h; } rt_alias_t;
static SceGxmRenderTarget targets[9];static rt_alias_t g_rt[8];
static uint64_t clock_us;
void xv_logf(const char *fmt,...) {(void)fmt;}
static char report[10000];static unsigned report_n,report_lines;
static void logf_test(const char *fmt,...)
{
    va_list ap;va_start(ap,fmt);
    int n=vsnprintf(report+report_n,sizeof report-report_n,fmt,ap);va_end(ap);
    assert(n>=0 && (unsigned)n<sizeof report-report_n);report_n+=(unsigned)n;report_lines++;
}
#define XV_LOG(...) logf_test(__VA_ARGS__)
#include "../../runtime/xv_visibility_placement.h"
#include "../../runtime/xv_query_boundary.h"
#include "../../runtime/xv_depth_store.h"
uint64_t xk_os_monotonic_us(void) {return ++clock_us;}
static uint64_t trace=1469598103934665603ull;
static unsigned opened,ends,begins,draws,uis,finishes,fail_begin,fail_end;
static void event(unsigned kind,unsigned value) {trace^=((uint64_t)kind<<32)|value;trace*=1099511628211ull;}
static void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *d,int value) {d->store=value;}
static void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *d,int value) {d->load=value;}
static int sceGxmBeginScene(SceGxmContext *c,unsigned flags,const SceGxmRenderTarget *rt,void *v,void *vs,SceGxmSyncObject *sync,const SceGxmColorSurface *color,const SceGxmDepthStencilSurface *depth)
{
    (void)c;(void)flags;(void)v;(void)vs;(void)sync;(void)color;(void)depth;
    assert(!opened);begins++;event(1,(unsigned)(rt-targets));
    if(begins==fail_begin)return -1;opened=1;return 0;
}
#ifdef XV_QUERY_BOUNDARY
void test_query_notification(const SceGxmNotification *f);
#endif
static int sceGxmEndScene(SceGxmContext *c,void *v,const SceGxmNotification *f)
{(void)c;(void)v;assert(opened);opened=0;ends++;event(2,ends);
#ifdef XV_QUERY_BOUNDARY
 if(f && ends!=fail_end)test_query_notification(f);
#else
 assert(!f);
#endif
 return ends==fail_end?-1:0;}
static void sceGxmFinish(SceGxmContext *c) {(void)c;finishes++;event(3,finishes);}
static void sceGxmSetViewport(SceGxmContext *c,float x,float xs,float y,float ys,float z,float zs)
{(void)c;(void)x;(void)xs;(void)y;(void)ys;(void)z;(void)zs;assert(opened);}
static void visibility_draw_state(SceGxmContext *c,cmdlist_t *l,const cmd_t *cmd)
{(void)c;(void)l;assert(!cmd);}
static void render_range(SceGxmContext *c,cmdlist_t *l,unsigned first,unsigned end,unsigned *clear,uint32_t frame,unsigned limit)
{
    (void)c;(void)clear;(void)frame;(void)limit;assert(opened);
    for(unsigned i=first;i<end;i++) {event(4,i);event(5,l->cmds[i].visibility);draws++;}
}
void xv_ui_gxm_replay_batch(SceGxmContext *c,unsigned frame,unsigned batch,const void *target)
{(void)c;(void)frame;(void)target;assert(opened);uis++;event(6,batch);}
void xv_ui_gxm_replay_overlay(SceGxmContext *c,unsigned frame)
{(void)c;(void)frame;assert(opened);event(7,0);}
#include "placement_replay.inc"
static void reset(unsigned frame)
{
    memset(lists,0,sizeof lists);memset(g_rt,0,sizeof g_rt);
    for(unsigned i=0;i<8;i++){g_rt[i].rt=&targets[i+1];g_rt[i].w=960;g_rt[i].h=544;}
    opened=ends=begins=draws=uis=finishes=fail_begin=fail_end=0;
    report_n=report_lines=0;report[0]=0;
#ifdef XV_VISIBILITY_PLACEMENT
    memset(g_visibility_placement,0,sizeof g_visibility_placement);
    memset(&g_visibility_placement_totals,0,sizeof g_visibility_placement_totals);
#endif
    lists[frame%3].visibility_gpu_ready=1;
}
static void slot(cmdlist_t *l,unsigned n,unsigned result,unsigned serial)
{assert(n<512);if(l->nvisibility<=n)l->nvisibility=n+1;l->visibility[n].result_slot=result;l->visibility[n].serial=serial;}
static void cmd(cmdlist_t *l,unsigned pass,unsigned query,unsigned count)
{assert(l->ncmds<XV_MAX_CMDS);l->cmds[l->ncmds++]=(cmd_t){0,pass,query,count};}
static void ui(cmdlist_t *l,unsigned at,unsigned pass)
{assert(l->nui<1024);unsigned u=l->nui++;l->ui[u].before=at;l->ui[u].target=pass;l->ui[u].batch=u;}
#ifdef XV_VISIBILITY_PLACEMENT
/* Enumerate the actual ordered event stream independently: each event carries
 * its scene, each writer carries its event scene. No use of observer cursors. */
static void oracle(const cmdlist_t *l)
{
    unsigned cmd_scene[XV_MAX_CMDS],scene=0,target=UINT32_MAX,i=0,u=0;
    for(;;) {
        int is_ui=u<l->nui && l->ui[u].before<=i;
        int done=i==l->ncmds && !is_ui;
        unsigned next=is_ui?l->ui[u].target:done?0:l->cmds[i].pass;
        if(next!=target){scene++;target=next;}
        if(done)break;
        if(is_ui)u++;else cmd_scene[i++]=scene;
    }
    unsigned last=0,first=UINT32_MAX,writers=0;
    for(i=0;i<l->ncmds;i++)if(!l->cmds[i].kind && l->cmds[i].visibility){
        writers++;if(cmd_scene[i]<first)first=cmd_scene[i];if(cmd_scene[i]>last)last=cmd_scene[i];
    }
    const xv_visibility_placement_totals *t=&g_visibility_placement_totals;
    assert(t->packets==1 && !t->errors && !t->unsupported);
    assert(t->closed==scene-1 && t->writers==writers);
    if(!writers){assert(t->no_query+t->no_writer==1);return;}
    if(last==scene){assert(t->last_final==1 && !t->boundary);assert(t->all_final==(first==scene));return;}
    assert(t->boundary==1 && t->cut_scene==last);
    uint64_t td=0,ti=0;
    unsigned after=0;
    for(i=0;i<l->ncmds;i++)if(cmd_scene[i]<=last)after=i+1;
    for(i=after;i<l->ncmds;i++)if(!l->cmds[i].kind){td++;ti+=l->cmds[i].index_count;}
    /* Count UI events whose scene follows the last writer's scene. */
    unsigned tu=0;scene=0;target=UINT32_MAX;i=u=0;
    while(i<l->ncmds || u<l->nui){
        int is_ui=u<l->nui && l->ui[u].before<=i;
        unsigned next=is_ui?l->ui[u].target:l->cmds[i].pass;
        if(next!=target){scene++;target=next;}
        if(is_ui){tu+=scene>last;u++;}else i++;
    }
    assert(t->tail_draws==td && t->tail_indices==ti && t->tail_ui==tu);
    assert(t->cut_command==after);
    assert(t->tail_scenes==t->closed-last+1);
}
#endif
static void run(unsigned frame,int expected)
{
    cmdlist_t *l=&lists[frame%3];before=*l;
    XV_VP_BEGIN(l,frame);
    SceGxmDepthStencilSurface depth={0};
    int result=xv_d3d_render_targets(NULL,frame,&targets[0],NULL,NULL,&depth,960,544,0);
    assert((result<0)==(expected<0));
    XV_VP_COMPLETE(l,frame);assert(!memcmp(&before,l,sizeof *l));
#ifdef XV_VISIBILITY_PLACEMENT
    if(!expected)oracle(l);else assert(g_visibility_placement_totals.errors==1);
#endif
    /* Main normally closes the still-open final scene. It is intentionally not
     * observed as an intermediate RTT boundary. */
    if(opened)sceGxmEndScene(NULL,NULL,NULL);
}
static void scenarios(void)
{
    reset(0);cmdlist_t *l=&lists[0];slot(l,0,4,11);
    cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,0,12);ui(l,1,0);ui(l,2,1);run(0,0);
    /* Later view/query writer forbids the earlier target transition. Reused
     * numerical ID (same result slot) still has independent query slots. */
    reset(1);l=&lists[1];slot(l,0,4,11);slot(l,1,4,12);
    cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,2,12);run(1,0);
#ifdef XV_VISIBILITY_PLACEMENT
    assert(g_visibility_placement_totals.reused==1 && g_visibility_placement_totals.writer_slots==2);
    assert(g_visibility_placement_totals.last_final==1 && !g_visibility_placement_totals.all_final);
#endif
    /* Query in a final offscreen scene, followed by mandatory empty backbuffer. */
    reset(2);l=&lists[2];slot(l,0,0,1);cmd(l,1,1,0);run(2,0);
#ifdef XV_VISIBILITY_PLACEMENT
    assert(g_visibility_placement_totals.empty_tail==1 && g_visibility_placement_totals.tail_scenes==1);
#endif
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,1,1,6);ui(l,1,2);ui(l,1,0);run(0,0);
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,0,3);cmd(l,1,0,6);cmd(l,0,1,9);run(0,0);
    reset(UINT32_MAX);l=&lists[UINT32_MAX%3];slot(l,0,0,UINT32_MAX);
    cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,1,12);run(UINT32_MAX,0); /* same slot spans scenes */
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);run(0,0); /* frame and serial wrap */
    reset(0);run(0,0); /* no-query, empty scene */
    reset(0);l=&lists[0];slot(l,0,0,0);run(0,0); /* allocated, never issued, no writers */
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);l->cmds[0].kind=1;run(0,0); /* clear disables visibility */
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);cmd(l,1,0,6);fail_begin=2;run(0,-1);
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);cmd(l,1,0,6);fail_end=1;run(0,-1);
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);cmd(l,1,0,6);g_rt[0].rt=NULL;run(0,-1);
    /* Deterministic randomized view/target/UI order, compared to event oracle. */
    uint32_t seed=1;
    for(unsigned trial=0;trial<500;trial++) {
        reset(0);l=&lists[0];slot(l,0,0,1);slot(l,1,0,2);
        for(unsigned j=0;j<20;j++) {
            seed=seed*1664525u+1013904223u;
            if(seed&1)ui(l,j,(seed>>4)%3);
            cmd(l,(seed>>8)%3,(seed>>12)%5==0?1+(seed>>15)%2:0,seed);
        }
        run(0,0);
    }
}
#ifdef XV_VISIBILITY_PLACEMENT
static void metadata_only(void)
{
    /* Malformed metadata is not replayed by this fixture. The observer does not
     * repair or authorize an invalid production packet. ASan checks its bounds. */
    for(unsigned reason=0;reason<8;reason++) {
        reset(0);cmdlist_t *l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);
        if(reason==0)l->ncmds=XV_MAX_CMDS+1;
        if(reason==1)l->cmds[0].visibility=513;
        if(reason==2)l->visibility[0].serial=0;
        if(reason==3)l->cmds[0].pass=9;
        if(reason==4)ui(l,2,0);
        if(reason==5)l->cmds[0].kind=2;
        if(reason==6)l->active_visibility=1;
        if(reason==7)l->visibility[0].result_slot=512;
        before=*l;vp_begin(l,0);vp_replayed(0,0);vp_complete(l,0);
        assert(g_visibility_placement_totals.unsupported==1);
        assert(g_visibility_placement_totals.reasons[reason]==1);
        assert(!memcmp(l,&before,sizeof *l));
    }
    for(unsigned which=0;which<2;which++) {
        reset(0);cmdlist_t *l=&lists[0];if(which)l->nui=1025;else l->nvisibility=513;
        vp_begin(l,0);vp_complete(l,0);assert(g_visibility_placement_totals.reasons[0]==1);
    }
    reset(0);cmdlist_t *l=&lists[0];slot(l,511,511,UINT32_MAX);
    for(unsigned i=0;i<XV_MAX_CMDS;i++)cmd(l,0,512,UINT32_MAX);
    for(unsigned i=0;i<1024;i++)ui(l,XV_MAX_CMDS,0);
    run(0,0);assert(g_visibility_placement_totals.indices==(uint64_t)UINT32_MAX*XV_MAX_CMDS);
    assert(g_visibility_placement_totals.scan_cmds==XV_MAX_CMDS);
    reset(0);l=&lists[0];slot(l,0,0,1);
    cmd(l,1,1,UINT32_MAX);
    for(unsigned i=1;i<XV_MAX_CMDS;i++)cmd(l,0,0,UINT32_MAX);
    for(unsigned i=0;i<1024;i++)ui(l,XV_MAX_CMDS,0);
    run(0,0);assert(g_visibility_placement_totals.scan_cmds==2*XV_MAX_CMDS-1);
    assert(g_visibility_placement_totals.tail_indices==(uint64_t)UINT32_MAX*(XV_MAX_CMDS-1));
    assert(g_visibility_placement_totals.tail_ui==1024);
    reset(0);l=&lists[0];vp_begin(l,0);vp_complete(l,0);assert(g_visibility_placement_totals.not_replayed==1);
    reset(0);l=&lists[0];vp_begin(l,0);vp_begin(l,3);vp_complete(l,0);vp_replayed(3,0);vp_complete(l,3);
    assert(g_visibility_placement_totals.abandoned==1 && g_visibility_placement_totals.orphan==1);
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);l->visibility_gpu_ready=0;run(0,0);
    assert(g_visibility_placement_totals.no_buffer==1);
    /* Native replay is one still-open final scene, not an intermediate fence. */
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);vp_begin(l,0);opened=1;xv_d3d_render(NULL,0);vp_complete(l,0);
    assert(g_visibility_placement_totals.last_final==1 && g_visibility_placement_totals.all_final==1);
    sceGxmEndScene(NULL,NULL,NULL);
    /* Actual formatter/cadence: 60 packet completions, no query/GPU state edits. */
    reset(0);l=&lists[0];slot(l,0,0,1);cmd(l,0,1,6);cmd(l,1,0,9);cmd(l,0,0,12);
    for(unsigned i=0;i<60;i++) {
        vp_begin(l,i*3);SceGxmDepthStencilSurface d={0};
        assert(!xv_d3d_render_targets(NULL,i*3,&targets[0],NULL,NULL,&d,960,544,0));
        vp_complete(l,i*3);sceGxmEndScene(NULL,NULL,NULL);
    }
    assert(report_lines==6 && !g_visibility_placement_totals.packets);
    assert(strstr(report,"60 packets: no-query 0 slots-no-writer 0 boundary 60"));
    assert(strstr(report,"remaining draws/indices/ui/scenes sums 120/1260/0/120"));
    fputs(report,stdout);
}
#endif
int main(void)
{
    scenarios();printf("shared-replay-trace=%016llx\n",(unsigned long long)trace);
#ifdef XV_VISIBILITY_PLACEMENT
    metadata_only();puts("placement ON: oracle, replay, errors, bounds and reporting passed");
#else
    assert(!clock_us);puts("placement OFF: no observer clock or output");
#endif
    return 0;
}
