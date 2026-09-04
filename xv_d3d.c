/*
 * xv_d3d.c - Direct3D 8 (Xbox) HLE: record on the guest fiber, replay on the pump.
 * See xv_d3d.h for the contract and the blueprint sections it implements.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/gxm.h>

#include "xv_d3d.h"

#include "xv_log.h"
#define XV_LOG(...)     xv_logf("[xv/d3d] " __VA_ARGS__)
#define XV_ONCE(flag, ...) do { static int flag; if (!flag) { flag = 1; XV_LOG(__VA_ARGS__); } } while (0)

#define XV_MAX_VS        96          /* vertex shader handles                          */
#define XV_MAX_CMDS      2048        /* draws + clears per frame                       */
#define XV_CONST_POOL    (256 * 1024) /* floats per list: c[] snapshots (1 MB; full-window programs = 768 floats each) */
#define XV_TEX_CACHE     512         /* texture control words                          */
#define XV_CLEAR_SLOTS   16          /* clear quads per frame                          */
#define XV_SEQ_INDICES   65536       /* sequential u16 indices for DrawVertices        */
#define XV_QUAD_INDICES  49152       /* per list: u16 indices rewritten from QUADLIST/POLYGON draws */
#define XV_NUM_LISTS     2

/* ----------------------------------------------------------------------------------
 *  Fragment programs.  GXM bakes the vertex program's output layout and the blend
 *  state into each fragment program, so they are created per (vertex shader,
 *  fragment kind, blend) and cached.
 * -------------------------------------------------------------------------------- */
enum { FS_COLOR = 0, FS_TEXMOD = 1, FS_TEX0 = 2, FS_LM = 3, FS_KINDS = 4 };
enum { BLEND_OPAQUE = 0, BLEND_ALPHA = 1, BLEND_ADD = 2, BLEND_MODES = 24, BLEND_NOCOLOR = BLEND_MODES - 1 };
/* blend variants are fragment-program link variants; slots beyond the two fixed ones are handed out
 * to whatever (src,dst) factor pairs the game actually uses (Halo: DESTCOLOR/ZERO, DESTCOLOR/SRCCOLOR,
 * ONE/INVSRCALPHA, SRCALPHA/ONE ...) */
static struct { uint8_t src, dst, mask; } g_blend_combo[BLEND_MODES] = {   /* mask: D3DRS_COLORWRITEENABLE bits (R1 G2 B4 A8) */
    { X_D3DBLEND_ONE, X_D3DBLEND_ZERO, 0xF }, { X_D3DBLEND_SRCALPHA, X_D3DBLEND_INVSRCALPHA, 0xF }, { X_D3DBLEND_ONE, X_D3DBLEND_ONE, 0xF } };
static unsigned g_blend_combos = 3;
static const char *const FS_GXP[FS_KINDS] = { "app0:shaders/xv_color.frag.gxp", "app0:shaders/xv_texmod.frag.gxp", "app0:shaders/xv_tex0.frag.gxp", "app0:shaders/xv_lm.frag.gxp" };

/* Recompiled register-combiner programs (Stage 3b, tools/ps_pipeline.py): fragment programs keyed by
 * the combiner hash the game submitted.  Linked lazily per (vertex program, combiner, blend). */
#include "xv_ps_table.h"
#define XV_PS_PER_VS 12
typedef struct { int16_t entry; uint8_t blend, failed; xv_fshader_t fs; } ps_link_t;

typedef struct {
    xv_vshader_t  vs;
    int           loaded;
    xv_fshader_t  fs[FS_KINDS][BLEND_MODES];
    int           fs_loaded[FS_KINDS][BLEND_MODES];
    ps_link_t     ps[XV_PS_PER_VS];
    unsigned      nps;
} vs_slot_t;

/* ----------------------------------------------------------------------------------
 *  Command list
 * -------------------------------------------------------------------------------- */
typedef struct {
    uint8_t   kind;                 /* 0 draw, 1 clear                                */
    uint8_t   fs_kind, blend;
    uint32_t  prim;                 /* SceGxmPrimitiveType (bitfield values, not small ints) */
    uint8_t   depth_func_idx, depth_write, cull;
    uint8_t   ntex;
    uint16_t  vs;                   /* slot index                                     */
    uint32_t  index_count;
    const void *streams[XV_MAX_STREAMS];
    const void *indices;
    uint32_t  const_off, const_n;   /* float4 units into the pool                     */
    float     const_attr[4];
    uint32_t  fog_color;            /* D3DCOLOR at record time (Halo toggles it within a frame) */
    SceGxmTexture tex[4];
    int16_t   ps_entry;             /* xv_ps_table index, or -1 (heuristic fragment)  */
    uint8_t   pass;                 /* 0 = back buffer, n = offscreen pass n           */
    float     psc[18][4];           /* combiner constants: per-stage c0[8], c1[8], final c0, c1 */
    uint32_t  clear_color;          /* clear only                                     */
    float     clear_z;
    uint8_t   clear_flags;
} cmd_t;

/* Render-to-texture: Halo renders object shadows (and a few effects) into textures it created with
 * CreateTexture, then samples them.  Each SetRenderTarget to such a surface opens a PASS: the draws and
 * clears recorded until the back buffer returns are tagged with it and replayed first, in their own GXM
 * scene straight into the texture's guest memory (the arena is GXM-mapped R/W), so the main scene can
 * sample the result through an alias texture instead of a CPU decode. */
#define XV_MAX_PASSES 4
typedef struct { uint32_t data; unsigned w, h, fmt; } rt_pass_t;
typedef struct {
    cmd_t     cmds[XV_MAX_CMDS];
    unsigned  ncmds;
    float     consts[XV_CONST_POOL];
    unsigned  nconsts;              /* floats used                                    */
    unsigned  last_gen, last_off, last_n; int last_base;   /* previous snapshot: reused while c[] is unchanged */
    unsigned  const_dropped;
    unsigned  dropped;
    rt_pass_t passes[XV_MAX_PASSES];
    unsigned  npasses, cur_pass;    /* cur_pass: 0 = back buffer, else index+1        */
} cmdlist_t;

/* ----------------------------------------------------------------------------------
 *  State
 * -------------------------------------------------------------------------------- */
typedef struct {
    uint32_t stream_guest[XV_MAX_STREAMS];   /* X_D3DResource guest addresses           */
    unsigned stream_stride[XV_MAX_STREAMS];
    unsigned vsc_gen;                                         /* bumps whenever SetAllConstants changes c[] */
    uint32_t tex_guest[4];
    uint32_t pal_guest[4];                                    /* SetPalette per stage (P8 textures) */
    uint8_t  tex_min[4], tex_mag[4], tex_addr_u[4], tex_addr_v[4];
    uint32_t vs_handle;
    float    vsc[192][4];                    /* c[-96..95] as D3D exposes them          */
    float    const_attr[4];
    uint32_t z_enable, z_write, z_func, cull, blend_enable, src_blend, dst_blend, color_mask;
    uint32_t indices_dbg;                    /* last index pointer handed to a draw (guest), for traces */
    uint32_t ps_hash;                        /* combiner program id (recomp/kernel/xd3d.c psdef_hash) */
    float    psc[18][4];                     /* c0[stage 0..7], c1[stage 0..7], final c0, c1 */
} d3d_state_t;

static const xv_vs_desc_t *const *g_table;
static unsigned            g_table_count;
static vs_slot_t           g_vs[XV_MAX_VS];
static unsigned            g_nvs;
static d3d_state_t         S;
static cmdlist_t          *g_lists[XV_NUM_LISTS];
static uint32_t            g_build_frame;              /* frame being recorded         */
static uint32_t            g_clear_vs;                  /* handle of xv_clear           */

/* GPU-visible scratch: sequential indices + clear quads (uncached, mapped READ). */
static SceUID    g_scratch_uid;
static uint8_t  *g_scratch;
static uint16_t *g_seq_indices;
static uint8_t  *g_clear_quads;                          /* XV_NUM_LISTS x SLOTS x 4 verts x 16 B */
static uint16_t *g_quad_indices;                         /* XV_NUM_LISTS x XV_QUAD_INDICES */
static uint32_t  g_quad_used[XV_NUM_LISTS];

typedef struct { uint32_t data, format; SceGxmTexture tex; uint8_t valid; } tex_entry_t;
static tex_entry_t g_texcache[XV_TEX_CACHE];

static const SceGxmDepthFunc DEPTH_FUNCS[] = {
    SCE_GXM_DEPTH_FUNC_NEVER, SCE_GXM_DEPTH_FUNC_LESS, SCE_GXM_DEPTH_FUNC_EQUAL, SCE_GXM_DEPTH_FUNC_LESS_EQUAL,
    SCE_GXM_DEPTH_FUNC_GREATER, SCE_GXM_DEPTH_FUNC_NOT_EQUAL, SCE_GXM_DEPTH_FUNC_GREATER_EQUAL, SCE_GXM_DEPTH_FUNC_ALWAYS,
};

/* ----------------------------------------------------------------------------------
 *  Init / shutdown
 * -------------------------------------------------------------------------------- */
int xv_d3d_init(const xv_vs_desc_t *const *table, unsigned count)
{
    g_table = table;
    g_table_count = count;
    for (int i = 0; i < XV_NUM_LISTS; ++i) {
        g_lists[i] = (cmdlist_t *)calloc(1, sizeof(cmdlist_t));
        if (!g_lists[i])
            return -1;
    }
    uint32_t size = (XV_SEQ_INDICES * 2 + XV_NUM_LISTS * XV_CLEAR_SLOTS * 4 * 16 + XV_NUM_LISTS * XV_QUAD_INDICES * 2 + 0xFFF) & ~0xFFFu;
    g_scratch_uid = sceKernelAllocMemBlock("xv_d3d_scratch", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (g_scratch_uid < 0)
        return g_scratch_uid;
    sceKernelGetMemBlockBase(g_scratch_uid, (void **)&g_scratch);
    int err = sceGxmMapMemory(g_scratch, size, SCE_GXM_MEMORY_ATTRIB_READ);
    if (err < 0)
        return err;
    g_seq_indices = (uint16_t *)g_scratch;
    for (uint32_t i = 0; i < XV_SEQ_INDICES; ++i)
        g_seq_indices[i] = (uint16_t)i;
    g_clear_quads = g_scratch + XV_SEQ_INDICES * 2;
    g_quad_indices = (uint16_t *)(g_clear_quads + XV_NUM_LISTS * XV_CLEAR_SLOTS * 4 * 16);

    memset(&S, 0, sizeof(S)); S.color_mask = 0xF;
    S.z_enable = 1; S.z_write = 1; S.z_func = X_D3DCMP_LESSEQUAL; S.cull = X_D3DCULL_CCW;
    S.src_blend = X_D3DBLEND_ONE; S.dst_blend = X_D3DBLEND_ZERO;
    S.const_attr[3] = 1.0f;
    for (int i = 0; i < 4; ++i) {
        S.tex_min[i] = S.tex_mag[i] = X_D3DTEXF_POINT;
        S.tex_addr_u[i] = S.tex_addr_v[i] = X_D3DTADDRESS_WRAP;
    }
    XV_LOG("HLE up: %u recompiled vertex shaders known, %u cmd slots/frame\n", count, XV_MAX_CMDS);
    return 0;
}

void xv_d3d_shutdown(void)
{
    for (unsigned i = 0; i < g_nvs; ++i) {
        for (int k = 0; k < FS_KINDS; ++k)
            for (int b = 0; b < BLEND_MODES; ++b)
                if (g_vs[i].fs_loaded[k][b])
                    xv_fshader_unload(&g_vs[i].fs[k][b]);
        if (g_vs[i].loaded)
            xv_vshader_unload(&g_vs[i].vs);
    }
    if (g_scratch) {
        sceGxmUnmapMemory(g_scratch);
        sceKernelFreeMemBlock(g_scratch_uid);
    }
    for (int i = 0; i < XV_NUM_LISTS; ++i)
        free(g_lists[i]);
}

/* ----------------------------------------------------------------------------------
 *  Vertex shaders
 * -------------------------------------------------------------------------------- */
static uint32_t fnv1a(const uint8_t *p, uint32_t n)
{
    uint32_t h = 0x811C9DC5u;
    while (n--)
        h = (h ^ *p++) * 0x01000193u;
    return h;
}

static uint32_t slot_to_handle(unsigned slot) { return (slot << 1) | 1; }   /* odd = shader, even = FVF */
static int handle_to_slot(uint32_t h) { return (h & 1) && ((h >> 1) < g_nvs) ? (int)(h >> 1) : -1; }

uint32_t xv_d3d_RegisterVertexShader(const xv_vs_desc_t *desc)
{
    if (g_nvs >= XV_MAX_VS)
        return 0;
    unsigned slot = g_nvs++;
    memset(&g_vs[slot], 0, sizeof(g_vs[slot]));
    if (xv_vshader_load(&g_vs[slot].vs, desc) != 0) {
        g_nvs--;
        return 0;
    }
    g_vs[slot].loaded = 1;
    return slot_to_handle(slot);
}

uint32_t xv_d3d_CreateVertexShader(uint32_t decl_guest, uint32_t func_guest)
{
    (void)decl_guest;                 /* the pairing was resolved offline (Stage 2b) */
    const uint8_t *blob = (const uint8_t *)xv_guest_ptr(func_guest);
    if (blob[0] != 0x78 || blob[1] != 0x20 || blob[2] == 0) {
        XV_LOG("CreateVertexShader: not an XVS blob at 0x%08X (%02X %02X %02X)\n", func_guest, blob[0], blob[1], blob[2]);
        return 0;
    }
    uint32_t size = 4 + 16u * blob[2];
    uint32_t hash = fnv1a(blob, size);
    for (unsigned i = 0; i < g_table_count; ++i) {
        const xv_vs_desc_t *d = g_table[i];
        if (d->func_hash == hash && d->func_size == size) {
            /* already registered? reuse */
            for (unsigned s = 0; s < g_nvs; ++s)
                if (g_vs[s].vs.desc == d)
                    return slot_to_handle(s);
            uint32_t h = xv_d3d_RegisterVertexShader(d);
            XV_LOG("CreateVertexShader(func 0x%08X, %u B, hash %08X) -> %s handle 0x%X\n",
                   func_guest, size, hash, d->gxp, h);
            return h;
        }
    }
    XV_LOG("CreateVertexShader: unknown program (hash %08X, %u B) - not in the recompiled set\n", hash, size);
    return 0;
}

void xv_d3d_SetVertexShader(uint32_t handle)
{
    if (!(handle & 1))
        XV_ONCE(warned_fvf, "SetVertexShader(FVF 0x%X): fixed-function handles not implemented yet\n", handle);
    S.vs_handle = handle;
}

void xv_d3d_SetVertexShaderConstant(int reg, const float *data, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        int r = reg + (int)i + 96;
        if (r >= 0 && r < 192)
            memcpy(S.vsc[r], data + 4 * i, 16);
    }
}

void xv_d3d_SetVertexData4f(unsigned vreg, float x, float y, float z, float w)
{
    (void)vreg;                        /* one shared value for now (see XV_CONST_STREAM) */
    S.const_attr[0] = x; S.const_attr[1] = y; S.const_attr[2] = z; S.const_attr[3] = w;
}

/* ----------------------------------------------------------------------------------
 *  Streams, textures, render state
 * -------------------------------------------------------------------------------- */
void xv_d3d_SetStreamSource(unsigned stream, uint32_t vb_guest, unsigned stride)
{
    if (stream >= XV_MAX_STREAMS)
        return;
    S.stream_guest[stream] = vb_guest;
    S.stream_stride[stream] = stride;
}

void xv_d3d_SetTexture(unsigned stage, uint32_t tex_guest)
{
    if (stage < 4)
        S.tex_guest[stage] = tex_guest;
}
void xv_d3d_SetTexturePalette(unsigned stage, uint32_t pal_guest)
{
    if (stage < 4)
        S.pal_guest[stage] = pal_guest;
}

void xv_d3d_SetTextureStageState(unsigned stage, unsigned type, uint32_t value)
{
    if (stage >= 4)
        return;
    switch (type) {
    case X_D3DTSS_ADDRESSU:  S.tex_addr_u[stage] = (uint8_t)value; break;
    case X_D3DTSS_ADDRESSV:  S.tex_addr_v[stage] = (uint8_t)value; break;
    case X_D3DTSS_MAGFILTER: S.tex_mag[stage] = (uint8_t)value; break;
    case X_D3DTSS_MINFILTER: S.tex_min[stage] = (uint8_t)value; break;
    default: break;                    /* combiner-related states arrive with the PSDEF work */
    }
}

void xv_d3d_SetRenderState_ZEnable(uint32_t v)          { S.z_enable = v; }
void xv_d3d_SetRenderState_ZWriteEnable(uint32_t v)     { S.z_write = v; }
void xv_d3d_SetRenderState_ZFunc(uint32_t v)            { S.z_func = v; }
void xv_d3d_SetRenderState_CullMode(uint32_t v)         { S.cull = v; }
void xv_d3d_SetRenderState_AlphaBlendEnable(uint32_t v) { S.blend_enable = v; }
void xv_d3d_SetRenderState_ColorWriteEnable(uint32_t v) { S.color_mask = v; }
void xv_d3d_SetRenderState_SrcBlend(uint32_t v)         { S.src_blend = v; }
void xv_d3d_SetRenderState_DestBlend(uint32_t v)        { S.dst_blend = v; }

/* --- texture control words over guest pixels (blueprint 1.4) ----------------------- */
static int xbox_format_to_gxm(uint32_t fmt, SceGxmTextureFormat *out, int *linear, int *bpp)
{
    *linear = 0;
    switch (fmt) {
    case X_D3DFMT_A8R8G8B8:     *out = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; *bpp = 32; return 0;
    case X_D3DFMT_X8R8G8B8:     *out = SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB; *bpp = 32; return 0;
    case X_D3DFMT_R5G6B5:       *out = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB;    *bpp = 16; return 0;
    case X_D3DFMT_A1R5G5B5:     *out = SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ARGB; *bpp = 16; return 0;
    case X_D3DFMT_A4R4G4B4:     *out = SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ARGB; *bpp = 16; return 0;
    case X_D3DFMT_L8:           *out = SCE_GXM_TEXTURE_FORMAT_U8_1RRR;       *bpp = 8;  return 0;
    case X_D3DFMT_A8L8:         *out = SCE_GXM_TEXTURE_FORMAT_U8U8_GRRR;     *bpp = 16; return 0;
    case X_D3DFMT_LIN_A8R8G8B8: *out = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; *bpp = 32; *linear = 1; return 0;
    case X_D3DFMT_LIN_X8R8G8B8: *out = SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB; *bpp = 32; *linear = 1; return 0;
    case X_D3DFMT_LIN_R5G6B5:   *out = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB;    *bpp = 16; *linear = 1; return 0;
    case X_D3DFMT_DXT1:         *out = SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;     *bpp = 4;  return 2;   /* needs block reorder */
    case X_D3DFMT_DXT3:         *out = SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR;     *bpp = 8;  return 2;
    case X_D3DFMT_DXT5:         *out = SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR;     *bpp = 8;  return 2;
    default: return -1;
    }
}

static SceGxmTextureAddrMode addr_mode(uint8_t x)
{
    switch (x) {
    case X_D3DTADDRESS_MIRROR:      return SCE_GXM_TEXTURE_ADDR_MIRROR;
    case X_D3DTADDRESS_CLAMP:
    case X_D3DTADDRESS_CLAMPTOEDGE: return SCE_GXM_TEXTURE_ADDR_CLAMP;
    case X_D3DTADDRESS_BORDER:      return SCE_GXM_TEXTURE_ADDR_CLAMP;
    default:                        return SCE_GXM_TEXTURE_ADDR_REPEAT;
    }
}

static unsigned g_clear_slot_used;                          /* clear quads consumed by this frame's offscreen passes */
static cmdlist_t *cur_list(void);
/* known render-target textures (guest data -> alias texture the GPU wrote) */
typedef struct { uint32_t data; unsigned w, h, fmt; SceGxmTexture tex; int valid; } rt_alias_t;
static rt_alias_t g_rt[16]; static unsigned g_rt_n;
static SceGxmColorFormat rt_color_fmt(unsigned fmt) { return (fmt == 0x05 || fmt == 0x11) ? SCE_GXM_COLOR_FORMAT_U5U6U5_RGB : SCE_GXM_COLOR_FORMAT_U8U8U8U8_ARGB; }
static SceGxmTextureFormat rt_tex_fmt(unsigned fmt) { return (fmt == 0x05 || fmt == 0x11) ? SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB : SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; }
static rt_alias_t *rt_find(uint32_t data) { for (unsigned i = 0; i < g_rt_n; ++i) if (g_rt[i].data == data) return &g_rt[i]; return NULL; }
static rt_alias_t *rt_register(uint32_t data, unsigned w, unsigned h, unsigned fmt)
{
    rt_alias_t *r = rt_find(data);
    if (!r) { if (g_rt_n >= 16) return NULL; r = &g_rt[g_rt_n++]; memset(r, 0, sizeof *r); r->data = data; }
    if (!r->valid || r->w != w || r->h != h || r->fmt != fmt) {
        r->w = w; r->h = h; r->fmt = fmt;
        r->valid = sceGxmTextureInitLinear(&r->tex, xv_guest_ptr(0x80000000u | data), rt_tex_fmt(fmt), w, h, 0) == SCE_OK;
        sceGxmTextureSetMinFilter(&r->tex, SCE_GXM_TEXTURE_FILTER_LINEAR); sceGxmTextureSetMagFilter(&r->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetUAddrMode(&r->tex, SCE_GXM_TEXTURE_ADDR_CLAMP); sceGxmTextureSetVAddrMode(&r->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
        static unsigned n; if (n++ < 8) XV_LOG("render target %08X %ux%u fmt %02X -> alias texture %s\n", data, w, h, fmt, r->valid ? "ok" : "FAILED");
    }
    return r;
}
/* SetRenderTarget: surface header (X_D3DPixelContainer) or 0 for the back buffer */
void xv_d3d_SetRenderTarget(uint32_t surface_hdr, int is_backbuffer)
{
    cmdlist_t *l = cur_list();
    if (is_backbuffer || !surface_hdr) { l->cur_pass = 0; return; }
    const uint32_t *hdr = (const uint32_t *)xv_guest_ptr(surface_hdr);
    uint32_t data = hdr[1], fw = hdr[3], sz = hdr[4];
    unsigned fmt = (fw >> 8) & 0xFF;
    unsigned w = sz ? (sz & 0xFFF) + 1 : 1u << ((fw >> 20) & 0xF), h = sz ? ((sz >> 12) & 0xFFF) + 1 : 1u << ((fw >> 24) & 0xF);
    if (!data || w > 1024 || h > 1024 || l->npasses >= XV_MAX_PASSES) { l->cur_pass = 0xFF; return; }   /* 0xFF: record nothing */
    rt_pass_t *p = &l->passes[l->npasses++];
    p->data = data; p->w = w; p->h = h; p->fmt = fmt;
    l->cur_pass = l->npasses;
    rt_register(data, w, h, fmt);
}
static int recording_dropped(void) { return cur_list()->cur_pass == 0xFF; }

#ifdef XV_RUN_RECOMP
/* Recomp build: the UI bridge owns the texture cache (CPU de-swizzle/DXT decode to linear RGBA - the GPU's
 * twiddle does not match the NV2A layout), so mesh draws borrow its control words. */
const SceGxmTexture *xv_ui_gxm_texture(uint32_t hdr);
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t hdr, uint32_t pal_guest);
static const SceGxmTexture *texture_for(unsigned stage)
{
    static SceGxmTexture t[4];
    const SceGxmTexture *src = NULL;
    if (g_rt_n) { rt_alias_t *r = rt_find(*(const uint32_t *)xv_guest_ptr(S.tex_guest[stage] + 4)); if (r && r->valid) src = &r->tex; }
    if (!src) src = xv_ui_gxm_texture_pal(S.tex_guest[stage], S.pal_guest[stage]);
    if (!src) return NULL;
    t[stage] = *src;
    sceGxmTextureSetMinFilter(&t[stage], S.tex_min[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&t[stage], S.tex_mag[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t[stage], addr_mode(S.tex_addr_u[stage]));
    sceGxmTextureSetVAddrMode(&t[stage], addr_mode(S.tex_addr_v[stage]));
    return &t[stage];
}
#else
static const SceGxmTexture *texture_for(unsigned stage)
{
    const X_D3DPixelContainer *x = (const X_D3DPixelContainer *)xv_guest_ptr(S.tex_guest[stage]);
    uint32_t key_data = x->Data, key_fmt = x->Format;
    tex_entry_t *e = &g_texcache[(key_data >> 4 ^ key_fmt * 2654435761u) % XV_TEX_CACHE];
    if (!e->valid || e->data != key_data || e->format != key_fmt) {
        SceGxmTextureFormat gf;
        int linear, bpp;
        int rc = xbox_format_to_gxm(X_FMT_FORMAT(key_fmt), &gf, &linear, &bpp);
        if (rc < 0) {
            XV_ONCE(warned_fmt, "texture format 0x%02X unsupported; texture skipped\n", X_FMT_FORMAT(key_fmt));
            return NULL;
        }
        if (rc == 2)
            XV_ONCE(warned_dxt, "DXT texture bound without block reorder (visual garbage expected until implemented)\n");
        const void *pix = xv_guest_ptr(0x80000000u | x->Data);     /* physical address (see the stream path) */
        int err;
        if (linear) {
            uint32_t w = (x->Size & 0xFFF) + 1, h = ((x->Size >> 12) & 0xFFF) + 1;
            uint32_t pitch = (((x->Size >> 24) & 0xFF) + 1) * 64;
            err = sceGxmTextureInitLinearStrided(&e->tex, pix, gf, w, h, pitch);
            xv_gpu_flush(pix, pitch * h);
        } else {
            uint32_t w = 1u << X_FMT_USIZE(key_fmt), h = 1u << X_FMT_VSIZE(key_fmt);
            uint32_t mips = X_FMT_MIPS(key_fmt) ? X_FMT_MIPS(key_fmt) : 1;
            err = sceGxmTextureInitSwizzled(&e->tex, pix, gf, w, h, mips);
            xv_gpu_flush(pix, (w * h * (uint32_t)bpp) / 8 * 4 / 3);   /* whole mip chain, upper bound */
        }
        if (err < 0) {
            XV_LOG("texture init failed 0x%08X (fmt 0x%02X)\n", err, X_FMT_FORMAT(key_fmt));
            return NULL;
        }
        e->data = key_data; e->format = key_fmt; e->valid = 1;
    }
    /* sampler state is per stage, applied on the fly (cheap bit-twiddling) */
    sceGxmTextureSetMinFilter(&e->tex, S.tex_min[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&e->tex, S.tex_mag[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&e->tex, addr_mode(S.tex_addr_u[stage]));
    sceGxmTextureSetVAddrMode(&e->tex, addr_mode(S.tex_addr_v[stage]));
    return &e->tex;
}
#endif

/* ----------------------------------------------------------------------------------
 *  Recording
 * -------------------------------------------------------------------------------- */
static cmdlist_t *cur_list(void) { return g_lists[g_build_frame % XV_NUM_LISTS]; }

static cmd_t *new_cmd(void)
{
    cmdlist_t *l = cur_list();
    if (l->ncmds >= XV_MAX_CMDS) {
        l->dropped++;
        return NULL;
    }
    cmd_t *c = &l->cmds[l->ncmds++];
    memset(c, 0, sizeof(*c));
    c->pass = (uint8_t)l->cur_pass;
    return c;
}

static int blend_mode(void)
{
    unsigned mask = S.color_mask & 0xF;
    unsigned src = S.blend_enable ? S.src_blend : X_D3DBLEND_ONE, dst = S.blend_enable ? S.dst_blend : X_D3DBLEND_ZERO;
    if (!S.blend_enable && mask == 0xF)
        return BLEND_OPAQUE;
    for (unsigned i = 1; i < g_blend_combos; ++i)
        if (g_blend_combo[i].src == src && g_blend_combo[i].dst == dst && g_blend_combo[i].mask == mask)
            return (int)i;
    if (g_blend_combos < BLEND_NOCOLOR) {
        g_blend_combo[g_blend_combos].src = (uint8_t)src; g_blend_combo[g_blend_combos].dst = (uint8_t)dst; g_blend_combo[g_blend_combos].mask = (uint8_t)mask;
        XV_LOG("blend variant %u: %u/%u mask %X\n", g_blend_combos, src, dst, mask);
        return (int)g_blend_combos++;
    }
    XV_ONCE(warned_blend, "blend %u/%u: variant table full; using src-alpha\n", S.src_blend, S.dst_blend);
    return BLEND_ALPHA;
}

static SceGxmBlendFactor d3d_blend_factor(unsigned f)
{
    switch (f) {
    case X_D3DBLEND_ZERO:         return SCE_GXM_BLEND_FACTOR_ZERO;
    case X_D3DBLEND_ONE:          return SCE_GXM_BLEND_FACTOR_ONE;
    case X_D3DBLEND_SRCCOLOR:     return SCE_GXM_BLEND_FACTOR_SRC_COLOR;
    case X_D3DBLEND_INVSRCCOLOR:  return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case X_D3DBLEND_SRCALPHA:     return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    case X_D3DBLEND_INVSRCALPHA:  return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case X_D3DBLEND_DESTALPHA:    return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
    case X_D3DBLEND_INVDESTALPHA: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case X_D3DBLEND_DESTCOLOR:    return SCE_GXM_BLEND_FACTOR_DST_COLOR;
    case X_D3DBLEND_INVDESTCOLOR: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case X_D3DBLEND_SRCALPHASAT:  return SCE_GXM_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    default:                      return SCE_GXM_BLEND_FACTOR_ONE;
    }
}

/* QUADLIST / POLYGON have no GXM equivalent: rewrite them as an indexed triangle list into this frame's
 * quad-index pool (GPU-visible scratch).  Halo draws its in-game UI text and HUD through DrawVertices(QUADLIST). */
static int rewrite_quads(uint32_t prim, uint32_t *count, const void **indices)
{
    const uint16_t *src = (const uint16_t *)*indices;
    uint32_t list = g_build_frame % XV_NUM_LISTS, n = *count, out = 0;
    uint16_t *dst = g_quad_indices + list * XV_QUAD_INDICES + g_quad_used[list];
    uint32_t room = XV_QUAD_INDICES - g_quad_used[list];
    if (prim == X_D3DPT_QUADLIST) {
        for (uint32_t q = 0; q + 4 <= n && out + 6 <= room; q += 4) {
            dst[out++] = src[q]; dst[out++] = src[q + 1]; dst[out++] = src[q + 2];
            dst[out++] = src[q]; dst[out++] = src[q + 2]; dst[out++] = src[q + 3];
        }
    } else {                                                  /* POLYGON: fan around the first vertex */
        for (uint32_t k = 1; k + 1 < n && out + 3 <= room; ++k) { dst[out++] = src[0]; dst[out++] = src[k]; dst[out++] = src[k + 1]; }
    }
    if (!out) return -1;
    g_quad_used[list] += out;
    *indices = dst; *count = out;
    return 0;
}

static int prim_to_gxm(uint32_t prim, uint32_t *count, const void **indices, uint32_t *out)
{
    switch (prim) {
    case X_D3DPT_TRIANGLELIST:  *out = SCE_GXM_PRIMITIVE_TRIANGLES;      return 0;
    case X_D3DPT_TRIANGLESTRIP: *out = SCE_GXM_PRIMITIVE_TRIANGLE_STRIP; return 0;
    case X_D3DPT_TRIANGLEFAN:   *out = SCE_GXM_PRIMITIVE_TRIANGLE_FAN;   return 0;
    case X_D3DPT_LINELIST:      *out = SCE_GXM_PRIMITIVE_LINES;          return 0;
    case X_D3DPT_POINTLIST:     *out = SCE_GXM_PRIMITIVE_POINTS;         return 0;
    case X_D3DPT_QUADLIST: case X_D3DPT_POLYGON:
        if (rewrite_quads(prim, count, indices) != 0) return -1;
        *out = SCE_GXM_PRIMITIVE_TRIANGLES; return 0;
    default:
        XV_ONCE(warned_prim, "primitive type %u not implemented (lineloop/linestrip/quadstrip)\n", prim);
        return -1;
    }
}

int xd3d_hist_active(void) __attribute__((weak));
static int trace_frame(void) { return xd3d_hist_active && xd3d_hist_active(); }
static const SceGxmTexture *cube_fallback(void);


unsigned xv_d3d_last_draws, xv_d3d_draw_acc, xv_d3d_bsp_acc;   /* draw counters for the frame-time log */
unsigned xv_dbg_count[16];                                        /* instrumented guest functions (portal walker) */
static void record_draw(uint32_t prim, uint32_t count, const void *indices, uint32_t base_vertex)
{
    xv_d3d_draw_acc++;
    if (recording_dropped()) return;
    int slot = handle_to_slot(S.vs_handle);
    if (slot < 0) {
        XV_ONCE(warned_novs, "draw with no valid vertex shader handle (0x%X) skipped\n", S.vs_handle);
        return;
    }
    cmd_t *c = new_cmd();
    if (!c)
        return;
    if (prim_to_gxm(prim, &count, &indices, &c->prim) != 0) {
        cur_list()->ncmds--;
        return;
    }
    vs_slot_t *v = &g_vs[slot];
    const xv_vs_desc_t *d = v->vs.desc;
    {   /* XV_SKIP_VS=halo_vs_40[,halo_vs_26]: drop every draw made with these vertex programs (pass isolation) */
        static const char *skip = NULL; static int sinit; if (!sinit) { sinit = 1; skip = getenv("XV_SKIP_VS"); }
        if (skip && d->gxp) { const char *b = strrchr(d->gxp, '/'); b = b ? b + 1 : d->gxp; size_t bl = strlen(b) - 4; const char *e = skip;
            while (*e) { const char *end = strchr(e, ','); size_t l = end ? (size_t)(end - e) : strlen(e); if (l == bl && !strncmp(e, b, l)) { cur_list()->ncmds--; return; } e = end ? end + 1 : e + l; } }
    }
    {   /* diagnostic: XV_SKIP_PS=<hash>[,<hash>..] drops every draw that uses one of those combiner programs */
        static const char *sp = NULL; static int si; if (!si) { si = 1; sp = getenv("XV_SKIP_PS"); }
        if (sp) { char hb[9]; snprintf(hb, sizeof hb, "%08X", S.ps_hash); const char *e = sp;
            while (*e) { const char *end = strchr(e, ','); size_t l = end ? (size_t)(end - e) : strlen(e); if (l == 8 && !strncmp(e, hb, 8)) { cur_list()->ncmds--; return; } e = end ? end + 1 : e + l; } }
    }
    {   /* which (vertex program, combiner program) pairs the game actually draws with: offline input */
        static struct { uint32_t vs, ps; } pairs[256]; static unsigned np; unsigned k;
        for (k = 0; k < np; ++k) if (pairs[k].vs == d->func_hash && pairs[k].ps == S.ps_hash) break;
        if (k == np && np < 256) { pairs[np].vs = d->func_hash; pairs[np].ps = S.ps_hash; np++;
            XV_LOG("[pspair] %08X %08X %s ntex %u%u%u%u blend %u/%u\n", d->func_hash, S.ps_hash, d->gxp, !!S.tex_guest[0], !!S.tex_guest[1], !!S.tex_guest[2], !!S.tex_guest[3], S.blend_enable ? S.src_blend : 0, S.blend_enable ? S.dst_blend : 0); }
    }

    c->kind = 0;
    c->vs = (uint16_t)slot;
    c->indices = indices;
    c->index_count = count;
    c->fs_kind = S.tex_guest[0] ? FS_TEXMOD : FS_COLOR;
    c->ps_entry = -1;
    for (unsigned i = 0; i < XV_PS_TABLE_COUNT; ++i)
        if (xv_ps_table[i].vs_fnv == d->func_hash && xv_ps_table[i].ps_hash == S.ps_hash) { c->ps_entry = (int16_t)i; break; }
    if (c->ps_entry < 0 && c->fs_kind == FS_TEXMOD) {
        /* no combiner program for this pair yet: texture x colour is black when the vertex program
         * writes no colour0 (most environment shaders) - use the plain texture instead */
        for (unsigned i = 0; i < XV_VS_OUTPUTS_COUNT; ++i)
            if (xv_vs_outputs[i].vs_fnv == d->func_hash) { if (!(xv_vs_outputs[i].outputs & 1)) c->fs_kind = FS_TEX0; break; }
        static unsigned n; if (n++ < 6) XV_LOG("[pspair] no program for vs %08X ps %08X: fallback %s\n", d->func_hash, S.ps_hash, c->fs_kind == FS_TEX0 ? "tex0" : "texmod");
    }
    {   /* debug: XV_FS_FORCE=texmod|tex0|lm|color bypasses the combiner programs for every textured draw */
        static int force = -2;
        if (force == -2) { const char *e = getenv("XV_FS_FORCE"); force = !e ? -1 : !strcmp(e, "tex0") ? FS_TEX0 : !strcmp(e, "lm") ? FS_LM : !strcmp(e, "color") ? FS_COLOR : FS_TEXMOD; }
        if (force >= 0 && S.tex_guest[0]) { c->fs_kind = (uint8_t)force; c->ps_entry = -1; }
    }
    memcpy(c->psc, S.psc, sizeof(c->psc));
    c->blend = (uint8_t)blend_mode();
    c->depth_func_idx = S.z_enable ? (uint8_t)((S.z_func >= 1 && S.z_func <= 8) ? S.z_func - 1 : 3) : 7;
    c->depth_write = S.z_enable && S.z_write;
    c->cull = (uint8_t)S.cull;
    { extern uint32_t xd3d_fog_color(void); c->fog_color = xd3d_fog_color(); }
    if (strstr(d->gxp, "halo_vs_16")) xv_d3d_bsp_acc++;
    memcpy(c->const_attr, S.const_attr, 16);

    /* streams: game pointers, offset by the base vertex, flushed for the GPU */
    uint32_t nverts = count;                                  /* vertices the GPU will fetch */
    if (indices && indices != g_seq_indices) {
        uint32_t mx = 0; const uint16_t *ix = (const uint16_t *)indices;
        for (uint32_t i = 0; i < count; ++i) if (ix[i] > mx) mx = ix[i];
        nverts = mx + 1;
    }
    for (unsigned s = 0; s < d->nstreams; ++s) {
        if (!S.stream_guest[s]) {
            XV_ONCE(warned_stream, "stream %u not set for %s\n", s, d->gxp);
            continue;
        }
        const X_D3DResource *vb = (const X_D3DResource *)xv_guest_ptr(S.stream_guest[s]);
        unsigned stride = S.stream_stride[s] ? S.stream_stride[s] : d->stride[s];
        /* Data is a PHYSICAL address: translate through the 0x80000000 alias.  As a bare low VA it hit
         * Halo's heap pages (heap VAs start at 0x00465000), so map-resident model vertices (vehicle parts,
         * dropped weapons: physical 0x005A5A00...) came back as zeros / live heap bytes. */
        const uint8_t *p = (const uint8_t *)xv_guest_ptr(0x80000000u | vb->Data) + base_vertex * stride;
        c->streams[s] = p;
        xv_gpu_flush(p, nverts * stride);
    }
    if (indices)
        xv_gpu_flush(indices, count * 2);

    {   /* XV_FOG_DUMP=1: first 3 draws per vertex program: the fog-plane constants c[12], c[13] */
        static int fdump = -1; if (fdump < 0) { const char *e = getenv("XV_FOG_DUMP"); fdump = e ? atoi(e) : 0; }
        static const void *fseen[96]; static unsigned fcnt[96], fn;
        if (fdump) {
            unsigned j; for (j = 0; j < fn; ++j) if (fseen[j] == d) break;
            if (j == fn && fn < 96) { fseen[fn] = d; fcnt[fn] = 0; fn++; }
            if (j < 96 && fcnt[j]++ < 3 && strstr(d->gxp, "halo_vs_06"))
                XV_LOG("[fog] psc0=(%.3f %.3f %.3f %.3f) psc1=(%.3f %.3f %.3f %.3f) psc8=(%.3f %.3f %.3f %.3f) psc9=(%.3f %.3f %.3f %.3f) fogcol %08X blend %u/%u\n",
                       S.psc[0][0], S.psc[0][1], S.psc[0][2], S.psc[0][3], S.psc[1][0], S.psc[1][1], S.psc[1][2], S.psc[1][3], S.psc[8][0], S.psc[8][1], S.psc[8][2], S.psc[8][3], S.psc[9][0], S.psc[9][1], S.psc[9][2], S.psc[9][3], c->fog_color, S.src_blend, S.dst_blend);
            if (j < 96 && fcnt[j] <= 3)
                XV_LOG("[fog] %s ps %08X c8=(%g %g %g %g) c9=(%g %g %g %g) c10=(%g %g %g %g) rows c0=(%g %g %g %g) c1=(%g %g %g %g) c2=(%g %g %g %g) c3=(%g %g %g %g)\n", d->gxp, S.ps_hash,
                       S.vsc[8][0], S.vsc[8][1], S.vsc[8][2], S.vsc[8][3], S.vsc[9][0], S.vsc[9][1], S.vsc[9][2], S.vsc[9][3], S.vsc[10][0], S.vsc[10][1], S.vsc[10][2], S.vsc[10][3],
                       S.vsc[0][0], S.vsc[0][1], S.vsc[0][2], S.vsc[0][3], S.vsc[1][0], S.vsc[1][1], S.vsc[1][2], S.vsc[1][3], S.vsc[2][0], S.vsc[2][1], S.vsc[2][2], S.vsc[2][3], S.vsc[3][0], S.vsc[3][1], S.vsc[3][2], S.vsc[3][3]);
        }
    }
    {   /* XV_SKIN_DUMP=1: for the first bone-indexed draws, log the node-index bytes (+28/+29), the weight
         * (+30) and the scale constant c[7] the program multiplies them by (vehicle wheels/weapons mangled) */
        static int dump = -1; if (dump < 0) { const char *e = getenv("XV_SKIN_DUMP"); dump = e ? atoi(e) : 0; }
        static unsigned shown;
        if (dump && c->streams[0] && shown < 160) {
            int skinned = 0;
            for (unsigned a = 0; a < d->nattrs; ++a) if (d->attrs[a].offset == 28 && d->attrs[a].format == SCE_GXM_ATTRIBUTE_FORMAT_U8N) skinned = 1;
            const uint8_t *vb0 = (const uint8_t *)c->streams[0];
            unsigned st = S.stream_stride[0] ? S.stream_stride[0] : d->stride[0], mx = 0, mx0 = 0; char buf[200]; int k = 0;
            if (skinned) for (unsigned i = 0; i < nverts; ++i) if (vb0[i * st + 28] > mx0) mx0 = vb0[i * st + 28];
            /* once per distinct (program, vertex count, node-set) signature so parts that appear later (the
             * Warthog when the player walks up to it) still get logged */
            uint32_t sig = 0;
            if (skinned) { sig = (uint32_t)(uintptr_t)d ^ (nverts * 2654435761u); for (unsigned i = 0; i < nverts; i += 7) sig = sig * 31u + vb0[i * st + 28]; }
            static uint32_t sigs[160]; int newsig = 0;
            if (skinned) { unsigned j; for (j = 0; j < shown; ++j) if (sigs[j] == sig) break; if (j == shown) { sigs[shown] = sig; newsig = 1; } }
            static unsigned big; int bigpart = skinned && nverts == 1372 && big < 40;
            if (bigpart) {                                          /* the Warthog: which vertices carry odd node bytes, and where the buffer lives */
                big++; unsigned odd = 0; char ob[160]; int ok = 0;
                for (unsigned i = 0; i < nverts; ++i) { uint8_t b = vb0[i * st + 28]; if (b % 3 || b > 120) { if (odd < 6) ok += snprintf(ob + ok, sizeof ob - ok, " v%u=%u/%u", i, b, vb0[i * st + 29]); odd++; } }
                if (big == 1) { FILE *f = fopen("ux0:data/xita/vb_1372.bin", "wb"); if (f) { fwrite(vb0, 1, nverts * st, f); fclose(f); } }
                if (big == 1) {                                         /* vertex-object table (index header +0x14) as fixed up by the loader */
                    uint32_t idx = 0x803A6000u, tbl = *(const uint32_t *)xv_guest_ptr(idx + 0x14), cnt = *(const uint32_t *)xv_guest_ptr(idx + 0x10);
                    XV_LOG("[skin] vobj table %08X x%u; this vb %08X = entry %d\n", tbl, cnt, S.stream_guest[0], (int)(S.stream_guest[0] - tbl) / 12);
                    for (unsigned i = 0; i < cnt && i < 359; i += (i < 8 || (i >= 246 && i <= 254)) ? 1 : 40) {
                        const uint32_t *e = (const uint32_t *)xv_guest_ptr(tbl + i * 12);
                        XV_LOG("[skin] vobj[%u] common %08X data %08X lock %08X\n", i, e[0], e[1], e[2]);
                    }
                }
                XV_LOG("[skin] BIG guest vb %08X (+%u) odd=%u:%s\n", S.stream_guest[0], (unsigned)((const uint8_t *)c->streams[0] - (const uint8_t *)xv_guest_ptr(S.stream_guest[0])), odd, ob);
            }
            if (skinned && newsig) {
                shown++;
                for (unsigned i = 0; i < nverts && i < 6; ++i) { const uint8_t *vv = vb0 + i * st; int16_t w; memcpy(&w, vv + 30, 2); k += snprintf(buf + k, sizeof buf - k, " [%u,%u w%d]", vv[28], vv[29], w); }
                for (unsigned i = 0; i < nverts; ++i) { const uint8_t *vv = vb0 + i * st; if (vv[28] > mx) mx = vv[28]; if (vv[29] > mx) mx = vv[29]; }
                XV_LOG("[skin] %s n=%u stride=%u maxnode=%u c7=(%g %g %g %g) c60=(%g %g %g %g) c63.x=%g nodes:%s\n", d->gxp, nverts, st, mx,
                       S.vsc[7][0], S.vsc[7][1], S.vsc[7][2], S.vsc[7][3], S.vsc[60][0], S.vsc[60][1], S.vsc[60][2], S.vsc[60][3], S.vsc[63][0], buf);
                /* distinct node0 bytes in this draw and the 3 matrix rows each selects (c[60+b..62+b]) */
                uint8_t seen[8]; unsigned ns = 0;
                for (unsigned i = 0; i < nverts && ns < 8; ++i) { uint8_t b = vb0[i * st + 28]; unsigned j; for (j = 0; j < ns; ++j) if (seen[j] == b) break; if (j == ns) seen[ns++] = b; }
                for (unsigned j = 0; j < ns; ++j) {
                    unsigned b = seen[j]; if (60 + b + 2 >= 192) continue;
                    XV_LOG("[skin]   node byte %u -> rows [%.3f %.3f %.3f %.2f] [%.3f %.3f %.3f %.2f] [%.3f %.3f %.3f %.2f]\n", b,
                           S.vsc[60 + b][0], S.vsc[60 + b][1], S.vsc[60 + b][2], S.vsc[60 + b][3],
                           S.vsc[61 + b][0], S.vsc[61 + b][1], S.vsc[61 + b][2], S.vsc[61 + b][3],
                           S.vsc[62 + b][0], S.vsc[62 + b][1], S.vsc[62 + b][2], S.vsc[62 + b][3]);
                }
            }
        }
    }

    /* constants: snapshot the window this program reads.  Consecutive draws with unchanged c[] share one
     * snapshot (Halo draws hundreds of BSP pieces per frame with full-window programs: 768 floats each) */
    cmdlist_t *l = cur_list();
    if (v->vs.p_c) {
        if (l->nconsts && l->last_gen == S.vsc_gen && l->last_base == d->c_base && l->last_n == d->c_count) {
            c->const_off = l->last_off; c->const_n = d->c_count;
        } else if (l->nconsts + d->c_count * 4 <= XV_CONST_POOL) {
            c->const_off = l->nconsts / 4;
            c->const_n = d->c_count;
            for (unsigned i = 0; i < d->c_count; ++i) {
                int r = d->c_base + (int)i + 96;
                const float *src = (r >= 0 && r < 192) ? S.vsc[r] : (const float[4]){ 0, 0, 0, 0 };
                memcpy(&l->consts[l->nconsts], src, 16);
                l->nconsts += 4;
            }
            l->last_gen = S.vsc_gen; l->last_off = c->const_off; l->last_n = d->c_count; l->last_base = d->c_base;
        } else {
            l->const_dropped++;               /* drawn with whatever the uniform buffer holds - visible garbage */
        }
    }

    /* textures */
    unsigned texok = 0;
    for (unsigned t = 0; t < 4; ++t) {
        if (!S.tex_guest[t])
            continue;
        const SceGxmTexture *tex = texture_for(t);
        if (tex) {
            c->tex[t] = *tex;
            c->ntex = (uint8_t)(t + 1);
            texok |= 1u << t;
        }
    }
    if (c->ps_entry >= 0 && xv_ps_table[c->ps_entry].cube_mask) {
        for (unsigned t = 0; t < 4; ++t) if (xv_ps_table[c->ps_entry].cube_mask & (1u << t)) {
            int have_cube = (texok & (1u << t)) && sceGxmTextureGetType(&c->tex[t]) == SCE_GXM_TEXTURE_CUBE;
            if (!have_cube) { const SceGxmTexture *fb = cube_fallback(); if (fb) { c->tex[t] = *fb; if (c->ntex < t + 1) c->ntex = (uint8_t)(t + 1); } }
        }
    }
    if (trace_frame()) {
        char tb[160]; int n = 0;
        for (unsigned t = 0; t < 4; ++t) {
            if (!S.tex_guest[t]) { n += snprintf(tb + n, sizeof tb - n, " -"); continue; }
            const uint32_t *hdr = (const uint32_t *)xv_guest_ptr(S.tex_guest[t]);
            uint32_t fw = hdr[3], sz = hdr[4];
            unsigned w = sz ? (sz & 0xFFF) + 1 : 1u << ((fw >> 20) & 0xF), h = sz ? ((sz >> 12) & 0xFFF) + 1 : 1u << ((fw >> 24) & 0xF);
            n += snprintf(tb + n, sizeof tb - n, " %02X:%ux%u%s", (fw >> 8) & 0xFF, w, h, (texok >> t) & 1 ? "" : "!");
            if ((texok >> t) & 1) n += snprintf(tb + n, sizeof tb - n, "[g%ux%u d%08X t%u]", (unsigned)sceGxmTextureGetWidth(&c->tex[t]), (unsigned)sceGxmTextureGetHeight(&c->tex[t]),
                                                 (unsigned)(uintptr_t)sceGxmTextureGetData(&c->tex[t]) & 0xFFFFFFFFu, (unsigned)sceGxmTextureGetType(&c->tex[t]) >> 29);   /* what GXM actually samples */
        }
        { extern char xd3d_last_stack[400]; XV_LOG("[hist]   stack:%s\n", xd3d_last_stack); }
        XV_LOG("[hist]   textures:%s psc0 %.2f %.2f %.2f %.2f psc1 %.2f %.2f %.2f %.2f\n", tb, c->psc[0][0], c->psc[0][1], c->psc[0][2], c->psc[0][3], c->psc[1][0], c->psc[1][1], c->psc[1][2], c->psc[1][3]);
        if (!S.z_enable && c->streams[0]) {                          /* depth-off draws (sky): where do the first vertices land in clip space? */
            unsigned st = S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]; char zb[400]; int k = 0;
            for (unsigned i = 0; i < 6 && k < 360; ++i) {
                const float *p = (const float *)((const uint8_t *)c->streams[0] + i * st); float v[4] = { p[0], p[1], p[2], 1.0f }, w[4];
                for (int r = 0; r < 3; ++r) w[r] = S.vsc[60 + r][0] * v[0] + S.vsc[60 + r][1] * v[1] + S.vsc[60 + r][2] * v[2] + S.vsc[60 + r][3];   /* node 0 */
                w[3] = 1.0f; float o[4];
                for (int r = 0; r < 4; ++r) o[r] = S.vsc[r][0] * w[0] + S.vsc[r][1] * w[1] + S.vsc[r][2] * w[2] + S.vsc[r][3] * w[3];
                k += snprintf(zb + k, sizeof zb - k, " [%.2f %.2f %.2f %.2f]", o[0] / o[3], o[1] / o[3], o[2] / o[3], o[3]);
            }
            XV_LOG("[hist]   zoff clip (x/w y/w z/w w):%s\n", zb);
        }
        for (unsigned t = 0; t < 4; ++t) {                            /* tiny textures (fog ramps): raw bytes */
            uint32_t cw = S.tex_guest[t]; if (!cw) continue;
            const uint32_t *hdr = (const uint32_t *)xv_guest_ptr(cw); uint32_t fmtw = hdr[3], data = hdr[1];
            unsigned fmt = (fmtw >> 8) & 0xFF, lw = (fmtw >> 20) & 0xF, lh = (fmtw >> 24) & 0xF, w = 1u << lw, h = 1u << lh;
            if (w * h > 256 && !(fmt == 0x0E || fmt == 0x0F || fmt == 0x0C || fmt == 0x19)) continue;   /* tiny textures, plus BC/A8 (sky) first bytes */
            const uint8_t *px = (const uint8_t *)xv_guest_ptr(0x80000000u | data); unsigned bpp = (fmt == 0x1A || fmt == 0x20 || fmt == 0x05) ? 2 : (fmt == 0x06 || fmt == 0x07) ? 4 : 1;
            char rb[600]; int k = 0; for (unsigned i = 0; i < ((w * h > 256) ? 64u : w * h * bpp) && i < 128 && k < 560; ++i) k += snprintf(rb + k, sizeof rb - k, "%02X", px[i]);
            XV_LOG("[hist]   tex%u fmt %02X %ux%u raw:%s\n", t, fmt, w, h, rb);
        }
    }
    if (trace_frame() && getenv("XV_HIST_CONSTS")) {      /* XV_HIST_CONSTS="12,13,14,24,26": D3D vertex constant registers to print per draw */
        char cb[512]; int n = 0; const char *e = getenv("XV_HIST_CONSTS");
        while (*e && n < 440) { int reg = atoi(e); const float *m = S.vsc[96 + (reg < -96 ? -96 : reg > 95 ? 95 : reg)];
            n += snprintf(cb + n, sizeof cb - n, " c[%d]=%.3f,%.3f,%.3f,%.3f", reg, m[0], m[1], m[2], m[3]);
            while (*e && *e != ',') e++; if (*e == ',') e++; }
        XV_LOG("[hist]   consts:%s\n", cb);
    }
    if (trace_frame() && getenv("XV_DUMP_VS") && strstr(d->gxp, getenv("XV_DUMP_VS")) && d->nstreams > 1 && c->streams[1]) {
        /* raw stream-1 bytes of the first vertices (lightmap uv / normal packing checks) */
        unsigned st = S.stream_stride[1] ? S.stream_stride[1] : d->stride[1]; char b[200]; int n = 0;
        for (unsigned k = 0; k < 4 && n < 180; ++k) { const uint8_t *p = (const uint8_t *)c->streams[1] + k * st; n += snprintf(b + n, sizeof b - n, " |"); for (unsigned j = 0; j < st && j < 16; ++j) n += snprintf(b + n, sizeof b - n, " %02X", p[j]); }
        XV_LOG("[hist]   stream1 stride %u (decl %u) guest %08X:%s\n", st, d->stride[1], S.stream_guest[1], b);
    }
    if (trace_frame() && getenv("XV_DUMP_VS") && strstr(d->gxp, getenv("XV_DUMP_VS")) && c->streams[0]) {
        unsigned st = S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]; char b[300]; int n = 0;
        for (unsigned k = 0; k < 3 && n < 280; ++k) { const uint8_t *p = (const uint8_t *)c->streams[0] + k * st; const float *f = (const float *)p;
            n += snprintf(b + n, sizeof b - n, " | pos %.2f %.2f %.2f uv@24 %.3f %.3f raw", f[0], f[1], f[2], f[6], f[7]);
            for (unsigned j = 12; j < st && j < 32; ++j) n += snprintf(b + n, sizeof b - n, " %02X", p[j]); }
        XV_LOG("[hist]   stream0 stride %u (decl %u):%s\n", st, d->stride[0], b);
    }
    if (trace_frame() && strstr(d->gxp, getenv("XV_DUMP_VS") ? getenv("XV_DUMP_VS") : "\001") && c->streams[0]) {
        /* first three vertices through the c[0..3] rows the microcode uses for oPos (dph) */
        for (unsigned k = 0; k < 3; ++k) {
            const uint16_t *ix = (const uint16_t *)indices; unsigned vi = ix ? ix[k] : k;
            const float *p = (const float *)((const uint8_t *)c->streams[0] + vi * (S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]));
            float o[4]; for (unsigned r = 0; r < 4; ++r) { const float *m = S.vsc[96 + d->c_base + r]; o[r] = p[0] * m[0] + p[1] * m[1] + p[2] * m[2] + m[3]; }
            XV_LOG("[hist]   v%u idx %u pos %.2f %.2f %.2f -> clip %.2f %.2f %.2f w %.2f (ndc %.2f %.2f z %.3f)\n", k, vi, p[0], p[1], p[2], o[0], o[1], o[2], o[3], o[0] / o[3], o[1] / o[3], o[2] / o[3]);
        }
        for (unsigned r = 0; r < 4; ++r) { const float *m = S.vsc[96 + d->c_base + r]; XV_LOG("[hist]   c[%d] %.3f %.3f %.3f %.3f\n", d->c_base + (int)r, m[0], m[1], m[2], m[3]); }
        { const uint16_t *ix = (const uint16_t *)indices; char b[200]; int n = 0;
          for (unsigned k = 0; ix && k < 24 && k < count && n < 180; ++k) n += snprintf(b + n, sizeof b - n, " %u", ix[k]);
          XV_LOG("[hist]   indices @%p (guest ib %08X base %u):%s\n", indices, S.indices_dbg, base_vertex, b); }
    }
    if (trace_frame())
        XV_LOG("[hist] cmd %u: draw %s ps %08X prim %u n %u base %u tex %08X/%08X/%08X/%08X ntex %u blend %u/%u%s z %u/%u cull %u c[%d..%d] c0 %.2f %.2f %.2f %.2f\n",
               l->ncmds - 1, d->gxp, S.ps_hash, prim, count, base_vertex, S.tex_guest[0], S.tex_guest[1], S.tex_guest[2], S.tex_guest[3], c->ntex, S.src_blend, S.dst_blend, S.blend_enable ? "" : "(off)",
               S.z_enable, S.z_write, S.cull, d->c_base, d->c_base + (int)d->c_count, S.vsc[96 + d->c_base][0], S.vsc[96 + d->c_base][1], S.vsc[96 + d->c_base][2], S.vsc[96 + d->c_base][3]);
}

void xv_d3d_DrawVertices(uint32_t prim, uint32_t start_vertex, uint32_t vertex_count)
{
    if (vertex_count > XV_SEQ_INDICES)
        vertex_count = XV_SEQ_INDICES;
    record_draw(prim, vertex_count, g_seq_indices, start_vertex);
}

void xv_d3d_DrawIndexedVertices(uint32_t prim, uint32_t vertex_count, uint32_t indices_guest)
{
    record_draw(prim, vertex_count, xv_guest_ptr(indices_guest), 0);
}
void xv_d3d_DrawIndexedVerticesBase(uint32_t prim, uint32_t index_count, uint32_t indices_guest, uint32_t base_vertex)
{
    S.indices_dbg = indices_guest;
    record_draw(prim, index_count, xv_guest_ptr(indices_guest), base_vertex);
}

/* Recomp-build helpers: the kernel's D3D object model (recomp/kernel/xd3d.c) owns the state and hands
 * it over per draw, so it needs handle lookup by microcode hash, bulk constants, and a frame boundary
 * that does not present (the game's own Present does). */
uint32_t xv_d3d_handle_for_hash(uint32_t fnv)
{
    for (unsigned s = 0; s < g_nvs; ++s)
        if (g_vs[s].vs.desc && g_vs[s].vs.desc->func_hash == fnv) return slot_to_handle(s);
    for (unsigned i = 0; i < g_table_count; ++i)
        if (g_table[i]->func_hash == fnv) {
            uint32_t h = xv_d3d_RegisterVertexShader(g_table[i]);
            XV_LOG("mesh program %s (fnv %08X) -> handle 0x%X\n", g_table[i]->gxp, fnv, h);
            return h;
        }
    return 0;
}
void xv_d3d_SetAllConstants(const float (*vsc)[4]) { if (memcmp(S.vsc, vsc, sizeof(S.vsc)) != 0) { memcpy(S.vsc, vsc, sizeof(S.vsc)); S.vsc_gen++; } }
void xv_d3d_SetPixelShader(uint32_t hash, const float (*psc)[4]) { S.ps_hash = hash; if (psc) memcpy(S.psc, psc, sizeof(S.psc)); }
uint32_t xv_d3d_EndFrame(void)
{
    cmdlist_t *l = cur_list();
    if (l->dropped) XV_LOG("frame %u: %u command(s) dropped (list full)\n", g_build_frame, l->dropped);
    if (l->const_dropped) XV_LOG("frame %u: %u draw(s) without constants (pool full: %u floats)\n", g_build_frame, l->const_dropped, l->nconsts);
    xv_d3d_last_draws = l->ncmds;
    uint32_t done = g_build_frame++;
    cmdlist_t *next = g_lists[g_build_frame % XV_NUM_LISTS];
    next->ncmds = 0; next->nconsts = 0; next->dropped = 0; next->const_dropped = 0; next->npasses = 0; next->cur_pass = 0; g_quad_used[g_build_frame % XV_NUM_LISTS] = 0;
    return done;
}

void xv_d3d_Clear(uint32_t flags, uint32_t color_argb, float z, uint32_t stencil)
{
    (void)stencil;
    if (recording_dropped()) return;
    if (trace_frame()) XV_LOG("[hist] cmd %u: Clear flags %X color %08X z %.3f\n", cur_list()->ncmds, flags, color_argb, z);
    cmd_t *c = new_cmd();
    if (!c)
        return;
    c->kind = 1;
    c->clear_flags = (uint8_t)((flags & X_D3DCLEAR_TARGET ? 1 : 0) | (flags & X_D3DCLEAR_ZBUFFER ? 2 : 0));
    c->clear_color = color_argb;
    c->clear_z = z;
}

void xv_d3d_Swap(void)
{
    cmdlist_t *l = cur_list();
    if (l->dropped)
        XV_LOG("frame %u: %u command(s) dropped (list full)\n", g_build_frame, l->dropped);
    g_build_frame++;
    /* reset the list the NEXT frame will use (the pump is done with it: at most one
       frame is in flight beyond the one just submitted) */
    cmdlist_t *next = g_lists[g_build_frame % XV_NUM_LISTS];
    next->ncmds = 0; next->nconsts = 0; next->dropped = 0;
    xv_present();
}

/* ----------------------------------------------------------------------------------
 *  Replay (pump thread)
 * -------------------------------------------------------------------------------- */
static const SceGxmBlendInfo *blend_info_for(unsigned blend, SceGxmBlendInfo *bi)
{
    if (blend == BLEND_OPAQUE) return NULL;
    memset(bi, 0, sizeof(*bi));
    if (blend == BLEND_NOCOLOR) {                     /* Z-only clear: depth writes, no colour */
        bi->colorMask = SCE_GXM_COLOR_MASK_NONE;
        bi->colorFunc = SCE_GXM_BLEND_FUNC_NONE; bi->alphaFunc = SCE_GXM_BLEND_FUNC_NONE;
        bi->colorSrc = SCE_GXM_BLEND_FACTOR_ONE; bi->colorDst = SCE_GXM_BLEND_FACTOR_ZERO;
        bi->alphaSrc = SCE_GXM_BLEND_FACTOR_ONE; bi->alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
        return bi;
    }
    { unsigned m = g_blend_combo[blend].mask; bi->colorMask = (m & 1 ? SCE_GXM_COLOR_MASK_R : 0) | (m & 2 ? SCE_GXM_COLOR_MASK_G : 0) | (m & 4 ? SCE_GXM_COLOR_MASK_B : 0) | (m & 8 ? SCE_GXM_COLOR_MASK_A : 0); }
    bi->colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    bi->alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    SceGxmBlendFactor sf = d3d_blend_factor(g_blend_combo[blend].src), df = d3d_blend_factor(g_blend_combo[blend].dst);
    bi->colorSrc = sf; bi->colorDst = df;
    /* colour-only factors have no alpha meaning: keep the framebuffer alpha as-is for those */
    bi->alphaSrc = (sf == SCE_GXM_BLEND_FACTOR_DST_COLOR || sf == SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR) ? SCE_GXM_BLEND_FACTOR_DST_ALPHA : sf;
    bi->alphaDst = (df == SCE_GXM_BLEND_FACTOR_SRC_COLOR || df == SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR) ? SCE_GXM_BLEND_FACTOR_SRC_ALPHA : df;
    return bi;
}

static SceGxmFragmentProgram *fragment_for(vs_slot_t *v, unsigned kind, unsigned blend, const SceGxmProgramParameter **p_tex0)
{
    if (!v->fs_loaded[kind][blend]) {
        SceGxmBlendInfo bi; const SceGxmBlendInfo *pbi = blend_info_for(blend, &bi);
        v->fs_loaded[kind][blend] = 1;                 /* cache the attempt either way: a failure must not be retried */
        if (xv_fshader_load(&v->fs[kind][blend], FS_GXP[kind], &v->vs, pbi) != 0) {
            static unsigned n; if (n++ < 12) XV_LOG("fragment %s does not link against %s (kind %u blend %u) - draws skipped\n", FS_GXP[kind], v->vs.desc ? v->vs.desc->gxp : "?", kind, blend);
            /* fs.fprog is NULL (xv_fshader_load memsets it); leave it cached NULL so the draw is skipped, not retried */
        }
    }
    (void)p_tex0;
    return v->fs[kind][blend].fprog;
}

/* A cube sampler must see a cube texture control word: when the game has a 2D texture (or nothing) on a
 * stage the combiner reads as a cube map, bind this neutral grey 8x8x6 cube instead of risking the GPU. */
static SceGxmTexture g_cube_fallback; static int g_cube_fallback_ok = -1;
/* 8x8 mid-grey 2D texture for stages a program samples but the game left unset or bound with the wrong
 * kind (a cube map on a sampler2D): binding a zeroed or mismatched SceGxmTexture faults the real GPU */
static SceGxmTexture g_tex2d_fallback; static int g_tex2d_fallback_ok = -1;
static const SceGxmTexture *tex2d_fallback(void)
{
    if (g_tex2d_fallback_ok < 0) {
        SceUID uid = sceKernelAllocMemBlock("xv_tex_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4096, NULL);
        void *mem = NULL; g_tex2d_fallback_ok = 0;
        if (uid >= 0) {
            sceKernelGetMemBlockBase(uid, &mem);
            if (sceGxmMapMemory(mem, 4096, SCE_GXM_MEMORY_ATTRIB_READ) == SCE_OK) {
                uint32_t *p = mem; for (int i = 0; i < 8 * 8; ++i) p[i] = 0xFF808080u;
                g_tex2d_fallback_ok = sceGxmTextureInitLinear(&g_tex2d_fallback, mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 8, 8, 1) == SCE_OK;
            }
        }
    }
    return g_tex2d_fallback_ok ? &g_tex2d_fallback : NULL;
}
static const SceGxmTexture *cube_fallback(void)
{
    if (g_cube_fallback_ok < 0) {
        /* GXM cube layout: every face is laid out with room for its whole mip chain and faces are 2 KB aligned
         * (32-bit, >= 16x16).  The old 8x8 cube packed six 256-byte faces into 4 KB (and overran it): faces 1..5
         * read garbage, and a combiner that samples this stand-in with a 2-D coordinate hits exactly those
         * faces - the a10 cryo-bay floor's base pass multiplied its texture by that junk (black tiles). */
        SceUID uid = sceKernelAllocMemBlock("xv_cube_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 16384, NULL);
        void *mem = NULL; g_cube_fallback_ok = 0;
        if (uid >= 0) {
            sceKernelGetMemBlockBase(uid, &mem);
            if (sceGxmMapMemory(mem, 16384, SCE_GXM_MEMORY_ATTRIB_READ) == SCE_OK) {
                uint32_t *p = mem; for (int i = 0; i < 16384 / 4; ++i) p[i] = 0xFF808080u;   /* every byte of every face (and mip slot) is mid grey */
                g_cube_fallback_ok = sceGxmTextureInitCube(&g_cube_fallback, mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 16, 16, 1) == SCE_OK;
            }
        }
    }
    return g_cube_fallback_ok ? &g_cube_fallback : NULL;
}

/* combiner program from the table; NULL when it cannot be linked (caller falls back to fragment_for) */
static xv_fshader_t *fragment_for_ps(vs_slot_t *v, int entry, unsigned blend)
{
    for (unsigned i = 0; i < v->nps; ++i)
        if (v->ps[i].entry == entry && v->ps[i].blend == blend) return v->ps[i].failed ? NULL : &v->ps[i].fs;
    if (v->nps >= XV_PS_PER_VS) return NULL;
    ps_link_t *l = &v->ps[v->nps++];
    l->entry = (int16_t)entry; l->blend = (uint8_t)blend; l->failed = 0;
    SceGxmBlendInfo bi; const SceGxmBlendInfo *pbi = blend_info_for(blend, &bi);
    if (xv_fshader_load(&l->fs, xv_ps_table[entry].gxp, &v->vs, pbi) != 0) {
        static unsigned n; if (n++ < 12) XV_LOG("combiner %s does not link against %s (blend %u) - using heuristic fragment\n", xv_ps_table[entry].gxp, v->vs.desc ? v->vs.desc->gxp : "?", blend);
        l->failed = 1; return NULL;
    }
    return &l->fs;
}

static void render_pass(SceGxmContext *ctx, cmdlist_t *l, unsigned pass, unsigned *clear_slot_io, uint32_t frame)
{
    unsigned clear_slot = *clear_slot_io;
    for (unsigned i = 0; i < l->ncmds; ++i) {
        cmd_t *c = &l->cmds[i];
        if (c->pass != pass) continue;
        if (c->kind == 1) {
            int slot = handle_to_slot(g_clear_vs);
            if (slot < 0 || clear_slot >= XV_CLEAR_SLOTS)
                continue;
            vs_slot_t *v = &g_vs[slot];
            SceGxmFragmentProgram *fp = fragment_for(v, FS_COLOR, (c->clear_flags & 1) ? BLEND_OPAQUE : BLEND_NOCOLOR, NULL);
            if (!fp)
                continue;
            /* quad in this frame's slot: xyz + D3DCOLOR (B,G,R,A bytes) */
            struct { float x, y, z; uint8_t b, g, r, a; } *q =
                (void *)(g_clear_quads + ((frame % XV_NUM_LISTS) * XV_CLEAR_SLOTS + clear_slot) * 4 * 16);
            uint32_t col = c->clear_color;
            uint8_t b = col & 0xFF, g = (col >> 8) & 0xFF, r = (col >> 16) & 0xFF, a = col >> 24;
            float z = (c->clear_flags & 2) ? c->clear_z : 1.0f;
            q[0] = (typeof(q[0])){ -1, -1, z, b, g, r, a }; q[1] = (typeof(q[0])){ 1, -1, z, b, g, r, a };
            q[2] = (typeof(q[0])){ 1, 1, z, b, g, r, a };   q[3] = (typeof(q[0])){ -1, 1, z, b, g, r, a };
            clear_slot++;
            sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
            sceGxmSetFrontDepthWriteEnable(ctx, (c->clear_flags & 2) ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
            sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
            sceGxmSetVertexProgram(ctx, v->vs.vprog);
            sceGxmSetFragmentProgram(ctx, fp);
            sceGxmSetVertexStream(ctx, 0, q);
            sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, SCE_GXM_INDEX_FORMAT_U16, g_seq_indices, 4);
            continue;
        }

        vs_slot_t *v = &g_vs[c->vs];
        xv_fshader_t *fs = c->ps_entry >= 0 ? fragment_for_ps(v, c->ps_entry, c->blend) : NULL;
        SceGxmFragmentProgram *fp = fs ? fs->fprog : fragment_for(v, c->fs_kind, c->blend, NULL);
        if (!fp)
            continue;
        if (!fs) fs = &v->fs[c->fs_kind][c->blend];
        sceGxmSetFrontDepthFunc(ctx, DEPTH_FUNCS[c->depth_func_idx]);
        sceGxmSetFrontDepthWriteEnable(ctx, c->depth_write ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
        sceGxmSetCullMode(ctx, c->cull == X_D3DCULL_NONE ? SCE_GXM_CULL_NONE
                               : c->cull == X_D3DCULL_CW ? SCE_GXM_CULL_CW : SCE_GXM_CULL_CCW);
        sceGxmSetVertexProgram(ctx, v->vs.vprog);
        sceGxmSetFragmentProgram(ctx, fp);

        if (c->const_n && v->vs.p_c) {
            void *ub;
            if (sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub) == 0)
                sceGxmSetUniformDataF(ub, v->vs.p_c, 0, c->const_n * 4, &l->consts[c->const_off * 4]);
        }
        xv_shader_set_const_attr(c->const_attr);
        xv_vshader_set_streams(ctx, &v->vs, c->streams);
        if (fs->p_psc || fs->p_fogcolor) {
            void *fub;
            if (sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fub) == 0) {
                uint32_t fc = c->fog_color;                                            /* D3DCOLOR ARGB, captured at record time */
                float fog[4] = { ((fc >> 16) & 0xFF) / 255.0f, ((fc >> 8) & 0xFF) / 255.0f, (fc & 0xFF) / 255.0f, ((fc >> 24) & 0xFF) / 255.0f };
                if (fs->p_psc) sceGxmSetUniformDataF(fub, fs->p_psc, 0, 18 * 4, &c->psc[0][0]);
                if (fs->p_fogcolor) sceGxmSetUniformDataF(fub, fs->p_fogcolor, 0, 4, fog);
            }
        }
        for (unsigned t = 0; t < 4; ++t) {
            if (fs->tex_index[t] < 0) continue;                       /* program does not sample this stage */
            const SceGxmTexture *tx = t < c->ntex ? &c->tex[t] : NULL;
            int want_cube = c->ps_entry >= 0 && (xv_ps_table[c->ps_entry].cube_mask & (1u << t));
            int have = tx && sceGxmTextureGetData(tx) != NULL;
            int is_cube = have && sceGxmTextureGetType(tx) == SCE_GXM_TEXTURE_CUBE;
            static SceGxmTexture face0[4];
            if (have && is_cube && !want_cube) {
                /* the game bound a cube map where the combiner samples 2D (NV2A PROJECT2D on a cube reads it
                 * as a 2D texture): present face +X as a 2D texture of the same size/format */
                if (sceGxmTextureInitSwizzled(&face0[t], sceGxmTextureGetData(tx), sceGxmTextureGetFormat(tx), sceGxmTextureGetWidth(tx), sceGxmTextureGetHeight(tx), 1) == SCE_OK) {
                    sceGxmTextureSetMinFilter(&face0[t], SCE_GXM_TEXTURE_FILTER_LINEAR); sceGxmTextureSetMagFilter(&face0[t], SCE_GXM_TEXTURE_FILTER_LINEAR);
                    tx = &face0[t]; have = 1; is_cube = 0;
                }
            }
            if (!have || (want_cube != is_cube)) {                    /* unset stage, or a 2D texture on a samplerCUBE */
                const SceGxmTexture *fb = want_cube ? cube_fallback() : tex2d_fallback();
                static unsigned n; if (n++ < 12) XV_LOG("draw: stage %u %s -> fallback %s\n", t, !have ? "unset" : "kind mismatch", want_cube ? "cube" : "2d");
                if (!fb) continue;
                tx = fb;
            }
            sceGxmSetFragmentTexture(ctx, (unsigned)fs->tex_index[t], tx);
        }
        sceGxmDraw(ctx, (SceGxmPrimitiveType)c->prim, SCE_GXM_INDEX_FORMAT_U16, c->indices, c->index_count);
    }
    *clear_slot_io = clear_slot;
}

void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    unsigned clear_slot = g_clear_slot_used;
    render_pass(ctx, l, 0, &clear_slot, frame);
    g_clear_slot_used = 0;
}

/* Offscreen passes: their own scenes, BEFORE the caller opens the main scene.  Colour surface = the guest
 * texture memory (linear, so the alias texture is a plain linear texture); depth = one shared scratch
 * buffer.  Render targets are cached per (w,h). */
static struct { unsigned w, h; SceGxmRenderTarget *rt; } g_pass_rts[8]; static unsigned g_pass_rt_n;
static SceUID g_pass_depth_uid; static void *g_pass_depth; static unsigned g_pass_depth_w;
static SceGxmRenderTarget *pass_rt(unsigned w, unsigned h)
{
    for (unsigned i = 0; i < g_pass_rt_n; ++i) if (g_pass_rts[i].w == w && g_pass_rts[i].h == h) return g_pass_rts[i].rt;
    if (g_pass_rt_n >= 8) return NULL;
    SceGxmRenderTargetParams rp; memset(&rp, 0, sizeof rp);
    rp.width = (uint16_t)w; rp.height = (uint16_t)h; rp.scenesPerFrame = 1; rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE; rp.driverMemBlock = -1;
    SceGxmRenderTarget *rt = NULL;
    if (sceGxmCreateRenderTarget(&rp, &rt) != SCE_OK) { XV_LOG("pass render target %ux%u failed\n", w, h); return NULL; }
    g_pass_rts[g_pass_rt_n].w = w; g_pass_rts[g_pass_rt_n].h = h; g_pass_rts[g_pass_rt_n].rt = rt; g_pass_rt_n++;
    return rt;
}
void xv_d3d_render_offscreen(SceGxmContext *ctx, uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    if (!l->npasses) return;
    if (!g_pass_depth) {                                        /* 1024x1024 max: 4 MB S8D24, uncached */
        g_pass_depth_w = 1024;
        g_pass_depth_uid = sceKernelAllocMemBlock("xv_pass_depth", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 1024 * 1024 * 4, NULL);
        if (g_pass_depth_uid < 0) return;
        sceKernelGetMemBlockBase(g_pass_depth_uid, &g_pass_depth);
        sceGxmMapMemory(g_pass_depth, 1024 * 1024 * 4, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
    }
    unsigned clear_slot = 0;
    for (unsigned pi = 0; pi < l->npasses; ++pi) {
        rt_pass_t *p = &l->passes[pi];
        SceGxmRenderTarget *rt = pass_rt(p->w, p->h);
        if (!rt) continue;
        SceGxmColorSurface cs; SceGxmDepthStencilSurface ds;
        if (sceGxmColorSurfaceInit(&cs, rt_color_fmt(p->fmt), SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                                   SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, p->w, p->h, p->w, xv_guest_ptr(0x80000000u | p->data)) != SCE_OK) continue;
        unsigned dstride = (p->w + 31) & ~31u;
        if (sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR, dstride, g_pass_depth, NULL) != SCE_OK) continue;
        if (sceGxmBeginScene(ctx, 0, rt, NULL, NULL, NULL, &cs, &ds) != SCE_OK) { static unsigned n; if (n++ < 4) XV_LOG("pass BeginScene failed\n"); continue; }
        sceGxmSetViewport(ctx, p->w * 0.5f, p->w * 0.5f, p->h * 0.5f, -(float)p->h * 0.5f, 0.5f, 0.5f);
        render_pass(ctx, l, pi + 1, &clear_slot, frame);
        sceGxmEndScene(ctx, NULL, NULL);
    }
    g_clear_slot_used = clear_slot;                             /* the main pass continues in the remaining clear-quad slots */
}

/* The clear quad's vertex shader is a runtime-owned program registered by main.c. */
void xv_d3d_set_clear_shader(uint32_t handle) { g_clear_vs = handle; }
