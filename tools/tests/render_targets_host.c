/* Deliberately asynchronous mock: GPU texture reads and color writes execute
 * in scene order only at Finish. CPU submission can queue multiple scenes.
 * Tests run production pool/mapping/scheduling functions extracted by
 * test_render_targets.py; this models ordering, not GXM pixel formats. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
#include "../../runtime/xv_render_profile.h"
void xv_logf(const char *fmt, ...) { (void)fmt; }
#define XV_CLEAR_SLOTS 64
#define XV_RT_SLOTS 8
#define XV_RT_BUDGET (16u * 1024 * 1024)
#define XV_NUM_LISTS 2
#define XV_LOG(...) ((void)0)
#define XV_ONCE(flag, ...) ((void)0)
typedef int SceUID;
typedef int SceGxmColorFormat;
typedef int SceGxmTextureFormat;
typedef int SceGxmContext;
typedef int SceGxmSyncObject;
typedef struct { unsigned width, height, scenesPerFrame, multisampleMode; int driverMemBlock; } SceGxmRenderTargetParams;
typedef struct { unsigned id, scenes; } SceGxmRenderTarget;
typedef struct { void *data; unsigned stride; } SceGxmColorSurface;
typedef struct { int load, store; } SceGxmDepthStencilSurface;
typedef struct { void *data; } SceGxmTexture;
typedef struct { unsigned pass, ntex, value, expect; SceGxmTexture tex[4]; } cmd_t;
typedef struct {
    cmd_t cmds[32]; unsigned ncmds;
    struct { unsigned before, frame, batch, target; } ui[1024];
    unsigned nui, ui_frame, cur_pass;
} cmdlist_t;
static cmdlist_t lists[2], *g_lists[2] = { &lists[0], &lists[1] };
static uint32_t g_build_frame;
static uint32_t guest[1024];
static void *xv_guest_ptr(uint32_t a) { assert(a < sizeof guest); return (char *)guest + a; }
static cmdlist_t *cur_list(void) { return g_lists[g_build_frame % 2]; }
static void *blocks[128];
static unsigned next_uid, objects, created, finishes, begun, ended, ui_draws;
static int open_scene, pending, fail_create, fail_begin, fail_end, fail_map;
static unsigned scene_extra = 4096, fail_create_above, fail_query_above;
static void *active_data;
static struct { const cmd_t *command; void *output; } gpu_draws[128];
static unsigned gpu_count, gpu_executed, max_pending;
static SceGxmDepthStencilSurface submitted_depth[128];
static char order[128]; static unsigned order_n;
static SceUID sceKernelAllocMemBlock(const char *n, int t, unsigned size, void *p)
{ assert(++next_uid < 128); blocks[next_uid] = malloc(size); assert(blocks[next_uid]); return next_uid; }
static int sceKernelGetMemBlockBase(SceUID id, void **p) { *p = blocks[id]; return 0; }
static int sceKernelFreeMemBlock(SceUID id) { free(blocks[id]); blocks[id] = NULL; return 0; }
static int sceGxmMapMemory(void *p, unsigned n, unsigned a) { return fail_map ? -1 : 0; }
static int sceGxmUnmapMemory(void *p) { return 0; }
static int sceGxmGetRenderTargetMemSize(const SceGxmRenderTargetParams *p, unsigned *n)
{ if (fail_query_above && p->scenesPerFrame > fail_query_above) return -2;
  *n = 4096 + (p->scenesPerFrame - 1) * scene_extra; return 0; }
static int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *p, SceGxmRenderTarget **r)
{ if (fail_create || (fail_create_above && p->scenesPerFrame > fail_create_above)) return -1;
  *r = malloc(sizeof **r); (*r)->id = ++created; (*r)->scenes = p->scenesPerFrame; objects++; return 0; }
static int sceGxmDestroyRenderTarget(SceGxmRenderTarget *r) { free(r); objects--; return 0; }
static int sceGxmColorSurfaceInit(SceGxmColorSurface *c, int f, int t, int s, int o, unsigned w, unsigned h, unsigned stride, void *p)
{ assert(stride >= w && stride % 8 == 0); c->data = p; c->stride = stride; return 0; }
static int sceGxmTextureInitLinear(SceGxmTexture *t, void *p, int f, unsigned w, unsigned h, unsigned m)
{ assert(m == 1); t->data = p; return 0; }
static int sceGxmDepthStencilSurfaceInit(SceGxmDepthStencilSurface *d, int f, int t, unsigned stride, void *p, void *s)
{ assert(stride % 32 == 0); memset(d, 0, sizeof *d); return 0; }
static void sceGxmDepthStencilSurfaceSetForceLoadMode(SceGxmDepthStencilSurface *d, int v) { d->load = v; }
static void sceGxmDepthStencilSurfaceSetForceStoreMode(SceGxmDepthStencilSurface *d, int v) { d->store = v; }
static void sceGxmTextureSetMinFilter(SceGxmTexture *t, int f) {}
static void sceGxmTextureSetMagFilter(SceGxmTexture *t, int f) {}
static void sceGxmTextureSetUAddrMode(SceGxmTexture *t, int f) {}
static void sceGxmTextureSetVAddrMode(SceGxmTexture *t, int f) {}
static void *sceGxmTextureGetData(const SceGxmTexture *t) { return t->data; }
static void sceGxmFinish(SceGxmContext *c)
{
    assert(!open_scene);
    for (unsigned i = 0; i < gpu_count; ++i) {
        const cmd_t *draw = gpu_draws[i].command;
        if (draw->ntex && sceGxmTextureGetData(&draw->tex[0]))
            assert(*(unsigned *)sceGxmTextureGetData(&draw->tex[0]) == draw->expect);
        *(unsigned *)gpu_draws[i].output = draw->value;
        gpu_executed++;
    }
    gpu_count = 0; pending = 0; finishes++;
}
void xv_render_target_drain(void) { sceGxmFinish(NULL); }
static int sceGxmBeginScene(SceGxmContext *c, int f, SceGxmRenderTarget *r, void *v, void *vs, SceGxmSyncObject *fs, const SceGxmColorSurface *cs, const SceGxmDepthStencilSurface *ds)
{
    assert(!open_scene);
    if (fail_begin && begun + 1 == (unsigned)fail_begin) return -1;
    open_scene = 1; active_data = cs->data;
    assert(begun < 128); submitted_depth[begun++] = *ds;
    return 0;
}
static int sceGxmEndScene(SceGxmContext *c, void *v, void *f)
{ assert(open_scene); open_scene = 0; pending++; if ((unsigned)pending > max_pending) max_pending = pending; ended++; return fail_end ? -1 : 0; }
static float viewport_width, viewport_height;
static unsigned range_calls, max_range;
static void sceGxmSetViewport(SceGxmContext *c, float x, float xs, float y, float ys, float z, float zs)
{ assert(open_scene && x == xs && y == -ys); viewport_width = 2 * xs; viewport_height = -2 * ys; }
static void render_range(SceGxmContext *ctx, cmdlist_t *l, unsigned first, unsigned end, unsigned *slot, uint32_t frame, unsigned clear_limit)
{
    assert(open_scene);
    range_calls++;
    if(end-first>max_range)max_range=end-first;
    for (unsigned i = first; i < end; ++i) {
        cmd_t *c = &l->cmds[i];
        assert(gpu_count < 128);
        gpu_draws[gpu_count].command = c;
        gpu_draws[gpu_count++].output = active_data;
        order[order_n++] = '0' + i;
    }
}
void xv_ui_gxm_replay_batch(SceGxmContext *c, unsigned frame, unsigned batch, const void *target)
{ assert(open_scene && frame == 1 && batch == 7); ui_draws++; order[order_n++] = 'U'; }
void xv_ui_gxm_replay_overlay(SceGxmContext *c, unsigned f) { assert(open_scene); }
static void visibility_draw_state(SceGxmContext *ctx, cmdlist_t *l, const cmd_t *c)
{ (void)ctx; (void)l; assert(!c); }
#include "../../runtime/xv_render_target.h"
#include "render_targets_under_test.inc"
static void capacity_tests(void)
{
    unsetenv("XV_RT_SCENES"); assert(xv_render_target_scenes() == 4);
    const char *values[] = {"", "invalid", "4junk", "0", "-4", "1", "3", "99", "999999999999999999999999999"};
    unsigned expected[] = {4,4,4,1,1,1,3,8,8};
    for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
        setenv("XV_RT_SCENES", values[i], 1); assert(xv_render_target_scenes() == expected[i]);
    }
    setenv("XV_RT_SCENES", "4", 1);
    SceGxmRenderTargetParams p = { .width=848, .height=480, .multisampleMode=73, .driverMemBlock=-1 };
    SceGxmRenderTargetParams original = p;
    SceGxmRenderTarget *r = NULL; unsigned bytes = 123;
    assert(xv_render_target_create(&p, 16384, &r, &bytes, "test") == 0);
    assert(r->scenes == 4 && bytes == 16384); sceGxmDestroyRenderTarget(r); r = NULL;
    assert(xv_render_target_create(&p, 8192, &r, &bytes, "test") == 0);
    assert(r->scenes == 2 && bytes == 8192); sceGxmDestroyRenderTarget(r); r = NULL;
    assert(xv_render_target_create(&p, 4096, &r, &bytes, "test") == 0);
    assert(r->scenes == 1 && bytes == 4096); sceGxmDestroyRenderTarget(r); r = NULL;
    bytes = 123;
    assert(xv_render_target_create(&p, 4095, &r, &bytes, "test") < 0 && !r && bytes == 123);
    fail_create_above = 2;
    assert(xv_render_target_create(&p, UINT32_MAX, &r, &bytes, "test") == 0 && r->scenes == 2);
    sceGxmDestroyRenderTarget(r); r = NULL; fail_create_above = 0;
    fail_query_above = 1;
    assert(xv_render_target_create(&p, UINT32_MAX, &r, &bytes, "test") == 0 && r->scenes == 1);
    sceGxmDestroyRenderTarget(r); r = NULL; fail_query_above = 0;
    scene_extra = 300 * 1024;
    assert(xv_render_target_create(&p, UINT32_MAX, &r, &bytes, "test") == 0);
    assert(r->scenes == 2 && bytes == 4096 + scene_extra);
    sceGxmDestroyRenderTarget(r); r = NULL; scene_extra = 4096;
    fail_create = 1; bytes = 123;
    assert(xv_render_target_create(&p, UINT32_MAX, &r, &bytes, "test") < 0 && !r && bytes == 123);
    fail_create = 0;
    assert(!memcmp(&p, &original, sizeof p) && !objects);
}
static void header(unsigned addr, unsigned data, unsigned w, unsigned h, unsigned fmt)
{
    uint32_t *p = xv_guest_ptr(addr);
    p[0] = 0x00050000; p[1] = data; p[3] = fmt << 8;
    p[4] = (w - 1) | ((h - 1) << 12); p[5] = 0;
}
int main(int argc, char **argv)
{
    if (argc > 1) {
        header(64, 0x10000, 17, 19, 0x12);
        xv_d3d_SetRenderTarget(64, 0);
        assert(cur_list()->cur_pass == 0xff && !objects);
        assert(!xv_d3d_record_ui(0, 0));
        xv_d3d_SetRenderTarget(0, 0); assert(cur_list()->cur_pass == 0xff);
        xv_d3d_SetRenderTarget(64, 1); assert(cur_list()->cur_pass == 0);
        assert(xv_d3d_record_ui(0, 0));
        assert(!xv_d3d_has_render_targets(0));
        puts("XV_DROP_RT=1 host checks passed.");
        return 0;
    }
    header(64, 0x10000, 17, 19, 0x12);
    xv_d3d_SetRenderTarget(64, 0);
    assert(cur_list()->cur_pass == 1 && objects == 1 && g_rt[0].color.stride == 24);
    assert(g_rt[0].rt->scenes == xv_render_target_scenes());
    assert(g_rt_bytes == g_rt[0].bytes + g_rt[0].driver_bytes);
    const SceGxmTexture *a = xv_d3d_render_target_texture(64);
    assert(a && a->data == g_rt[0].color.data);
    header(128, 0x10000, 17, 19, 0x12);
    assert(xv_d3d_render_target_texture(128) == a); /* distinct surface/texture header */
    xv_d3d_SetRenderTarget(0, 0); assert(cur_list()->cur_pass == 1);
    xv_d3d_SetRenderTarget(64, 0); assert(objects == 1 && created == 1);
    header(192, 0x20000, 17, 19, 0x12);
    xv_d3d_SetRenderTarget(192, 0);
    assert(cur_list()->cur_pass == 2 && objects == 2 && g_rt[1].mem != a->data);

    /* B -> A -> B(samples A) -> A(overwrite) -> B(samples new A),
     * with a UI draw between the first producer and consumer. */
    cmdlist_t *l = cur_list(); l->ncmds = 5;
    unsigned targets[] = {0,1,0,1,0};
    for (unsigned i = 0; i < 5; ++i) { l->cmds[i].pass = targets[i]; l->cmds[i].value = 10 + i; }
    l->cmds[2].ntex = l->cmds[4].ntex = 1;
    l->cmds[2].tex[0] = l->cmds[4].tex[0] = *a;
    l->cmds[2].expect = 11; l->cmds[4].expect = 13;
    /* The scheduler cannot reject stale bindings without knowing which shader
     * samplers are active. Command 1 writes A with unused A bound at stage 3. */
    l->cmds[1].ntex=4; l->cmds[1].tex[3]=*a;
    l->ui[0].before = 2; l->ui[0].frame = 1; l->ui[0].batch = 7; l->ui[0].target = 0; l->nui = 1;
    unsigned back_pixels = 0; SceGxmColorSurface back_color = { &back_pixels, 960 };
    SceGxmDepthStencilSurface back_depth = {0}; SceGxmRenderTarget back_rt = {0};
    int queued = atoi(getenv("XV_RT_QUEUE"));
    unsigned before_finishes = finishes;
    assert(xv_d3d_has_render_targets(0));
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) == 0);
    assert(viewport_width == 848 && viewport_height == 480);
    assert(begun == 5 && ended == 4 && ui_draws == 1 && !strcmp(order, "01U234"));
    assert(finishes - before_finishes == (queued ? 0u : 4u));
    if (queued) assert(!back_pixels && !*(unsigned *)a->data && !gpu_executed);
    for (unsigned i = 0; i < 5; ++i) {
        assert(submitted_depth[i].store == SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
        assert(submitted_depth[i].load == (i ? SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED : 0));
    }
    sceGxmEndScene(NULL, NULL, NULL); sceGxmFinish(NULL);
    assert(gpu_executed == 5 && !gpu_count && max_pending == (queued ? 5u : 1u));
    assert(back_pixels == 14 && *(unsigned *)a->data == 13);
    assert(back_depth.load == 0 && back_depth.store == 0); /* caller's surface unchanged */

    /* Adjacent mesh commands share a replay range, but a UI batch interrupts
     * that range even when all draws use the same color target. */
    l->ncmds=3;
    for(unsigned i=0;i<3;++i){l->cmds[i].pass=0;l->cmds[i].ntex=0;}
    range_calls=max_range=order_n=0;memset(order,0,sizeof order);
    assert(xv_d3d_render_targets(NULL,0,&back_rt,NULL,&back_color,&back_depth,704,400)==0);
    assert(range_calls==2&&max_range==2&&!strcmp(order,"01U2"));
    assert(viewport_width==704&&viewport_height==400);
    sceGxmEndScene(NULL,NULL,NULL);sceGxmFinish(NULL);

    /* Live identities cannot be evicted; released storage cannot be reused
     * while either recorded list may still reference it. */
    for (unsigned i = 2; i < 8; ++i) assert(rt_register((i+1)*0x10000, 17, 19, 0x12));
    assert(!rt_register(0x90000, 17, 19, 0x12));
    xv_d3d_ReleaseRenderTarget(0x10000);
    assert(!xv_d3d_render_target_texture(64));
    assert(!rt_register(0x90000, 17, 19, 0x12));
    g_build_frame = 2;
    unsigned old_created = created;
    assert(rt_register(0x90000, 17, 19, 0x12) == &g_rt[0]);
    assert(created == old_created); /* same geometry reuses all GXM objects */
    rt_shutdown(); assert(!g_rt_bytes && !objects);

    fail_create = 1; assert(!rt_register(0x10000, 64, 64, 0x12));
    assert(!objects && !g_rt_bytes); fail_create = 0;
    fail_map = 1; assert(!rt_register(0x10000, 64, 64, 0x12));
    assert(!objects && !g_rt_bytes); fail_map = 0;
    assert(!rt_register(0x10000, 64, 64, 0x0c)); /* DXT cannot be a color attachment */
    assert(!rt_register(0x10000, 2048, 64, 0x12));
    assert(rt_register(0x10000, 1024, 1024, 0x12));
    assert(!rt_register(0x20000, 1024, 1024, 0x12)); /* 16 MB including driver memory */
    rt_shutdown();
    memset(lists, 0, sizeof lists);
    assert(!xv_d3d_has_render_targets(0));
    fail_begin = begun + 1;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene); fail_begin = 0;
    /* A later BeginScene failure must drain every previously queued pass. */
    lists[0].ncmds = 1; lists[0].cmds[0].pass = 1;
    assert(rt_register(0x10000, 64, 64, 0x12));
    fail_begin = begun + 2;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending && !gpu_count); fail_begin = 0;
    lists[0].ncmds = 1; lists[0].cmds[0].pass = 1;
    assert(rt_register(0x10000, 64, 64, 0x12));
    fail_end = 1;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending); fail_end = 0;
    rt_shutdown();
    /* The hardware crash log had target 90 with only eight slots. Reject a
     * malformed target before indexing the pool or submitting invalid pointers. */
    memset(lists, 0, sizeof lists);
    lists[0].ncmds = 1; lists[0].cmds[0].pass = 90;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending);
    lists[0].ncmds = 2; lists[0].cmds[0].pass = 0; lists[0].cmds[1].pass = 90;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending);
    lists[0].ncmds = 0; lists[0].nui = 1; lists[0].ui[0].target = 90;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending);
    lists[0].ui[0].target = 1; /* valid index, no live target */
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth, 848, 480) < 0);
    assert(!open_scene && !pending);
    for (unsigned i = 1; i <= next_uid; ++i) assert(!blocks[i]);
    capacity_tests();
    puts("RT host checks passed: mapping, deferred producer/consumer/UI, depth preservation, reuse, bounds, failure cleanup.");
}
