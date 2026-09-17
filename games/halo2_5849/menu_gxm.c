#include "menu_gxm.h"
#include "menu_texture.h"
#include "menu_combiner.h"
#include "quad_gxm.h"
#include <psp2/gxm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void xv_logf(const char *, ...);
extern void h2_menu_dump_target(const uint8_t *target, uint32_t W, uint32_t H, uint64_t drawn, uint32_t color_offset);

#define GCHECK(x) do { int err_ = (x); if (err_ < 0) { xv_logf("[h2/menu-gxm] FAIL %s = %08X\n", #x, err_); return 0; } } while (0)

enum { W = 640, H = 480, MAX_TARGETS = 4, MAX_VS = 64, MAX_FS = 128, MAX_TEX = 96,
       VERTEX_RING = 6u << 20, INDEX_RING = 1u << 20, TEX_POOL = 48u << 20, TEX_CAP = 1024 * 1024 };

/* ---- programs ---------------------------------------------------------------- */
typedef struct { uint64_t hash; const SceGxmProgram *gxp; SceGxmShaderPatcherId id; SceGxmVertexProgram *prog;
                 const SceGxmProgramParameter *c; unsigned c_count; uint8_t vregs[16]; unsigned nattr, stride; int missing; } vs_entry;
typedef struct { uint64_t hash, key; const SceGxmProgram *gxp; SceGxmShaderPatcherId id; SceGxmFragmentProgram *prog;
                 const SceGxmProgramParameter *psc, *fog, *atest, *texscale, *blendconst; int sampler[4]; const vs_entry *vs; int missing; } fs_entry;
static vs_entry g_vs[MAX_VS]; static unsigned g_nvs;
static fs_entry g_fs[MAX_FS]; static unsigned g_nfs;
static uint64_t g_missing_logged[64]; static unsigned g_nmissing;

/* ---- textures ---------------------------------------------------------------- */
typedef struct { uint64_t hash; uint32_t *mem; uint32_t w, h, bytes; SceGxmTexture tex; uint64_t used; int linear; } tex_entry;
static tex_entry g_tex[MAX_TEX]; static uint8_t *g_texpool; static size_t g_texpool_used;

/* ---- render targets: one GXM colour surface per game colour buffer ---------- */
typedef struct { uint32_t color_offset; uint8_t *guest; uint32_t *mem; SceGxmColorSurface surface; SceGxmRenderTarget *rt; uint64_t used; } target;
static target g_targets[MAX_TARGETS]; static unsigned g_ntargets;
static SceGxmDepthStencilSurface g_depth; static void *g_depth_mem; static int g_depth_clear_pending = 1; static float g_depth_clear = 1.0f;
static target *g_open;                 /* target with an open scene */
static unsigned g_open_draws;
static uint8_t *g_vring; static unsigned g_vring_used; static uint16_t *g_iring; static unsigned g_iring_used;
static SceGxmContext *g_ctx; static SceGxmShaderPatcher *g_patcher;
static int g_ready = -1;
static uint64_t g_serial, g_drawn, g_fallbacks;

static const char *const VREG_NAMES[16] = {"position", "blendweight", "normal", "color0", "color1", "fog", "psize", "backcolor0",
                                            "backcolor1", "texcoord0", "texcoord1", "texcoord2", "texcoord3", "attr13", "attr14", "attr15"};

/* Device-compiled shaders are preferred from ux0:data/xita/shaders/<name> (as the CE runtime
 * does); the copy packed into the VPK (app0:<name>) is the fallback. */
static const SceGxmProgram *load_gxp(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "ux0:data/xita/shaders/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(path, sizeof path, "app0:%s", name); f = fopen(path, "rb"); }
    if (!f) return NULL;
    long size = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    void *data = NULL;
    int ok = size > 0 && size <= 1 << 20 && !fseek(f, 0, SEEK_SET);
    if (ok) { data = malloc((size_t)size); ok = data && fread(data, 1, (size_t)size, f) == (size_t)size; }
    fclose(f);
    if (!ok || sceGxmProgramCheck((const SceGxmProgram *)data) < 0) { free(data); xv_logf("[h2/menu-gxm] bad program %s\n", path); return NULL; }
    return (const SceGxmProgram *)data;
}

#define VIS_STRIDE 4096u
static void *g_vis_raw; static uint32_t *g_vis; static uint64_t g_vis_base;
static uint64_t vis_sum(void)
{
    uint64_t sum = 0;
    for (unsigned core = 0; core < 4; ++core) sum += *(volatile uint32_t *)((uint8_t *)g_vis + core * VIS_STRIDE);
    return sum;
}
/* Both are called by the command consumer (CLEAR_REPORT_VALUE / GET_REPORT): the open scene is
 * finished first so the counts cover every draw submitted so far. */
uint64_t h2_menu_gxm_zpass_read(void)
{
    if (g_ready != 1) return 0;
    if (g_open) h2_menu_gxm_flush();
    return vis_sum() - g_vis_base;
}
void h2_menu_gxm_zpass_clear(void)
{
    if (g_ready != 1) return;
    if (g_open) h2_menu_gxm_flush();
    g_vis_base = vis_sum();
}
static int initialize(void)
{
    if (!h2_gxm_ensure()) return 0;
    g_ctx = h2_gxm_context(); g_patcher = h2_gxm_patcher();
    if (!g_ctx || !g_patcher) return 0;
    unsigned off;
    g_vring = h2_gxm_alloc(VERTEX_RING, 0, &off); g_iring = h2_gxm_alloc(INDEX_RING, 0, &off);
    g_texpool = h2_gxm_alloc(TEX_POOL, 0, &off);
    unsigned aligned_w = (W + 31) & ~31u, aligned_h = (H + 31) & ~31u;
    g_depth_mem = h2_gxm_alloc(aligned_w * aligned_h * 4, 0, &off);
    if (!g_vring || !g_iring || !g_texpool || !g_depth_mem) return 0;
    /* Z-pass pixel-count query: one visibility slot per GPU core (INCREMENT op accumulates
     * the fragments that pass the depth/stencil test); 4 KB per core, base 4 KB aligned. */
    g_vis_raw = h2_gxm_alloc(VIS_STRIDE * 4 + 4096, 0, &off);
    if (!g_vis_raw) return 0;
    g_vis = (uint32_t *)(((uintptr_t)g_vis_raw + 4095) & ~(uintptr_t)4095);
    memset(g_vis, 0, VIS_STRIDE * 4);
    if (sceGxmSetVisibilityBuffer(g_ctx, g_vis, VIS_STRIDE) < 0) { xv_logf("[h2/menu-gxm] visibility buffer rejected\n"); return 0; }
    GCHECK(sceGxmDepthStencilSurfaceInit(&g_depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,
                                         aligned_w, g_depth_mem, NULL));
    sceGxmDepthStencilSurfaceSetForceStoreMode(&g_depth, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
    xv_logf("[h2/menu-gxm] ready: rings %u+%u KB, texture pool %u MB\n", VERTEX_RING >> 10, INDEX_RING >> 10, TEX_POOL >> 20);
    return 1;
}

static target *find_target(const h2_kelvin_clear *c, uint32_t color_offset, uint8_t *guest)
{
    for (unsigned i = 0; i < g_ntargets; ++i) if (g_targets[i].color_offset == color_offset) { g_targets[i].guest = guest; return &g_targets[i]; }
    if (g_ntargets == MAX_TARGETS) return NULL;
    (void)c;
    target *t = &g_targets[g_ntargets];
    unsigned off;
    t->mem = h2_gxm_alloc(W * H * 4, 0, &off);
    if (!t->mem) return NULL;
    SceGxmRenderTargetParams rp; memset(&rp, 0, sizeof rp);
    rp.width = W; rp.height = H; rp.scenesPerFrame = 1; rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE; rp.driverMemBlock = -1;
    if (sceGxmCreateRenderTarget(&rp, &t->rt) < 0) return NULL;
    if (sceGxmColorSurfaceInit(&t->surface, SCE_GXM_COLOR_FORMAT_A8R8G8B8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                               SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, W, H, W, t->mem) < 0) return NULL;
    t->color_offset = color_offset; t->guest = guest; ++g_ntargets;
    xv_logf("[h2/menu-gxm] target %u for colour buffer %08X\n", g_ntargets - 1, color_offset);
    return t;
}

/* End the open scene, wait, and download the GXM colour surface into the guest buffer. */
static void flush_scene(void)
{
    if (!g_open) return;
    sceGxmEndScene(g_ctx, NULL, NULL);
    sceGxmFinish(g_ctx);
    memcpy(g_open->guest, g_open->mem, W * H * 4);
    sceGxmDepthStencilSurfaceSetForceLoadMode(&g_depth, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
    g_open = NULL; g_open_draws = 0; g_vring_used = g_iring_used = 0;
}

void h2_menu_gxm_flush(void) { if (g_ready == 1) flush_scene(); }

void h2_menu_gxm_zeta_cleared(uint32_t clear_value)
{
    if (g_ready == 1) flush_scene();
    g_depth_clear_pending = 1;
    g_depth_clear = (float)(clear_value >> 8) / 16777215.0f;
}

static int open_scene(target *t)
{
    if (g_open == t) return 1;
    flush_scene();
    memcpy(t->mem, t->guest, W * H * 4);                  /* the game's clears/CPU writes since the last scene */
    if (g_depth_clear_pending) {
        sceGxmDepthStencilSurfaceSetBackgroundDepth(&g_depth, g_depth_clear);
        sceGxmDepthStencilSurfaceSetForceLoadMode(&g_depth, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_DISABLED);
        g_depth_clear_pending = 0;
    }
    GCHECK(sceGxmBeginScene(g_ctx, 0, t->rt, NULL, NULL, NULL, &t->surface, &g_depth));
    sceGxmSetViewportEnable(g_ctx, SCE_GXM_VIEWPORT_ENABLED);
    sceGxmSetViewport(g_ctx, 320.0f, 320.0f, 240.0f, -240.0f, 0.0f, 1.0f);
    sceGxmSetCullMode(g_ctx, SCE_GXM_CULL_NONE);
    sceGxmSetFrontFragmentProgramEnable(g_ctx, SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetBackFragmentProgramEnable(g_ctx, SCE_GXM_FRAGMENT_PROGRAM_ENABLED);
    sceGxmSetFrontStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0);
    sceGxmSetBackStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0);
    g_open = t; g_open_draws = 0; g_vring_used = g_iring_used = 0;
    return 1;
}

/* ---- program lookup ------------------------------------------------------------ */
static uint64_t hash_vp(const h2_command_state *s)
{
    uint64_t hv = 0xCBF29CE484222325ull ^ s->program_start;
    for (uint32_t i = 0; i < s->program_load && i < 136; ++i)
        for (unsigned k = 0; k < 4; ++k) { hv ^= s->program[i][k]; hv *= 0x100000001B3ull; }
    return hv;
}
static uint64_t hash_ps(const h2_command_state *s, const menu_combiner *cb)
{
    uint64_t hp = 0x84222325CBF29CE4ull ^ cb->stages;
    for (unsigned i = 0; i < cb->stages && i < 8; ++i) {
        uint32_t w[4] = {cb->rgb_in[i], cb->rgb_out[i], cb->alpha_in[i], cb->alpha_out[i]};
        for (unsigned k = 0; k < 4; ++k) { hp ^= w[k]; hp *= 0x100000001B3ull; }
    }
    hp ^= cb->final_abcd; hp *= 0x100000001B3ull; hp ^= cb->final_efg; hp *= 0x100000001B3ull;
    hp ^= s->setup[0x1E70 / 4]; hp *= 0x100000001B3ull;
    return hp;
}

static void note_missing(uint64_t h, const char *kind)
{
    for (unsigned i = 0; i < g_nmissing; ++i) if (g_missing_logged[i] == h) return;
    if (g_nmissing < 64) g_missing_logged[g_nmissing++] = h;
    xv_logf("[h2/menu-gxm] no compiled %s for %016llx; draws with it use the software path\n", kind, (unsigned long long)h);
}

static vs_entry *get_vs(const h2_command_state *s)
{
    uint64_t hv = hash_vp(s);
    for (unsigned i = 0; i < g_nvs; ++i) if (g_vs[i].hash == hv) return g_vs[i].missing ? NULL : &g_vs[i];
    if (g_nvs == MAX_VS) return NULL;
    vs_entry *e = &g_vs[g_nvs++]; memset(e, 0, sizeof *e); e->hash = hv;
    char path[80]; snprintf(path, sizeof path, "h2menu_vs_%016llx.gxp", (unsigned long long)hv);
    e->gxp = load_gxp(path);
    if (!e->gxp) { e->missing = 1; note_missing(hv, "vertex program"); return NULL; }
    if (sceGxmShaderPatcherRegisterProgram(g_patcher, e->gxp, &e->id) < 0) { e->missing = 1; return NULL; }
    SceGxmVertexAttribute attr[16]; SceGxmVertexStream stream;
    e->nattr = 0;
    for (unsigned v = 0; v < 16; ++v) {
        char qualified[32];                                 /* Cg names the inputs after the AppIn struct: "IN.position" */
        snprintf(qualified, sizeof qualified, "IN.%s", VREG_NAMES[v]);
        const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName(e->gxp, qualified);
        if (!p) continue;
        attr[e->nattr].streamIndex = 0; attr[e->nattr].offset = (uint16_t)(e->nattr * 16);
        attr[e->nattr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32; attr[e->nattr].componentCount = 4;
        attr[e->nattr].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(p);
        e->vregs[e->nattr++] = (uint8_t)v;
    }
    e->stride = e->nattr * 16;
    stream.stride = (uint16_t)e->stride; stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    if (sceGxmShaderPatcherCreateVertexProgram(g_patcher, e->id, attr, e->nattr, &stream, 1, &e->prog) < 0) { e->missing = 1; return NULL; }
    e->c = sceGxmProgramFindParameterByName(e->gxp, "c");
    e->c_count = e->c ? sceGxmProgramParameterGetArraySize(e->c) : 0;
    xv_logf("[h2/menu-gxm] vertex program %016llx: %u attributes, c[%u]\n", (unsigned long long)hv, e->nattr, e->c_count);
    return e;
}

static int gl_factor(uint32_t f, SceGxmBlendFactor *out)
{
    switch (f) {
    case 0: *out = SCE_GXM_BLEND_FACTOR_ZERO; return 1;
    case 1: *out = SCE_GXM_BLEND_FACTOR_ONE; return 1;
    case 0x300: *out = SCE_GXM_BLEND_FACTOR_SRC_COLOR; return 1;
    case 0x301: *out = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; return 1;
    case 0x302: *out = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; return 1;
    case 0x303: *out = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; return 1;
    case 0x304: *out = SCE_GXM_BLEND_FACTOR_DST_ALPHA; return 1;
    case 0x305: *out = SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA; return 1;
    case 0x306: *out = SCE_GXM_BLEND_FACTOR_DST_COLOR; return 1;
    case 0x307: *out = SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR; return 1;
    case 0x308: *out = SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE; return 1;
    default: return 0;
    }
}
static int gl_equation(uint32_t e, SceGxmBlendFunc *out)
{
    switch (e) {
    case 0x8006: case 0xF006: *out = SCE_GXM_BLEND_FUNC_ADD; return 1;
    case 0x8007: *out = SCE_GXM_BLEND_FUNC_MIN; return 1;
    case 0x8008: *out = SCE_GXM_BLEND_FUNC_MAX; return 1;
    case 0x800A: *out = SCE_GXM_BLEND_FUNC_SUBTRACT; return 1;
    case 0x800B: case 0xF005: *out = SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT; return 1;
    default: return 0;
    }
}

static fs_entry *get_fs(const h2_command_state *s, const menu_combiner *cb, const vs_entry *vs, const SceGxmBlendInfo *blend, uint32_t blend_key)
{
    uint64_t hp = hash_ps(s, cb), key = hp ^ ((uint64_t)blend_key << 40) ^ (vs->hash << 8);
    for (unsigned i = 0; i < g_nfs; ++i) if (g_fs[i].key == key) return g_fs[i].missing ? NULL : &g_fs[i];
    if (g_nfs == MAX_FS) return NULL;
    fs_entry *e = &g_fs[g_nfs++]; memset(e, 0, sizeof *e); e->hash = hp; e->key = key; e->vs = vs;
    /* one compiled program per (combiner, vertex program) pair - it only reads the varyings that
     * vertex program writes; the raw program is shared between its blend variants */
    for (unsigned i = 0; i + 1 < g_nfs; ++i) if (g_fs[i].hash == hp && g_fs[i].vs == vs && g_fs[i].gxp) { e->gxp = g_fs[i].gxp; e->id = g_fs[i].id; break; }
    if (!e->gxp) {
        char path[96]; snprintf(path, sizeof path, "h2menu_ps_%016llx_%016llx.frag.gxp", (unsigned long long)hp, (unsigned long long)vs->hash);
        e->gxp = load_gxp(path);
        if (!e->gxp) { e->missing = 1; note_missing(hp, "fragment program"); return NULL; }
        if (sceGxmShaderPatcherRegisterProgram(g_patcher, e->gxp, &e->id) < 0) { e->missing = 1; return NULL; }
    }
    if (sceGxmShaderPatcherCreateFragmentProgram(g_patcher, e->id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                                 blend, vs->gxp, &e->prog) < 0) { e->missing = 1; return NULL; }
    e->psc = sceGxmProgramFindParameterByName(e->gxp, "psc");
    e->fog = sceGxmProgramFindParameterByName(e->gxp, "xv_fogcolor");
    e->atest = sceGxmProgramFindParameterByName(e->gxp, "xv_atest");
    e->texscale = sceGxmProgramFindParameterByName(e->gxp, "xv_texscale");
    e->blendconst = sceGxmProgramFindParameterByName(e->gxp, "xv_blendconst");
    for (unsigned u = 0; u < 4; ++u) {
        char name[8]; snprintf(name, sizeof name, "tex%u", u);
        const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName(e->gxp, name);
        e->sampler[u] = p ? (int)sceGxmProgramParameterGetResourceIndex(p) : -1;
    }
    return e;
}

/* ---- textures ---------------------------------------------------------------- */
static tex_entry *get_texture(const h2_command_state *s, const h2_kelvin_clear *c, unsigned unit)
{
    uint32_t tw = 0, th = 0; int linear = 0; uint64_t hash = 0;
    const uint32_t *px = menu_texture_acquire(s, c, unit, g_serial, TEX_CAP, &tw, &th, &linear, &hash);
    if (!px) return NULL;
    tex_entry *e = NULL, *victim = NULL;
    for (unsigned i = 0; i < MAX_TEX; ++i) {
        if (g_tex[i].mem && g_tex[i].hash == hash) { e = &g_tex[i]; break; }
        if (!victim || (!g_tex[i].mem && victim->mem) || (g_tex[i].mem && victim->mem && g_tex[i].used < victim->used)) victim = &g_tex[i];
    }
    if (!e) {
        uint32_t bytes = tw * th * 4;
        if (bytes > TEX_POOL) return NULL;
        if (g_texpool_used + bytes > TEX_POOL) {            /* simple pool reset: everything re-uploads */
            flush_scene(); for (unsigned i = 0; i < MAX_TEX; ++i) g_tex[i].mem = NULL; g_texpool_used = 0;
        }
        e = victim; if (!e) return NULL;
        e->mem = (uint32_t *)(g_texpool + g_texpool_used); g_texpool_used += (bytes + 255) & ~255u;
        memcpy(e->mem, px, bytes);
        e->hash = hash; e->w = tw; e->h = th; e->bytes = bytes; e->linear = linear;
        if (sceGxmTextureInitLinear(&e->tex, e->mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB, tw, th, 1) < 0) { e->mem = NULL; return NULL; }
        sceGxmTextureSetMinFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetUAddrMode(&e->tex, linear ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
        sceGxmTextureSetVAddrMode(&e->tex, linear ? SCE_GXM_TEXTURE_ADDR_CLAMP : SCE_GXM_TEXTURE_ADDR_REPEAT);
    }
    e->used = g_serial;
    return e;
}

/* ---- vertex fetch (same decoding as the software path) ------------------------ */
typedef struct { const uint8_t *base; uint32_t stride, type, count; int enabled; } vattr;
static float rd_f32(const uint8_t *p) { float f; memcpy(&f, p, 4); return f; }
static int rd_s16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd_u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint32_t attr_bytes(uint32_t type, uint32_t count)
{ switch (type) { case 2: return count * 4; case 1: case 5: return count * 2; case 0: case 4: return count; default: return 4; } }
static void decode_attr(const vattr *a, const uint8_t *p, float out[4])
{
    out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f;
    if (!a->enabled || !p) return;
    unsigned n = a->count < 4 ? a->count : 4;
    switch (a->type) {
    case 2: for (unsigned k = 0; k < n; ++k) out[k] = rd_f32(p + k * 4); break;
    case 1: for (unsigned k = 0; k < n; ++k) out[k] = rd_s16(p + k * 2) / 32767.0f; break;
    case 5: for (unsigned k = 0; k < n; ++k) out[k] = (float)rd_s16(p + k * 2); break;
    case 0: if (n == 4) { out[0] = p[2] / 255.0f; out[1] = p[1] / 255.0f; out[2] = p[0] / 255.0f; out[3] = p[3] / 255.0f; }
            else for (unsigned k = 0; k < n; ++k) out[k] = p[k] / 255.0f;
            break;
    case 4: for (unsigned k = 0; k < n; ++k) out[k] = p[k] / 255.0f; break;
    case 6: { uint32_t w = rd_u32(p); int x = (int)(w << 21) >> 21, y = (int)(w << 10) >> 21, z = (int)w >> 22;
              out[0] = x / 1023.0f; out[1] = y / 1023.0f; out[2] = z / 511.0f; } break;
    default: break;
    }
}
static void resolve_arrays(const h2_command_state *s, const h2_kelvin_clear *c, vattr attrs[16], uint32_t maxv)
{
    for (unsigned a = 0; a < 16; ++a) {
        attrs[a].enabled = 0; attrs[a].base = NULL;
        uint32_t fmt = s->setup[(0x1760 + a * 4) / 4];
        uint32_t type = fmt & 0xF, count = (fmt >> 4) & 0xF, stride = (fmt >> 8) & 0xFFFFFF;
        if (count == 0) continue;
        uint32_t off = s->setup[(0x1720 + a * 4) / 4];
        unsigned dmab = off >> 31;
        uint32_t byteoff = off & 0x7FFFFFFF, inst = s->dma[dmab ? 8 : 7];
        if (!inst) continue;
        h2_dma_object obj;
        if (!h2_dma_load(c->read_instance, c->opaque, inst, &obj)) continue;
        uint32_t span = (uint64_t)(maxv + 1) * stride + attr_bytes(type, count) > 0x08000000u ? 0 : (maxv + 1) * stride + attr_bytes(type, count);
        uint32_t phys;
        if (!span || !h2_dma_resolve(&obj, byteoff, span, 0, c->physical_bytes, &phys)) continue;
        const uint8_t *p = c->map_physical(c->opaque, phys, span);
        if (!p) continue;
        attrs[a].base = p; attrs[a].stride = stride; attrs[a].type = type; attrs[a].count = count; attrs[a].enabled = 1;
    }
}

/* ---- the draw ---------------------------------------------------------------- */
int h2_menu_gxm_render(void *opaque, const h2_menu_request *r)
{
    (void)opaque;
    const h2_command_state *s = r->state;
    const h2_kelvin_clear *c = r->clear;
    if (g_ready < 0) g_ready = initialize();
    if (g_ready != 1) return -1;
    if (!s || !c || !c->map_physical || !c->read_instance || !c->has_color_dma || !s->program_load) return 0;
    uint32_t cw = c->clip_horizontal >> 16, ch = c->clip_vertical >> 16;
    if (cw != W || ch != H) { flush_scene(); return -1; }          /* only the 640x480 back buffers here */
    ++g_serial;

    /* target */
    h2_dma_object dmac; uint32_t cphys;
    if (!h2_dma_load(c->read_instance, c->opaque, c->dma_color, &dmac) ||
        !h2_dma_resolve(&dmac, c->color_offset, W * H * 4, 1, c->physical_bytes, &cphys)) return 0;
    uint8_t *guest = c->map_physical(c->opaque, cphys, W * H * 4);
    if (!guest) return 0;
    target *t = find_target(c, c->color_offset, guest);
    if (!t) { flush_scene(); return -1; }
    if (!r->vertex_count && !r->index_count && !r->array_count) return 1;   /* BEGIN/END with no emission */

    /* programs */
    static menu_combiner cb;
    menu_combiner_decode(s, &cb);
    vs_entry *vs = get_vs(s);
    if (!vs) { flush_scene(); return -1; }
    SceGxmBlendInfo blend; memset(&blend, 0, sizeof blend);
    blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
    uint32_t blend_key = 0;
    float blendconst[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (s->setup[0x304 / 4]) {
        SceGxmBlendFactor sf, df; SceGxmBlendFunc eq;
        uint32_t sfv = s->setup[0x344 / 4], dfv = s->setup[0x348 / 4];
        /* GXM has no CONSTANT_COLOR factors: fold NV097 0x8001/0x8002 into the fragment output
         * (xv_blendconst) and blend with ONE; a constant destination factor is left to the software path. */
        if (sfv == 0x8001 || sfv == 0x8002) {
            uint32_t bc = s->setup[0x34C / 4];
            for (unsigned k = 0; k < 3; ++k) {
                float v = ((bc >> (16 - 8 * k)) & 255) / 255.0f;
                blendconst[k] = sfv == 0x8001 ? v : 1.0f - v;
            }
            sfv = 1;
        }
        if (dfv >= 0x8001 && dfv <= 0x8004) { flush_scene(); return -1; }
        if (!gl_factor(sfv, &sf) || !gl_factor(dfv, &df) || !gl_equation(s->setup[0x350 / 4], &eq)) {
            sf = SCE_GXM_BLEND_FACTOR_SRC_ALPHA; df = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; eq = SCE_GXM_BLEND_FUNC_ADD;
        }
        blend.colorFunc = blend.alphaFunc = eq; blend.colorSrc = blend.alphaSrc = sf; blend.colorDst = blend.alphaDst = df;
        blend_key = 1u | (uint32_t)sf << 4 | (uint32_t)df << 12 | (uint32_t)eq << 20;
    } else {
        blend.colorFunc = blend.alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
        blend.colorSrc = blend.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE; blend.colorDst = blend.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    }
    fs_entry *fs = get_fs(s, &cb, vs, &blend, blend_key);
    if (!fs) { flush_scene(); return -1; }

    /* textures: a unit sampling the open target's buffer needs the scene flushed first */
    tex_entry *tex[4] = {0};
    for (unsigned u = 0; u < 4; ++u) {
        if (!(cb.tex_used & (1u << u)) || fs->sampler[u] < 0) continue;
        uint32_t toff = s->setup[(0x1B00 + u * 64) / 4];
        if (g_open && toff >= g_open->color_offset && toff < g_open->color_offset + W * H * 4) flush_scene();
        tex[u] = get_texture(s, c, u);
    }

    /* vertices: packed F32x4 per attribute the program declares, in vreg order */
    uint32_t n = 0, count = 0; const uint16_t *src_idx = NULL; uint32_t base = 0;
    if (r->vertex_count) { n = r->vertex_count; count = n; }
    else if (r->index_count) { for (uint32_t i = 0; i < r->index_count; ++i) if (r->indices[i] > n) n = r->indices[i]; ++n; src_idx = r->indices; count = r->index_count; }
    else if (r->array_count) { base = r->array_start; n = r->array_start + r->array_count; count = r->array_count; }
    else return 1;
    if (n > 65535 || count > 65535 * 3 || n * vs->stride > VERTEX_RING) return 0;
    if (!open_scene(t)) return 0;
    if (g_vring_used + n * vs->stride > VERTEX_RING || g_iring_used + (count * 2 + 8) * 2 > INDEX_RING) { flush_scene(); if (!open_scene(t)) return 0; }
    float *vb = (float *)(g_vring + g_vring_used);
    if (r->vertex_count) {
        for (uint32_t i = 0; i < n; ++i)
            for (unsigned a = 0; a < vs->nattr; ++a) memcpy(vb + (i * vs->nattr + a) * 4, r->vertices[i].attribute[vs->vregs[a]], 16);
    } else {
        vattr attrs[16]; resolve_arrays(s, c, attrs, n - 1);
        for (uint32_t vi = base; vi < n; ++vi)
            for (unsigned a = 0; a < vs->nattr; ++a) {
                unsigned v = vs->vregs[a];
                decode_attr(&attrs[v], attrs[v].enabled ? attrs[v].base + (uint64_t)vi * attrs[v].stride : NULL, vb + (vi * vs->nattr + a) * 4);
            }
    }
    uint16_t *ib = g_iring + g_iring_used / 2; uint32_t ni = 0;
    SceGxmPrimitiveType prim;
    switch (r->primitive) {
    case 5: prim = SCE_GXM_PRIMITIVE_TRIANGLES; break;
    case 6: prim = SCE_GXM_PRIMITIVE_TRIANGLE_STRIP; break;
    case 7: prim = SCE_GXM_PRIMITIVE_TRIANGLE_FAN; break;
    case 8: prim = SCE_GXM_PRIMITIVE_TRIANGLES; break;   /* quads expand below */
    default: return 0;
    }
    if (r->primitive == 8) {
        for (uint32_t i = 0; i + 3 < count; i += 4) {
            uint16_t q[4]; for (unsigned k = 0; k < 4; ++k) q[k] = (uint16_t)(src_idx ? src_idx[i + k] : base + i + k);
            ib[ni++] = q[0]; ib[ni++] = q[1]; ib[ni++] = q[2]; ib[ni++] = q[0]; ib[ni++] = q[2]; ib[ni++] = q[3];
        }
    } else {
        for (uint32_t i = 0; i < count; ++i) ib[ni++] = (uint16_t)(src_idx ? src_idx[i] : base + i);
    }
    if (ni < 3) return 1;

    /* state */
    sceGxmSetVertexProgram(g_ctx, vs->prog); sceGxmSetFragmentProgram(g_ctx, fs->prog);
    SceGxmDepthFunc dfn = SCE_GXM_DEPTH_FUNC_ALWAYS;
    if (s->setup[0x30C / 4]) {
        switch (s->setup[0x354 / 4]) {
        case 0x200: dfn = SCE_GXM_DEPTH_FUNC_NEVER; break; case 0x201: dfn = SCE_GXM_DEPTH_FUNC_LESS; break;
        case 0x202: dfn = SCE_GXM_DEPTH_FUNC_EQUAL; break; case 0x203: dfn = SCE_GXM_DEPTH_FUNC_LESS_EQUAL; break;
        case 0x204: dfn = SCE_GXM_DEPTH_FUNC_GREATER; break; case 0x205: dfn = SCE_GXM_DEPTH_FUNC_NOT_EQUAL; break;
        case 0x206: dfn = SCE_GXM_DEPTH_FUNC_GREATER_EQUAL; break; default: dfn = SCE_GXM_DEPTH_FUNC_ALWAYS; break;
        }
    }
    SceGxmDepthWriteMode dw = (s->setup[0x30C / 4] && (s->setup[0x35C / 4] & 1)) ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED;
    sceGxmSetFrontDepthFunc(g_ctx, dfn); sceGxmSetBackDepthFunc(g_ctx, dfn);
    sceGxmSetFrontDepthWriteEnable(g_ctx, dw); sceGxmSetBackDepthWriteEnable(g_ctx, dw);
    sceGxmSetRegionClip(g_ctx, SCE_GXM_REGION_CLIP_OUTSIDE, 0, 0, W - 1, H - 1);
    {   /* Z-pass pixel count: slot 0 accumulates while NV097_SET_ZPASS_PIXEL_COUNT_ENABLE is set */
        SceGxmVisibilityTestMode vm = s->zpass_enable ? SCE_GXM_VISIBILITY_TEST_ENABLED : SCE_GXM_VISIBILITY_TEST_DISABLED;
        sceGxmSetFrontVisibilityTestIndex(g_ctx, 0); sceGxmSetBackVisibilityTestIndex(g_ctx, 0);
        sceGxmSetFrontVisibilityTestOp(g_ctx, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT); sceGxmSetBackVisibilityTestOp(g_ctx, SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
        sceGxmSetFrontVisibilityTestEnable(g_ctx, vm); sceGxmSetBackVisibilityTestEnable(g_ctx, vm);
    }

    /* uniforms */
    void *vu = NULL, *fu = NULL;
    GCHECK(sceGxmReserveVertexDefaultUniformBuffer(g_ctx, &vu));
    if (vs->c && vs->c_count) GCHECK(sceGxmSetUniformDataF(vu, vs->c, 0, (vs->c_count < 192 ? vs->c_count : 192) * 4, (const float *)s->constants));
    GCHECK(sceGxmReserveFragmentDefaultUniformBuffer(g_ctx, &fu));
    float psc[18][4], fog[4] = {0, 0, 0, 0}, atest[4], scale[4][4];
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t c0 = cb.factor0[i], c1 = cb.factor1[i];
        psc[i][0] = ((c0 >> 16) & 255) / 255.0f; psc[i][1] = ((c0 >> 8) & 255) / 255.0f; psc[i][2] = (c0 & 255) / 255.0f; psc[i][3] = (c0 >> 24) / 255.0f;
        psc[8 + i][0] = ((c1 >> 16) & 255) / 255.0f; psc[8 + i][1] = ((c1 >> 8) & 255) / 255.0f; psc[8 + i][2] = (c1 & 255) / 255.0f; psc[8 + i][3] = (c1 >> 24) / 255.0f;
    }
    for (unsigned k = 0; k < 2; ++k) {
        uint32_t v = k ? cb.final_factor1 : cb.final_factor0;
        psc[16 + k][0] = ((v >> 16) & 255) / 255.0f; psc[16 + k][1] = ((v >> 8) & 255) / 255.0f; psc[16 + k][2] = (v & 255) / 255.0f; psc[16 + k][3] = (v >> 24) / 255.0f;
    }
    atest[0] = (float)(s->setup[0x340 / 4] & 255) / 255.0f; atest[1] = (float)(s->setup[0x33C / 4] & 7); atest[2] = (float)(s->setup[0x300 / 4] & 1); atest[3] = 0;
    for (unsigned u = 0; u < 4; ++u) { scale[u][0] = scale[u][1] = 1.0f; scale[u][2] = scale[u][3] = 0.0f;
        if (tex[u] && tex[u]->linear) { scale[u][0] = 1.0f / (float)tex[u]->w; scale[u][1] = 1.0f / (float)tex[u]->h; } }
    if (fs->psc) GCHECK(sceGxmSetUniformDataF(fu, fs->psc, 0, 18 * 4, &psc[0][0]));
    if (fs->fog) GCHECK(sceGxmSetUniformDataF(fu, fs->fog, 0, 4, fog));
    if (fs->atest) GCHECK(sceGxmSetUniformDataF(fu, fs->atest, 0, 4, atest));
    if (fs->texscale) GCHECK(sceGxmSetUniformDataF(fu, fs->texscale, 0, 16, &scale[0][0]));
    if (fs->blendconst) GCHECK(sceGxmSetUniformDataF(fu, fs->blendconst, 0, 4, blendconst));
    for (unsigned u = 0; u < 4; ++u)
        if (fs->sampler[u] >= 0 && tex[u]) GCHECK(sceGxmSetFragmentTexture(g_ctx, fs->sampler[u], &tex[u]->tex));
    GCHECK(sceGxmSetVertexStream(g_ctx, 0, vb));
    GCHECK(sceGxmDraw(g_ctx, prim, SCE_GXM_INDEX_FORMAT_U16, ib, ni));
    g_vring_used += n * vs->stride; g_iring_used += (ni * 2 + 3) & ~3u;
    ++g_open_draws; ++g_drawn;
    if (g_drawn <= 40 || !(g_drawn % 500))
        xv_logf("[h2/menu-gxm] draw=%llu prim=%u verts=%u indices=%u target=%08X vs=%016llx ps=%016llx scene_draws=%u fallbacks=%llu\n",
                (unsigned long long)g_drawn, r->primitive, n, ni, c->color_offset, (unsigned long long)vs->hash,
                (unsigned long long)fs->hash, g_open_draws, (unsigned long long)g_fallbacks);
    static int dump_every = -1;
    if (dump_every < 0) { const char *e = getenv("XV_MENU_GXM_DUMP"); dump_every = e ? atoi(e) : 200; }
    if (dump_every > 0 && !(g_drawn % (uint64_t)dump_every)) { flush_scene(); h2_menu_dump_target(t->guest, W, H, g_drawn, c->color_offset); }
    return 1;
}
