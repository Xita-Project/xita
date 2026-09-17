/* Private scheduling model, not a production switch. Actual recorder/replay,
 * query lifecycle and RTT allocation are extracted by the companion driver.
 * The draw sink models owned payload reads and GXM completion, not shaders. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "constants.h"
#define XV_QUERY_BOUNDARY 1
#define XV_FLARE_QUERY_OVERLAP 1
#define XV_NUM_LISTS 3u
#define XV_MAX_CMDS 2048u
#define XV_RT_SLOTS 8u
#define XV_RT_BUDGET (16u*1024*1024)
#define XV_CLEAR_SLOTS 64u
#define XV_VISIBILITY_GPU_CORES 4u
#define XV_VISIBILITY_WORDS (512u*4)
#define XV_VISIBILITY_STRIDE 512u
#define XV_CONST_POOL 8192u
#define XV_MAX_VS 96u
#define XV_FRAME_CONSTANT_BYTES (XV_CONST_POOL*4u)
#define XV_FRAME_CONSTANT_ALLOC (XV_FRAME_CONSTANT_BYTES+4096u)
#define X_D3DCLEAR_TARGET 1
#define X_D3DCLEAR_ZBUFFER 2
#define X_D3DCLEAR_STENCIL 4
#define XV_LOG(...) log_ignore(__VA_ARGS__)
#define XV_ONCE(...) ((void)0)
#define XV_VP_BEGIN(...) ((void)0)
#define XV_VP_COMPLETE(...) ((void)0)
#define XV_VP_END(...) ((void)0)
#define XV_VP_ERROR(...) ((void)0)
#define XV_VP_REPLAYED(...) ((void)0)
#define XV_RENDER_END(t,x) (x)
#define XV_RENDER_CALL(t,x) (x)
#define xv_render_profile_stage(x) ((void)0)
#include "../../runtime/xv_visibility.h"
#include "../../runtime/xv_frame_slots.h"
static void log_ignore(const char *fmt,...) {(void)fmt;}
typedef int SceUID;
typedef int SceGxmContext;
typedef int SceGxmSyncObject;
typedef int SceGxmColorFormat;
typedef int SceGxmTextureFormat;
typedef struct { unsigned id; } SceGxmRenderTarget;
typedef struct { void *data; } SceGxmColorSurface;
typedef struct { unsigned load,store; } SceGxmDepthStencilSurface;
typedef struct { void *data; } SceGxmTexture;
typedef struct { unsigned width,height,multisampleMode;int driverMemBlock; } SceGxmRenderTargetParams;
typedef struct { volatile unsigned *address;unsigned value; } SceGxmNotification;
typedef struct { unsigned enabled,func,fail,depth_fail,pass,write_mask; } xv_stencil;
typedef struct {
    uint8_t kind,pass,clear_flags,clear_stencil;uint16_t visibility;
    unsigned index_count,depth_write,clear_color;float clear_z;
    xv_stencil stencil;
    unsigned const_off,const_n;
    const unsigned *vertices,*indices,*texture;
    unsigned nv,ni,shader,vs;
} cmd_t;
typedef struct {
    cmd_t cmds[XV_MAX_CMDS];unsigned ncmds,cur_pass,dropped,drop_commands;
    float consts[XV_CONST_POOL];unsigned nconsts;
    struct {unsigned before,frame,batch,target;} ui[1024];unsigned nui,ui_frame;
    struct {uint16_t result_slot;uint32_t serial,guest_area,render_area;} visibility[512];
    unsigned nvisibility,active_visibility,visibility_back_area,visibility_draw_slot;
    int visibility_gpu_ready;
} cmdlist_t;
typedef struct {
    uint32_t data,owner,last_frame;unsigned w,h,fmt,bytes,driver_bytes;
    SceUID uid;void *mem;SceGxmRenderTarget *rt;
    SceGxmColorSurface color;SceGxmDepthStencilSurface depth;SceGxmTexture tex;
} rt_alias_t;
static cmdlist_t lists[XV_NUM_LISTS],*g_lists[]={&lists[0],&lists[1],&lists[2]};
static rt_alias_t g_rt[XV_RT_SLOTS];static unsigned g_rt_bytes;
static uint32_t g_build_frame;
static cmdlist_t *cur_list(void);
static xv_visibility_result g_visibility_results[512];
static uint32_t *g_visibility_memory;static SceUID g_visibility_uid;
static struct {uint8_t *memory;SceUID uid;uint32_t frame;unsigned ready,copied;} g_frame_constants[3];
static uint64_t g_constant_upload_bytes;
static unsigned g_constant_upload_high,g_constant_upload_frames,g_constant_direct_draws,g_constant_ring_draws,g_constant_bad_draws;
static int g_frame_constant_layout[XV_MAX_VS];
typedef struct { unsigned bytes; } SceGxmProgram;
typedef struct { unsigned category,type,components,count,index,container; } SceGxmProgramParameter;
typedef struct { unsigned c_count;const char *gxp; } shader_desc;
typedef struct { const shader_desc *desc;SceGxmProgram *prog;SceGxmProgramParameter *p_c; } xv_vshader_t;
static SceGxmProgram vertex_program={16};
static SceGxmProgramParameter vertex_parameter={SCE_GXM_PARAMETER_CATEGORY_UNIFORM,SCE_GXM_PARAMETER_TYPE_F32,4,1,0,14};
static const shader_desc vertex_description={1,"fixture"};
static xv_vshader_t vertex_shader={&vertex_description,&vertex_program,&vertex_parameter};
#define REFLECT(name,field) static unsigned name(const SceGxmProgramParameter *p){return p->field;}
REFLECT(sceGxmProgramParameterGetCategory,category)
REFLECT(sceGxmProgramParameterGetType,type)
REFLECT(sceGxmProgramParameterGetComponentCount,components)
REFLECT(sceGxmProgramParameterGetArraySize,count)
REFLECT(sceGxmProgramParameterGetResourceIndex,index)
REFLECT(sceGxmProgramParameterGetContainerIndex,container)
static unsigned sceGxmProgramGetDefaultUniformBufferSize(const SceGxmProgram *p){return p->bytes;}
static const float *bound_constants;
static int sceGxmSetVertexDefaultUniformBuffer(SceGxmContext *c,const void *p){(void)c;bound_constants=p;return 0;}
static int sceGxmReserveVertexDefaultUniformBuffer(SceGxmContext *c,void **p){(void)c;(void)p;assert(0);return -1;}
static int sceGxmSetUniformDataF(void *p,const SceGxmProgramParameter *q,unsigned offset,unsigned count,const float *src)
{(void)p;(void)q;(void)offset;(void)count;(void)src;assert(0);return -1;}
static unsigned blocks_n,targets_n,alloc_failure,begin_failure,end_failure;
static void *blocks[4096];
static SceUID sceKernelAllocMemBlock(const char *name,int type,unsigned bytes,void *p)
{(void)name;(void)type;(void)p;if(alloc_failure)return -1;assert(++blocks_n<4096);blocks[blocks_n]=calloc(1,bytes);assert(blocks[blocks_n]);return blocks_n;}
static int sceKernelGetMemBlockBase(SceUID id,void **p){*p=blocks[id];return 0;}
static int sceKernelFreeMemBlock(SceUID id){free(blocks[id]);blocks[id]=NULL;return 0;}
static int sceGxmMapMemory(void *p,unsigned n,unsigned flags){assert(p&&n);(void)flags;return 0;}
static int sceGxmUnmapMemory(void *p){assert(p);return 0;}
static unsigned flushed_bytes,flush_calls;
static void xv_gpu_flush_pump(const void *p,unsigned bytes){assert(p&&bytes);flushed_bytes+=bytes;flush_calls++;}
static int xv_render_target_create(SceGxmRenderTargetParams *p,unsigned limit,SceGxmRenderTarget **r,unsigned *bytes,const char *tag)
{(void)p;(void)limit;(void)tag;*r=malloc(sizeof **r);assert(*r);(*r)->id=++targets_n;*bytes=4096;return 0;}
static int sceGxmDestroyRenderTarget(SceGxmRenderTarget *r){free(r);return 0;}
static int sceGxmColorSurfaceInit(SceGxmColorSurface *s,int f,int t,int scale,int reg,unsigned w,unsigned h,unsigned stride,void *p)
{(void)f;(void)t;(void)scale;(void)reg;(void)w;(void)h;(void)stride;s->data=p;return 0;}
static int sceGxmTextureInitLinear(SceGxmTexture *t,void *p,int f,unsigned w,unsigned h,unsigned n)
{(void)f;(void)w;(void)h;assert(n==1);t->data=p;return 0;}
static int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *d,int f,int t,unsigned stride,void *p,void *s)
{(void)f;(void)t;(void)stride;(void)p;(void)s;memset(d,0,sizeof *d);return 0;}
static void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *d,int v){d->load=v;}
static void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *d,int v){d->store=v;}
static void sceGxmTextureSetMinFilter(SceGxmTexture *t,int v){(void)t;(void)v;}
static void sceGxmTextureSetMagFilter(SceGxmTexture *t,int v){(void)t;(void)v;}
static void sceGxmTextureSetUAddrMode(SceGxmTexture *t,int v){(void)t;(void)v;}
static void sceGxmTextureSetVAddrMode(SceGxmTexture *t,int v){(void)t;(void)v;}
static unsigned guest[4096];
static void *xv_guest_ptr(uint32_t p){assert(p<sizeof guest);return (char*)guest+p;}
static unsigned clock_us,notifications,waits;
uint64_t xk_os_monotonic_us(void){return ++clock_us;}
void xk_os_scheduler_notify(void){notifications++;}
int xk_wait_u32(const uint32_t *p,uint32_t serial,uint64_t timeout){(void)p;(void)serial;(void)timeout;waits++;return 0;}
static int recording_dropped(void){return 0;}
static int trace_frame(void){return 0;}

/* Hardware model: deferred commands read owned payloads at completion, after
 * the producer has mutated every original guest input and appended a suffix. */
enum {E_BEGIN=1,E_END,E_VIEW,E_DRAW,E_UI,E_OVERLAY};
typedef struct {unsigned kind,a,b,c,d;uint64_t hash;const cmd_t *draw;const float *constants;SceGxmNotification fence;} event_t;
static event_t events[8192];static unsigned nevents,executed,open_scene,begin_count,end_count,finishes,current_target;
static uint64_t hash_bytes(uint64_t h,const void *p,unsigned bytes)
{const unsigned char *s=p;for(unsigned i=0;i<bytes;i++){h^=s[i];h*=1099511628211ull;}return h;}
static event_t *event(unsigned kind){assert(nevents<8192);event_t *e=&events[nevents++];memset(e,0,sizeof *e);e->kind=kind;return e;}
static int sceGxmBeginScene(SceGxmContext *ctx,int flags,const SceGxmRenderTarget *rt,void *a,void *b,SceGxmSyncObject *sync,const SceGxmColorSurface *color,const SceGxmDepthStencilSurface *depth)
{(void)ctx;(void)flags;(void)a;(void)b;(void)sync;(void)color;assert(!open_scene);begin_count++;if(begin_failure==begin_count)return -1;open_scene=1;current_target=rt->id;event_t *e=event(E_BEGIN);e->a=current_target;e->b=depth->load;e->c=depth->store;return 0;}
static int sceGxmEndScene(SceGxmContext *ctx,void *a,const SceGxmNotification *f)
{(void)ctx;(void)a;assert(open_scene);open_scene=0;end_count++;event_t *e=event(E_END);e->a=current_target;if(end_failure==end_count)return -1;if(f)e->fence=*f;return 0;}
static void sceGxmSetViewport(SceGxmContext *ctx,float x,float xs,float y,float ys,float z,float zs)
{(void)ctx;(void)z;(void)zs;assert(open_scene&&x==xs&&y==-ys);event_t *e=event(E_VIEW);e->a=(unsigned)(x*2);e->b=(unsigned)(y*2);}
static void gpu_execute(void)
{
    for(;executed<nevents;executed++) {event_t *e=&events[executed];
        if(e->kind==E_DRAW) {
            const cmd_t *c=e->draw;uint64_t h=1469598103934665603ull;
            h=hash_bytes(h,&c->shader,sizeof c->shader);
            if(!c->kind) {h=hash_bytes(h,c->vertices,c->nv*4);h=hash_bytes(h,c->indices,c->ni*4);
                h=hash_bytes(h,e->constants,c->const_n*16);h=hash_bytes(h,c->texture,16);}
            else {h=hash_bytes(h,&c->clear_flags,1);h=hash_bytes(h,&c->clear_color,4);h=hash_bytes(h,&c->clear_z,4);h=hash_bytes(h,&c->clear_stencil,1);}
            e->hash=h;
            if(!c->kind&&c->visibility)for(unsigned core=0;core<4;core++)
                g_visibility_memory[(g_build_frame%3)*XV_VISIBILITY_WORDS+core*512+c->visibility-1]+=c->index_count+core;
        }
        if(e->fence.address)*e->fence.address=e->fence.value;
    }
}
static void sceGxmFinish(SceGxmContext *ctx){(void)ctx;assert(!open_scene);gpu_execute();finishes++;}
static unsigned visibility_init,visibility_front,visibility_back;
static int sceGxmSetVisibilityBuffer(SceGxmContext *ctx,void *p,unsigned stride)
{(void)ctx;assert(p&&stride==512);visibility_init++;return 0;}
static void sceGxmSetFrontVisibilityTestIndex(SceGxmContext *c,unsigned v){(void)c;(void)v;}
static void sceGxmSetBackVisibilityTestIndex(SceGxmContext *c,unsigned v){(void)c;(void)v;}
static void sceGxmSetFrontVisibilityTestEnable(SceGxmContext *c,unsigned v){(void)c;visibility_front=v;}
static void sceGxmSetBackVisibilityTestEnable(SceGxmContext *c,unsigned v){(void)c;visibility_back=v;}
static void sceGxmSetFrontVisibilityTestOp(SceGxmContext *c,unsigned v){(void)c;(void)v;}
static void sceGxmSetBackVisibilityTestOp(SceGxmContext *c,unsigned v){(void)c;(void)v;}

typedef struct {
    unsigned started,ended,command,ui,clear,current,stored,cut,commands,uis,consts,visibility;
    uint32_t frame,upload_ticket;SceGxmDepthStencilSurface bd;
    volatile unsigned prefix_word,final_word;unsigned present,owned,published,final_submitted,failed;
    SceGxmNotification prefix_fence,final_fence;
} prefix_cursor;
static prefix_cursor cursor;
static xv_slot_owner mesh_owner;
static unsigned uploaded_ticket,next_upload_ticket,requested_full,completed_full,prefix_drains;
static int consume_prefix(void);
void xv_render_target_drain(void)
{
    /* No unfinished packet is counted in requested_full. All submitted work
     * is a closed existing scene. Production would marshal this to the pump. */
    assert(!open_scene);assert(requested_full==completed_full);
    if(cursor.owned&&!cursor.present) {
        if(!cursor.ended) {
            /* Model the pump's independent worker-ticket wait and consumption
             * of the released prefix, not a request for the unrecorded tail. */
            uploaded_ticket=cursor.upload_ticket;assert(consume_prefix()==1);
        }
        assert(cursor.ended);prefix_drains++;
    }
    sceGxmFinish(NULL);
    if(cursor.owned&&!cursor.present)assert(cursor.prefix_word==cursor.frame);
    if(cursor.owned)assert(xv_slot_busy(&mesh_owner,completed_full));
    /* No slot/list/constants/visibility ownership is released here. */
}
#include "rt.inc"
#include "recorder.inc"
#include "visibility.inc"
#include "constants.inc"
#include "../../runtime/xv_query_boundary.h"

enum {DS_OFF,DS_READY,DS_FIRST,DS_FINAL,DS_BOUNDS,DS_UI,DS_CLEAR,DS_WRITE,DS_STENCIL,DS_SHADER,DS_EMPTY,DS_READY_FINAL};
static int ds_stencil_readonly(const xv_stencil *s){return !s->enabled||!s->write_mask||(!s->fail&&!s->depth_fail&&!s->pass);}
static int ds_draw_shader(const cmd_t *c){return c->shader!=999;}
int xv_ui_gxm_depth_tail_readonly(unsigned frame){(void)frame;return 1;}
#include "depth.inc"
#define XV_DS_SETUP(l,t) int ds_valid=ds_list_valid(l),ds_stored=0,ds_tail=!!(t)
#define XV_DS_STORED(t) do {if(!(t))ds_stored=1;} while(0)
#define XV_DS_SURFACE(l,i,u,t,d,w,h) do {if(!(t)){unsigned why=!ds_valid?DS_BOUNDS:ds_scene(l,i,u,ds_stored,ds_tail);sceGxmDepthStencilSurfaceSetForceStoreMode(d,why==DS_READY||why==DS_READY_FINAL?0:SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);}} while(0)
static const float *gpu_constants;
static void render_range(SceGxmContext *ctx,cmdlist_t *l,unsigned first,unsigned end,unsigned *clear,uint32_t frame,unsigned limit)
{
    (void)ctx;(void)frame;assert(open_scene);
    for(unsigned i=first;i<end;i++) {const cmd_t *c=&l->cmds[i];
        if(!c->kind)assert(bind_vertex_constants(ctx,l,c,&vertex_shader,frame,g_frame_constants[frame%3].copied));
        event_t *e=event(E_DRAW);e->a=i;e->b=c->kind;e->c=c->pass;e->d=c->visibility;e->draw=c;e->constants=bound_constants;
        if(c->kind) {assert(*clear<limit);(*clear)++;}
        visibility_draw_state(ctx,l,c);
    }
}
void xv_ui_gxm_replay_batch(SceGxmContext *ctx,unsigned frame,unsigned batch,const void *target)
{(void)ctx;(void)target;assert(open_scene);event_t *e=event(E_UI);e->a=frame;e->b=batch;}
void xv_ui_gxm_replay_overlay(SceGxmContext *ctx,unsigned frame)
{(void)ctx;assert(open_scene);event_t *e=event(E_OVERLAY);e->a=frame;}
#include "replay.inc"
#include "prefix_replay.inc"

/* Own capture helper models the production cmd_t loan boundary. Actual new_cmd,
 * clear merging and query allocation/issue are used; guest draw translation,
 * texture decoding and asynchronous uploader implementation are not claimed. */
static unsigned vertex_pool[8192],index_pool[8192],texture_pool[8192],owned_used;
static unsigned guest_vertex[4],guest_index[3],guest_texture[4];static float guest_constant[4];
static void record_draw(unsigned target,unsigned marker)
{
    cmdlist_t *l=cur_list();l->cur_pass=target;cmd_t *c=new_cmd();assert(c);
    assert(owned_used+16<8192&&l->nconsts+4<XV_CONST_POOL);
    for(unsigned i=0;i<4;i++){guest_vertex[i]=marker+i;guest_texture[i]=marker*7+i;guest_constant[i]=(float)(marker+i);}
    for(unsigned i=0;i<3;i++)guest_index[i]=marker+i;
    c->vertices=vertex_pool+owned_used;c->indices=index_pool+owned_used;c->texture=texture_pool+owned_used;
    memcpy((void*)c->vertices,guest_vertex,sizeof guest_vertex);memcpy((void*)c->indices,guest_index,sizeof guest_index);memcpy((void*)c->texture,guest_texture,sizeof guest_texture);
    c->nv=4;c->ni=3;c->index_count=3;c->shader=marker;c->const_off=l->nconsts/4;c->const_n=1;
    memcpy(l->consts+l->nconsts,guest_constant,sizeof guest_constant);l->nconsts+=4;owned_used+=16;
    next_upload_ticket++;
}
static void mutate_sources(void)
{memset(guest_vertex,0x91,sizeof guest_vertex);memset(guest_index,0x83,sizeof guest_index);memset(guest_constant,0xf7,sizeof guest_constant);memset(guest_texture,0x42,sizeof guest_texture);}
static int seal_prefix(void)
{
    cmdlist_t *l=cur_list();if(cursor.owned||l->ncmds<2||l->ncmds>XV_MAX_CMDS||l->nui||l->active_visibility||!l->nvisibility)return 0;
    unsigned cut=1;while(cut<l->ncmds&&l->cmds[cut].pass==l->cmds[0].pass)cut++;
    if(cut==l->ncmds)return 0;
    /* The proof command must already have succeeded. Counts may never expose
     * a provisional new_cmd. Its target byte is immutable even if a later
     * clear merges into its unsubmitted payload. */
    xv_query_boundary_plan plan={0};if(qb_plan(l,&plan)!=QB_READY||plan.command!=cut||plan.ui)return 0;
    cursor=(prefix_cursor){.cut=cut,.commands=cut+1,.consts=l->nconsts,.visibility=l->nvisibility,.current=0xff,
        .frame=g_build_frame,.upload_ticket=next_upload_ticket,.owned=1,.prefix_word=~g_build_frame,.final_word=~g_build_frame};
    cursor.prefix_fence=(SceGxmNotification){&cursor.prefix_word,g_build_frame};
    cursor.final_fence=(SceGxmNotification){&cursor.final_word,g_build_frame};
    mesh_owner=(xv_slot_owner){requested_full+1,1};assert(xv_slot_busy(&mesh_owner,completed_full));
    xv_d3d_query_boundary_prepare(g_build_frame,1);xv_d3d_query_boundary_arm(g_build_frame,&cursor.prefix_fence);
    return 1;
}
static SceGxmRenderTarget back={0};static SceGxmDepthStencilSurface back_depth;
static int consume_prefix(void)
{
    if(!cursor.owned||uploaded_ticket!=cursor.upload_ticket)return 0;
    visibility_physical_prepare(NULL,g_build_frame,960,544,cursor.visibility);
    gpu_constants=(const float*)frame_constants_prepare(cur_list(),g_build_frame,cursor.consts);
    int r=prefix_replay_step(NULL,g_build_frame,&back,NULL,NULL,&back_depth,960,544,1,&cursor,1);
#ifdef TEST_BAD_EARLY_PUBLISH
    if(r==1){gpu_execute();xv_d3d_visibility_complete(g_build_frame);}
#endif
#ifdef TEST_BAD_RETIRE_PREFIX
    if(r==1)completed_full=mesh_owner.ticket;
#endif
    if(r<0)cursor.failed=1;return r;
}
static void check_before_present(uint32_t old)
{
    uint32_t p=999;assert(xd3d_r_visibility_result(7,&p)==XV_VISIBILITY_INCOMPLETE);
    assert(xd3d_r_visibility_result_generation(7,old,&p)==0&&p==1234);
    xv_visibility_result *r=xv_visibility_find(g_visibility_results,7);assert(r->submitted==old&&r->completed==old);
    unsigned before=waits;assert(!xd3d_r_visibility_wait(7,100,&p,&p));assert(waits==before);
    assert(xd3d_r_visibility_wait_generation(7,old,100));assert(waits==before+1);
    assert(!notifications&&cursor.owned&&!cursor.present&&requested_full==completed_full);
    assert(xv_slot_busy(&mesh_owner,completed_full));
    for(unsigned serial=1;serial<=4;serial++) {
        assert(!xd3d_r_visibility_result_generation(7,serial,&p)&&p==1230+serial);
    }
}
static unsigned finish_prefix(void)
{
    cmdlist_t *l=cur_list();assert(cursor.owned&&cursor.ended&&!cursor.present);
    cursor.present=1;requested_full++;
    /* Existing Present drain has read retained old generations before here. */
    visibility_public_submit(g_build_frame);
#ifdef TEST_BAD_ZERO_AT_PRESENT
    visibility_physical_prepare(NULL,g_build_frame,960,544,l->nvisibility);
#endif
    xv_query_boundary_plan complete={0};unsigned why=qb_plan(l,&complete);
    int same=why==QB_READY&&complete.command==cursor.cut&&complete.ui==0&&l->nui==0;
    if(!same) { /* Never reset or repurpose the in-flight provisional word. */
        g_query_boundary_plans[g_build_frame%3].armed=0;
    }
    /* Prefix completion can now satisfy the original full-packet publication
     * contract, after the original Present consumer drain. Exactly once. */
    if(same&&cursor.prefix_word==g_build_frame) {
        xv_d3d_visibility_complete(g_build_frame);cursor.published=1;
        assert(cursor.owned&&!cursor.final_submitted);
        assert(xv_slot_busy(&mesh_owner,completed_full));
    }
    cursor.commands=l->ncmds;cursor.uis=l->nui;
    gpu_constants=(const float*)frame_constants_prepare(l,g_build_frame,l->nconsts);
    int r=prefix_replay_step(NULL,g_build_frame,&back,NULL,NULL,&back_depth,960,544,1,&cursor,0);
    if(r>=0) {assert(open_scene);sceGxmEndScene(NULL,NULL,&cursor.final_fence);cursor.final_submitted=1;}
    else {cursor.failed=1;sceGxmFinish(NULL);} /* Pump-owned existing error drain. */
    gpu_execute();assert(cursor.failed||cursor.final_word==g_build_frame);
    if(!cursor.published){xv_d3d_visibility_complete(g_build_frame);cursor.published=1;}
    completed_full=requested_full;assert(!xv_slot_busy(&mesh_owner,completed_full));cursor.owned=0;return same;
}

typedef struct {unsigned count;event_t e[8192];xv_visibility_result results[512];unsigned init,notify,constant_bytes;} snapshot;
static snapshot baseline;
static unsigned comparisons,cases,fallbacks,physical_shape,proof_clear;
static void reset_case(unsigned frame)
{
    assert(!open_scene);gpu_execute();cursor.owned=0;rt_shutdown();
    memset(lists,0,sizeof lists);memset(g_visibility_results,0,sizeof g_visibility_results);
    memset(&cursor,0,sizeof cursor);memset(g_query_boundary_plans,0,sizeof g_query_boundary_plans);
    memset(events,0,sizeof events);nevents=executed=begin_count=end_count=finishes=0;
    begin_failure=end_failure=alloc_failure=0;requested_full=completed_full=frame;memset(&mesh_owner,0,sizeof mesh_owner);
    g_build_frame=frame;clock_us=notifications=waits=visibility_init=0;owned_used=0;
    uploaded_ticket=next_upload_ticket=0;g_frame_constants[frame%3].ready=0;
    flushed_bytes=flush_calls=0;g_constant_upload_bytes=0;g_constant_upload_frames=0;
    g_constant_upload_high=g_constant_direct_draws=g_constant_ring_draws=g_constant_bad_draws=0;targets_n=0;
    for(unsigned i=0;i<2;i++){rt_alias_t *r=rt_register(0x1000+0x100*i,960,544,6);assert(r);r->owner=r->data;}
    /* Public history from four earlier generations; latest+four-old behavior
     * stays original. The oldest retained sample is read before Present. */
    uint16_t slot;uint32_t serial;
    for(unsigned i=0;i<4;i++){assert(!xv_visibility_issue(g_visibility_results,7,&slot,&serial));xv_visibility_submit(g_visibility_results,slot,serial,1);xv_visibility_publish(g_visibility_results,slot,serial,1231+i);}
    clock_us=notifications=0;
}
static void build_first(void)
{
    cmdlist_t *l=cur_list();l->cur_pass=0;xv_d3d_Clear(1,0x12131415,1,0);xv_d3d_Clear(2,0,0.5f,0);assert(l->ncmds==1);
    if(physical_shape)for(unsigned i=0;i<41;i++)record_draw(0,40+i);
    xd3d_r_visibility_begin(960,544);record_draw(0,11);assert(!xd3d_r_visibility_end(7));
    xd3d_r_visibility_begin(960,544);record_draw(0,13);assert(!xd3d_r_visibility_end(8));
    xd3d_r_visibility_begin(960,544);assert(!xd3d_r_visibility_end(9)); /* Exact zero writer. */
    if(proof_clear){l->cur_pass=1;xv_d3d_Clear(1,0x99112233,1,0);}
    else record_draw(1,17); /* The actual first target transition is now proved. */
}
static void build_suffix(unsigned variant)
{
    mutate_sources();cmdlist_t *l=cur_list();
    if(proof_clear){unsigned count=l->ncmds;xv_d3d_Clear(2,0,0.3f,0);assert(l->ncmds==count);}
    record_draw(1,19);
    if(physical_shape) {
        assert(!variant);
        const unsigned targets[]={1,2,0,1,2,0,1,0};
        for(unsigned span=0;span<8;span++)for(unsigned j=span?0:2;j<(span==7?7u:10u);j++)record_draw(targets[span],40+span*10+j);
        assert(l->ncmds==44+77);mutate_sources();return;
    }
    if(variant==1){xd3d_r_visibility_begin(960,544);record_draw(1,23);assert(!xd3d_r_visibility_end(7));}
    if(variant==2){l->ui[l->nui].before=l->ncmds;l->ui[l->nui].target=1;l->ui[l->nui].batch=37;l->nui++;}
    l->cur_pass=1;xv_d3d_Clear(1,0x87888990,1,0);xv_d3d_Clear(2,0,0.2f,0); /* Unsubmitted clear merge. */
    record_draw(2,29);record_draw(0,31);mutate_sources();
}
static void save_snapshot(snapshot *s)
{s->count=nevents;memcpy(s->e,events,nevents*sizeof *events);memcpy(s->results,g_visibility_results,sizeof s->results);s->init=visibility_init;s->notify=notifications;s->constant_bytes=g_constant_upload_bytes;}
static void compare_snapshot(const snapshot *s,int same_fences)
{
    assert(nevents==s->count);
    for(unsigned i=0;i<nevents;i++){event_t a=events[i],b=s->e[i];assert(a.kind==b.kind&&a.a==b.a&&a.b==b.b&&a.c==b.c&&a.d==b.d&&a.hash==b.hash);
        if(same_fences)assert(!!a.fence.address==!!b.fence.address&&(!a.fence.address||a.fence.value==b.fence.value));comparisons++;}
    /* Submission/completion timestamps describe deliberately different clocks.
     * All IDs, generations, pixels and four complete history records match. */
    for(unsigned i=0;i<512;i++){xv_visibility_result a=g_visibility_results[i],b=s->results[i];a.submitted_us=b.submitted_us=0;a.completed_us=b.completed_us=0;assert(!memcmp(&a,&b,sizeof a));comparisons++;}
    assert(visibility_init==1&&s->init==1&&notifications==s->notify&&notifications==1);
    assert(g_constant_upload_bytes==s->constant_bytes);
}
static void normal_cases(void)
{
    for(unsigned shape=0;shape<3;shape++)for(unsigned frame_index=0;frame_index<3;frame_index++)for(unsigned variant=0;variant<(shape?1u:3u);variant++)for(unsigned pending=0;pending<2;pending++) {
        physical_shape=shape==1;proof_clear=shape==2;
        unsigned frame=frame_index==0?0:frame_index==1?UINT32_MAX:3;
        reset_case(frame);build_first();build_suffix(variant);
        cursor.prefix_word=~frame;cursor.final_word=~frame;cursor.prefix_fence=(SceGxmNotification){&cursor.prefix_word,frame};cursor.final_fence=(SceGxmNotification){&cursor.final_word,frame};
        visibility_physical_prepare(NULL,frame,960,544,cur_list()->nvisibility);visibility_public_submit(frame);
        gpu_constants=(const float*)frame_constants_prepare(cur_list(),frame,cur_list()->nconsts);
        if(xv_d3d_query_boundary_prepare(frame,1))xv_d3d_query_boundary_arm(frame,&cursor.prefix_fence);
        assert(!xv_d3d_render_targets(NULL,frame,&back,NULL,NULL,&back_depth,960,544,1));sceGxmEndScene(NULL,NULL,&cursor.final_fence);gpu_execute();xv_d3d_visibility_complete(frame);save_snapshot(&baseline);

        reset_case(frame);build_first();assert(seal_prefix());
        assert(!consume_prefix());assert(!nevents&&!visibility_init); /* Pending upload may not expose any GPU work. */
        uploaded_ticket=cursor.upload_ticket;assert(consume_prefix()==1);assert(!open_scene&&cursor.ended&&visibility_init==1);
        unsigned first_events=nevents;build_suffix(variant);assert(nevents==first_events);
        /* Complete just the submitted prefix while owner still records. No
         * history or submitted serial is changed, including all four old slots. */
        if(!pending)gpu_execute();check_before_present(4);
        uint32_t pixels;assert(!xd3d_r_visibility_result_generation(7,1,&pixels)&&pixels==1231);
        unsigned same=finish_prefix();assert(same==(variant==0));fallbacks+=!same;
        assert(visibility_init==1&&!cursor.owned&&cursor.published&&cursor.final_submitted);
        if(physical_shape)assert(cursor.cut==44&&end_count==9);
        assert(finishes==2); /* Initial two RT registrations only. */
        compare_snapshot(&baseline,same);cases++;
    }
    physical_shape=proof_clear=0;
}
static void admission_cases(void)
{
    for(unsigned bad=0;bad<8;bad++) {
        reset_case(0);build_first();cmdlist_t *l=cur_list();
        if(bad==0)l->active_visibility=1;
        if(bad==1)l->nui=1;
        if(bad==2)l->ncmds--;
        if(bad==3)l->cmds[1].visibility=513;
        if(bad==4)l->visibility[0].serial=0;
        if(bad==5)l->cmds[3].pass=9;
        if(bad==6)l->nvisibility=0;
        if(bad==7){cmd_t *rejected=new_cmd();assert(rejected);l->ncmds--;l->cmds[3].pass=0;}
        cmdlist_t saved=*l;assert(!seal_prefix());assert(!memcmp(&saved,l,sizeof saved));assert(!cursor.owned&&!nevents);cases++;
    }
}
static void drain_cases(void)
{
    for(unsigned consumed=0;consumed<2;consumed++) {
        reset_case(0);build_first();assert(seal_prefix());
        if(consumed){uploaded_ticket=cursor.upload_ticket;assert(consume_prefix()==1);}
        unsigned first=nevents;rt_alias_t *r=rt_register(0x3000,128,128,6);assert(r);
        assert((!consumed||nevents==first)&&cursor.owned&&!cursor.present&&cursor.prefix_word==0);
        assert(prefix_drains&&requested_full==completed_full);check_before_present(4);
        assert(!rt_register(0x1000,128,128,6)); /* Live geometry cannot be silently replaced. */
        alloc_failure=1;assert(!rt_register(0x4000,64,64,6));alloc_failure=0;
        void *old_memory=g_rt[0].mem;SceGxmRenderTarget *old_target=g_rt[0].rt;
        xv_d3d_ReleaseRenderTarget(0x1000);
        rt_alias_t *fresh=rt_register(0x5000,960,544,6);
        assert(fresh&&fresh!=&g_rt[0]&&g_rt[0].mem==old_memory&&g_rt[0].rt==old_target);
        build_suffix(0);assert(finish_prefix());
        /* Only a retired sufficiently old generation may reclaim the entry. */
        g_build_frame+=XV_NUM_LISTS;r=rt_register(0x6000,960,544,6);
        assert(r==&g_rt[0]&&r->mem==old_memory&&r->rt==old_target);cases++;
    }
}
static void error_cases(void)
{
    for(unsigned which=0;which<3;which++) {
        reset_case(0);build_first();assert(seal_prefix());uploaded_ticket=cursor.upload_ticket;
        if(which==0)begin_failure=1;if(which==1)end_failure=1;
        int r=consume_prefix();
        if(which<2){assert(r<0&&cursor.failed&&!cursor.present);assert(!open_scene);assert(cursor.owned);sceGxmFinish(NULL);cursor.owned=0;}
        else {assert(r==1);build_suffix(0);SceGxmRenderTarget *saved=g_rt[1].rt;g_rt[1].rt=NULL;assert(!finish_prefix());g_rt[1].rt=saved;assert(cursor.failed&&!cursor.owned);}
        cases++;
    }
}
int main(void)
{
    normal_cases();admission_cases();drain_cases();error_cases();
    assert(!open_scene);gpu_execute();rt_shutdown();
    for(unsigned i=0;i<3;i++)if(g_frame_constants[i].memory)sceKernelFreeMemBlock(g_frame_constants[i].uid);
    if(g_visibility_memory)sceKernelFreeMemBlock(g_visibility_uid);
    for(unsigned i=1;i<=blocks_n;i++)assert(!blocks[i]);
    printf("PASS cases=%u comparisons=%u final-fallbacks=%u; query-bearing first scene; exact rendering/data/history; one physical query init; no ordinary extra scene/Finish; modeled immutable ownership only\n",cases,comparisons,fallbacks);
    return 0;
}
