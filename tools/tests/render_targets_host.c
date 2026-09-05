/* Deliberately asynchronous mock: an EndScene's pixels only become visible at
 * Finish. BeginScene refuses an unfinished predecessor. Tests run production
 * pool/mapping/scheduling functions extracted by test_render_targets.py. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constants.h"
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
typedef struct { unsigned id; } SceGxmRenderTarget;
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
static const SceGxmColorSurface *active_color;
static void *pending_data;
static unsigned pending_value;
static char order[128]; static unsigned order_n;
static SceUID sceKernelAllocMemBlock(const char *n, int t, unsigned size, void *p)
{ assert(++next_uid < 128); blocks[next_uid] = malloc(size); assert(blocks[next_uid]); return next_uid; }
static int sceKernelGetMemBlockBase(SceUID id, void **p) { *p = blocks[id]; return 0; }
static int sceKernelFreeMemBlock(SceUID id) { free(blocks[id]); blocks[id] = NULL; return 0; }
static int sceGxmMapMemory(void *p, unsigned n, unsigned a) { return fail_map ? -1 : 0; }
static int sceGxmUnmapMemory(void *p) { return 0; }
static int sceGxmGetRenderTargetMemSize(const SceGxmRenderTargetParams *p, unsigned *n) { *n = 4096; return 0; }
static int sceGxmCreateRenderTarget(const SceGxmRenderTargetParams *p, SceGxmRenderTarget **r)
{ if (fail_create) return -1; *r = malloc(sizeof **r); (*r)->id = ++created; objects++; return 0; }
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
{ assert(!open_scene); if (pending_data) *(unsigned *)pending_data = pending_value; pending_data = NULL; pending = 0; finishes++; }
void xv_render_target_drain(void) { sceGxmFinish(NULL); }
static int sceGxmBeginScene(SceGxmContext *c, int f, SceGxmRenderTarget *r, void *v, void *vs, SceGxmSyncObject *fs, const SceGxmColorSurface *cs, const SceGxmDepthStencilSurface *ds)
{ assert(!open_scene && !pending); if (fail_begin) return -1; open_scene = 1; active_color = cs; begun++; return 0; }
static int sceGxmEndScene(SceGxmContext *c, void *v, void *f)
{ assert(open_scene); open_scene = 0; pending = 1; ended++; return fail_end ? -1 : 0; }
static void sceGxmSetViewport(SceGxmContext *c, float x, float xs, float y, float ys, float z, float zs)
{ assert(open_scene && x == xs && y == -ys); }
static void render_range(SceGxmContext *ctx, cmdlist_t *l, unsigned first, unsigned end, unsigned *slot, uint32_t frame, unsigned clear_limit)
{
    assert(open_scene);
    for (unsigned i = first; i < end; ++i) {
        cmd_t *c = &l->cmds[i];
        if (c->ntex) assert(*(unsigned *)c->tex[0].data == c->expect);
        pending_data = active_color->data; pending_value = c->value;
        order[order_n++] = '0' + i;
    }
}
void xv_ui_gxm_replay_batch(SceGxmContext *c, unsigned frame, unsigned batch, const void *target)
{ assert(open_scene && frame == 1 && batch == 7); ui_draws++; order[order_n++] = 'U'; }
void xv_ui_gxm_replay_overlay(SceGxmContext *c, unsigned f) { assert(open_scene); }
#include "render_targets_under_test.inc"
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
    l->ui[0].before = 2; l->ui[0].frame = 1; l->ui[0].batch = 7; l->ui[0].target = 0; l->nui = 1;
    unsigned back_pixels = 0; SceGxmColorSurface back_color = { &back_pixels, 960 };
    SceGxmDepthStencilSurface back_depth = {0}; SceGxmRenderTarget back_rt = {0};
    assert(xv_d3d_has_render_targets(0));
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth) == 0);
    assert(begun == 5 && ended == 4 && ui_draws == 1 && !strcmp(order, "01U234"));
    sceGxmEndScene(NULL, NULL, NULL); sceGxmFinish(NULL);
    assert(back_pixels == 14 && *(unsigned *)a->data == 13);
    assert(back_depth.load == 0 && back_depth.store == 0); /* caller's surface unchanged */

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
    fail_begin = 1;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth) < 0);
    assert(!open_scene); fail_begin = 0;
    lists[0].ncmds = 1; lists[0].cmds[0].pass = 1;
    assert(rt_register(0x10000, 64, 64, 0x12));
    fail_end = 1;
    assert(xv_d3d_render_targets(NULL, 0, &back_rt, NULL, &back_color, &back_depth) < 0);
    assert(!open_scene && !pending); fail_end = 0;
    rt_shutdown();
    for (unsigned i = 1; i <= next_uid; ++i) assert(!blocks[i]);
    puts("RT host checks passed: mapping, ordered producer/consumer/UI, reuse, bounds, failure cleanup.");
}
