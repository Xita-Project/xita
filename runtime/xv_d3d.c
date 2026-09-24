#if XV_POSE_PIPELINE
#include "../recomp/kernel/xk_pose_pipeline.h"
#endif
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
#include "../recomp/kernel/xd3d.h"
#include "xv_draw_profile.h"
#include "xv_constant_window.h"
#include "xv_draw_state.h"
#include "xv_texture_state.h"
#include "xv_stencil_gxm.h"
#include "xv_render_profile.h"
#include "xv_render_target.h"
#include "xv_index_copy.h"
#include "xv_index_cache.h"
#include "xv_bytes_equal.h"
#include "xv_visibility.h"
#include "xv_frame_slots.h"
#include "xv_scene_census.h"
#include "xv_vertex_upload.h"
#include "xv_vertex_prepare.h"
#include "xv_vertex_capture.h"
#include "xv_gpu_upload.h"
#include "xv_texture_alpha.h"
#include "xv_depth_prepare.h"
#include "xv_record_opt.h"

#include "xv_log.h"
#define XV_LOG(...)     xv_logf("[xv/d3d] " __VA_ARGS__)
#define XV_ONCE(flag, ...) do { static int flag; if (!flag) { flag = 1; XV_LOG(__VA_ARGS__); } } while (0)

#define XV_MAX_VS        96          /* vertex shader handles                          */
#define XV_MAX_CMDS      2048        /* draws + clears per frame                       */
#define XV_CONST_POOL    (256 * 1024) /* floats per list: c[] snapshots (1 MB; full-window programs = 768 floats each) */
#define XV_TEX_CACHE     512         /* texture control words                          */
#define XV_LEGACY_CLEAR_SLOTS 16     /* preserve the no-RTT replay limit */
#define XV_CLEAR_SLOTS   64          /* clear quads per frame                          */
#define XV_SEQ_INDICES   65536       /* sequential u16 indices for DrawVertices        */
#define XV_QUAD_INDICES  49152       /* per list: u16 indices rewritten from QUADLIST/POLYGON draws */
#define XV_FRAME_INDICES (384 * 1024) /* per list: the a10 intro requests over 277K indices */
#define XV_NUM_LISTS     XV_FRAME_SLOTS
#define XV_IM_VERTICES  1024 /* shared immediate/attribute storage: a10 flares exceed 64 KiB */
#define XV_IM_STRIDE    (16 * 4 * sizeof(float))
#define XV_IM_BYTES     (XV_IM_VERTICES * XV_IM_STRIDE)

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
static struct { uint8_t src, dst, mask, op; } g_blend_combo[BLEND_MODES] = {   /* mask: D3DRS_COLORWRITEENABLE bits (R1 G2 B4 A8); op 0 = add */
    { X_D3DBLEND_ONE, X_D3DBLEND_ZERO, 0xF, 0 }, { X_D3DBLEND_SRCALPHA, X_D3DBLEND_INVSRCALPHA, 0xF, 0 }, { X_D3DBLEND_ONE, X_D3DBLEND_ONE, 0xF, 0 } };
static unsigned g_blend_combos = 3;
static const char *const FS_GXP[FS_KINDS] = { "app0:shaders/xv_color.frag.gxp", "app0:shaders/xv_texmod.frag.gxp", "app0:shaders/xv_tex0.frag.gxp", "app0:shaders/xv_lm.frag.gxp" };

/* Recompiled register-combiner programs (Stage 3b, tools/ps_pipeline.py): fragment programs keyed by
 * the combiner hash the game submitted.  Linked lazily per (vertex program, combiner, blend). */
#include "xv_ps_table.h"
#define XV_PS_LINKS (XV_MAX_VS * 12) /* share the former per-VS capacity */
#define XV_PS_BUCKETS 2048
typedef struct { uint16_t vs, next; int16_t entry; uint8_t blend, failed, alpha_mode, replace_blend; xv_fshader_t fs; } ps_link_t;
static ps_link_t g_ps_links[XV_PS_LINKS];
static uint16_t g_ps_buckets[XV_PS_BUCKETS]; /* slot + 1; zero ends a chain */
static unsigned g_ps_count;
static int replace_blend_override=-1;
void xv_d3d_blend_replace_override(int enabled)
{ __atomic_store_n(&replace_blend_override,enabled<0?-1:!!enabled,__ATOMIC_RELEASE); }
static int replace_blend_enabled(void)
{
    static int configured=-1;
    if(configured<0) { const char *e=getenv("XV_BLEND_REPLACE");configured=e && atoi(e)!=0; }
    int value=__atomic_load_n(&replace_blend_override,__ATOMIC_ACQUIRE);
    return value<0?configured:value; /* opt in until measured on hardware */
}
static int replace_blend_eligible(unsigned blend)
{
    /* UCHAR4 output and normalized color surfaces: adding destination*0 to
     * source*1 replaces exactly the selected channels. Retain the mask, which
     * often preserves destination alpha for a later pass. No other equation
     * or factor pair is changed, including subtraction, min/max and blending. */
    return blend>BLEND_OPAQUE && blend<BLEND_NOCOLOR &&
        g_blend_combo[blend].src==X_D3DBLEND_ONE &&
        g_blend_combo[blend].dst==X_D3DBLEND_ZERO && !g_blend_combo[blend].op;
}
/* Guest-owned preparation metadata; it never reads the pump's link cache. */
static uint8_t g_ps_texture_masks[XV_PS_TABLE_COUNT]; /* bit 4 marks initialized */
static unsigned texture_stages_prepared, texture_stages_skipped;
static unsigned opaque_candidates, opaque_proven;
#ifndef XV_DEPTH_PREPARE_DEFAULT
#define XV_DEPTH_PREPARE_DEFAULT 0
#endif
#if XV_DEPTH_PREPARE_DEFAULT != 0 && XV_DEPTH_PREPARE_DEFAULT != 1
#error XV_DEPTH_PREPARE_DEFAULT must be 0 or 1
#endif
static xv_depth_proofs g_depth_proofs;
static unsigned depth_prepare_hits;
static int depth_prepare_override = -1;
void xv_depth_prepare_override(int enabled)
{ __atomic_store_n(&depth_prepare_override, enabled < 0 ? -1 : !!enabled, __ATOMIC_RELEASE); }
int xv_depth_prepare_available(void)
{
    const char *override = getenv("XV_SHADER_OVERRIDE");
    const char *depth = getenv("XV_DEPTH_ONLY_SHADER");
    const char *alpha = getenv("XV_ALPHA_SPECIALIZE");
    return (!override || atoi(override) == 0) && (!depth || atoi(depth) != 0) &&
        (!alpha || atoi(alpha) != 0);
}
static int depth_prepare_enabled(void)
{
    /* Recorder-owned configuration. Development shader files can change after
     * a proof was published, so overrides always retain ordinary preparation. */
    static int configured = -1, compatible;
    if (configured < 0) {
        const char *e = getenv("XV_DEPTH_PREPARE");
        configured = e ? atoi(e) != 0 : XV_DEPTH_PREPARE_DEFAULT;
        compatible = xv_depth_prepare_available();
    }
    int override = __atomic_load_n(&depth_prepare_override, __ATOMIC_ACQUIRE);
    return compatible && (override < 0 ? configured : override);
}
static int opaque_material_override = -1;
void xv_opaque_material_override(int enabled)
{ opaque_material_override = enabled < 0 ? -1 : !!enabled; }
static int cutout_override = -1;
void xv_cutout_override(int enabled) { cutout_override = enabled < 0 ? -1 : !!enabled; }
static int cutout_enabled(void)
{
    static int enabled = -1, compatible;
    if (enabled < 0) {
        const char *e = getenv("XV_CUTOUT_TEST"); enabled = !e || atoi(e) != 0;
        e = getenv("XV_SHADER_OVERRIDE"); compatible = !e || atoi(e) == 0;
        e = getenv("XV_NO_ATEST"); compatible &= !e || atoi(e) == 0;
        e = getenv("XV_ALPHA_SPECIALIZE"); compatible &= !e || atoi(e) != 0;
    }
    return compatible && (cutout_override < 0 ? enabled : cutout_override);
}
static int unused_textures_override = -1;
void xv_unused_textures_override(int enabled)
{ unused_textures_override = enabled < 0 ? -1 : !!enabled; }

static void ps_links_shutdown(void)
{
    /* Shutdown is already drained; no retained command may outlive its shader. */
    memset(&g_depth_proofs, 0, sizeof g_depth_proofs);
    for (unsigned i = 0; i < g_ps_count; i++) xv_fshader_unload(&g_ps_links[i].fs);
    memset(g_ps_links, 0, sizeof g_ps_links);
    memset(g_ps_buckets, 0, sizeof g_ps_buckets);
    g_ps_count = 0;
}

typedef struct {
    xv_vshader_t  vs;
    int           loaded;
    xv_fshader_t  fs[FS_KINDS][BLEND_MODES];
    int           fs_loaded[FS_KINDS][BLEND_MODES];
} vs_slot_t;

/* ----------------------------------------------------------------------------------
 *  Command list
 * -------------------------------------------------------------------------------- */
typedef struct {
    uint8_t   kind;                 /* 0 draw, 1 clear, 2 failed captured draw, 3 occlusion proxy (XV_OCCL) */
    uint8_t   fs_kind, blend;
    uint32_t  prim;                 /* SceGxmPrimitiveType (bitfield values, not small ints) */
    uint8_t   depth_func_idx, depth_write, cull;
    uint8_t   ntex;
    uint16_t  vs;                   /* slot index                                     */
    uint16_t  visibility;           /* frame query index + 1, zero disables testing */
    uint32_t  index_count;
    const void *streams[XV_MAX_STREAMS];
    const void *indices;
    uint32_t geometry_bytes[XV_MAX_STREAMS + 1], geometry_hash[XV_MAX_STREAMS + 1]; /* trace frames only */
    uint32_t  const_off, const_n;   /* float4 units into the pool                     */
    const void *constant_stream;    /* immutable, frame-owned persistent attributes */
    uint32_t  fog_color;            /* D3DCOLOR at record time (Halo toggles it within a frame) */
    uint32_t  atest;                /* alpha test at record time: ref | func<<8 | enable<<16 (xd3d_alpha_test) */
    SceGxmTexture tex[4];
    float     texscale[4][4];
    uint32_t  loading_border_color;
    uint8_t   loading_border_axes;
    int16_t   ps_entry;             /* xv_ps_table index, or -1 (heuristic fragment)  */
    uint8_t   pass;                 /* 0 = back buffer, n = offscreen pass n           */
    uint8_t   previous_frame;       /* stages sampling the last completed backbuffer */
    uint8_t   opaque_alpha;         /* pinned tex0 upload proves the captured test always passes */
#if XV_PACKED_VERTEX_LAYOUT
    uint8_t   packed_vertex;        /* captured immutable representation; never consult live stream state */
#endif
    uint8_t   depth_prepared;       /* published proof selects the retained constant fragment */
    float     psc[18][4];           /* combiner constants: per-stage c0[8], c1[8], final c0, c1 */
    uint32_t  clear_color;          /* clear only                                     */
    float     clear_z;
    uint8_t   clear_flags, clear_stencil;
    xv_stencil stencil;
    uint16_t  occl;                 /* XV_OCCL object slot (1-based) of the draw or proxy, 0 = untracked */
} cmd_t;

/* Alpha-disabled variants only. Texture-kind-dependent variants may change
 * entry during recording, so they cannot use this proof before texture capture. */
static uint32_t depth_prepare_key(const cmd_t *c)
{
    if (c->vs >= XV_MAX_VS || c->ps_entry < 0 || (unsigned)c->ps_entry >= XV_PS_TABLE_COUNT ||
        c->blend >= BLEND_MODES || g_blend_combo[c->blend].mask ||
        xv_ps_table[c->ps_entry].cube_modes ||
        ((c->atest & (1u << 16)) && ((c->atest >> 8) & 7u) != 7u)) return 0;
    return xv_depth_proof_key(c->vs, c->ps_entry, c->blend);
}

/* Commands retain their target identity; replay visits contiguous ranges in order. */
#define XV_RT_SLOTS 8
#define XV_RT_BUDGET (16u * 1024 * 1024)
typedef struct {
    cmd_t     cmds[XV_MAX_CMDS];
    unsigned  ncmds;
    float     consts[XV_CONST_POOL];
    unsigned  nconsts;              /* floats used                                    */
    unsigned  last_gen, last_off, last_n; int last_base;   /* previous snapshot: reused while c[] is unchanged */
    unsigned  const_dropped;
    unsigned  dropped, drop_commands, drop_indices, drop_attributes, drop_immediate;
    struct { unsigned before, frame, batch, target; } ui[1024];
    unsigned nui, ui_frame;
    unsigned  cur_pass;    /* 0 = backbuffer, slot+1 = RTT, 0xff = discard */
    struct { uint16_t result_slot; uint32_t serial, guest_area, render_area; } visibility[XV_VISIBILITY_PER_FRAME];
    unsigned nvisibility, active_visibility;
    int visibility_gpu_ready;
    unsigned visibility_back_area;
    unsigned visibility_draw_slot; /* pump-owned cached GXM state */
    uint8_t census;                /* XV_FRAG_CENSUS frame: per-command counters armed */
    /* XV_OCCL: objects of this frame (slot i+1), what happened to them, and whether their counters are armed */
    uint32_t occl_handle[255]; uint8_t occl_flags[255]; uint16_t occl_n; uint8_t occl_armed;
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
    uint32_t tex_border[4];         /* D3DTSS_BORDERCOLOR (guest state index 29), ARGB */
    uint32_t vs_handle;
    float    vsc[192][4];                    /* c[-96..95] as D3D exposes them          */
    const float (*vsc_source)[4];            /* synchronized owner; partial/UI writes invalidate */
    float    const_attr[16][4];
    uint32_t z_enable, z_write, z_func, cull, blend_enable, src_blend, dst_blend, color_mask;
    xv_stencil stencil;
    uint32_t indices_dbg;                    /* last index pointer handed to a draw (guest), for traces */
    uint32_t ps_hash;                        /* combiner program id (recomp/kernel/xd3d.c psdef_hash) */
    uint32_t ps_key;
    uint8_t blend_op;
    float    psc[18][4];                     /* c0[stage 0..7], c1[stage 0..7], final c0, c1 */
} d3d_state_t;

static const xv_vs_desc_t *const *g_table;
static unsigned            g_table_count;
static vs_slot_t           g_vs[XV_MAX_VS];
static unsigned            g_nvs;
static d3d_state_t         S;
static cmdlist_t          *g_lists[XV_NUM_LISTS];
static unsigned            g_record_pass;
static uint32_t            g_build_frame;              /* frame being recorded         */
static uint32_t            g_clear_vs;                  /* handle of xv_clear           */

/* GPU-visible scratch: sequential indices + clear quads (uncached, mapped READ). */
static SceUID    g_scratch_uid;
static uint8_t  *g_scratch;
static uint16_t *g_seq_indices;
static uint8_t  *g_clear_quads;                          /* XV_NUM_LISTS x SLOTS x 4 verts x 16 B */
static uint16_t *g_quad_indices;                         /* XV_NUM_LISTS x XV_QUAD_INDICES */
static uint32_t  g_quad_used[XV_NUM_LISTS];
static uint16_t *g_frame_indices;
static uint32_t  g_index_used[XV_NUM_LISTS];
static uint8_t  *g_im_vertices;
static uint32_t  g_im_used[XV_NUM_LISTS];
static uint64_t  g_im_requested[XV_NUM_LISTS];
static uint64_t g_index_requested[XV_NUM_LISTS];
/* Serialized recorder scratch. Reset for every draw; no GPU/pump pointer to it. */
static xv_vertex_refs g_draw_vertex_refs;
static int g_draw_vertex_refs_valid;

typedef struct { uint32_t data, format; SceGxmTexture tex; uint8_t valid; } tex_entry_t;
static tex_entry_t g_texcache[XV_TEX_CACHE];

static xv_visibility_result g_visibility_results[XV_VISIBILITY_IDS];
/* SGX543MP4 writes one counter array per GPU core. Each frame owns its buffer
 * until the pump retires its GPU notification, just like indices/vertices. */
#define XV_VISIBILITY_GPU_CORES 4u
/* XV_FRAG_CENSUS=<n> (diagnostic, default 0): every n-th frame, each command without a game
 * query gets its own counter (index 512 + command) so final completion reports the depth-passing
 * samples of every draw and clear. Enabling it widens each core's array to 2048 words. */
#define XV_CENSUS_CORE_WORDS 2048u
/* XV_OCCL (recomp/kernel/xk_occlusion.c): per-object counters in the widened array - an object's own draws count into
 * word 1536 + slot - 1, its depth-tested proxy rectangle into 1792 + slot - 1 (slot 1..255). Census frames take the
 * whole array for per-command counters, so a census frame carries no object results. */
#define XV_OCCL_REAL0  1536u
#define XV_OCCL_PROXY0 1792u
#define XV_OCCL_SLOTS  255u
static int g_occl_enabled = -1;           /* XV_OCCL != 0: counters armed, proxies replayed */
static uint16_t g_occl_cur;               /* recording thread: object slot of the draws being recorded */
static uint8_t *g_occl_quads;             /* XV_NUM_LISTS x 255 proxies x 4 verts x 16 B, GPU-mapped */
static SceUID g_occl_quads_uid = -1;
/* census frames: what replay actually bound per command (discard/depth-replace/depth-only, alpha mode) */
static struct { uint8_t valid, discard, replaces_depth, depth_only, alpha_mode; } g_census_fs[XV_CENSUS_CORE_WORDS];
static unsigned g_vis_core_words = XV_VISIBILITY_PER_FRAME;
static int g_census_period = -1;
#define XV_VISIBILITY_STRIDE (g_vis_core_words * sizeof(uint32_t))
#define XV_VISIBILITY_WORDS (g_vis_core_words * XV_VISIBILITY_GPU_CORES)
static uint32_t *g_visibility_memory;
static SceUID g_visibility_uid = -1;
#include "xv_visibility_placement.h"

static const SceGxmDepthFunc DEPTH_FUNCS[] = {
    SCE_GXM_DEPTH_FUNC_NEVER, SCE_GXM_DEPTH_FUNC_LESS, SCE_GXM_DEPTH_FUNC_EQUAL, SCE_GXM_DEPTH_FUNC_LESS_EQUAL,
    SCE_GXM_DEPTH_FUNC_GREATER, SCE_GXM_DEPTH_FUNC_NOT_EQUAL, SCE_GXM_DEPTH_FUNC_GREATER_EQUAL, SCE_GXM_DEPTH_FUNC_ALWAYS,
};

/* Frame-owned vertex constants. The rocket/death and driving GPU dumps both
 * fault at the end of the driver's vertex ring. Keep immutable mesh constants
 * out of that ring; frame acquisition already protects these three slots. */
#define XV_FRAME_CONSTANT_BYTES (XV_CONST_POOL * sizeof(float))
#define XV_FRAME_CONSTANT_ALLOC ((XV_FRAME_CONSTANT_BYTES + 4096u) & ~4095u)
static struct {
    SceUID uid;
    uint8_t *memory;
    uint32_t frame;
    unsigned ready;
} g_frame_constants[XV_NUM_LISTS];
static int8_t g_frame_constant_layout[XV_MAX_VS]; /* 0 unknown, 1 raw float4, -1 packed */
static unsigned g_constant_upload_frames, g_constant_upload_high;
static uint64_t g_constant_upload_bytes;
static unsigned g_constant_direct_draws, g_constant_ring_draws, g_constant_bad_draws;

static void frame_constants_shutdown(void)
{
    for (unsigned i = 0; i < XV_NUM_LISTS; ++i) {
        if (!g_frame_constants[i].memory) continue;
        sceGxmUnmapMemory(g_frame_constants[i].memory);
        sceKernelFreeMemBlock(g_frame_constants[i].uid);
        memset(&g_frame_constants[i], 0, sizeof(g_frame_constants[i]));
    }
    memset(g_frame_constant_layout, 0, sizeof g_frame_constant_layout);
}

static const uint8_t *frame_constants_prepare(const cmdlist_t *l, uint32_t frame)
{
    unsigned slot = frame % XV_NUM_LISTS;
    if (!l->nconsts || l->nconsts > XV_CONST_POOL) return NULL;
    if (g_frame_constants[slot].ready && g_frame_constants[slot].frame == frame)
        return g_frame_constants[slot].memory;
    if (!g_frame_constants[slot].memory) {
        void *mem = NULL;
        SceUID uid = sceKernelAllocMemBlock("xv_frame_const", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
            XV_FRAME_CONSTANT_ALLOC, NULL);
        if (uid < 0) return NULL;
        if (sceKernelGetMemBlockBase(uid, &mem) < 0 || !mem ||
            sceGxmMapMemory(mem, XV_FRAME_CONSTANT_ALLOC, SCE_GXM_MEMORY_ATTRIB_READ) < 0) {
            sceKernelFreeMemBlock(uid); return NULL;
        }
        /* A mapped, zeroed tail also keeps speculative boundary reads inside
         * owned storage. It is not included in any shader's declared window. */
        memset(mem, 0, XV_FRAME_CONSTANT_ALLOC);
        xv_gpu_flush_pump(mem, XV_FRAME_CONSTANT_ALLOC);
        g_frame_constants[slot].memory = mem;
        g_frame_constants[slot].uid = uid;
        XV_LOG("frame constants slot %u: %u KiB + mapped tail @ %p\n", slot,
            (unsigned)(XV_FRAME_CONSTANT_BYTES / 1024), mem);
    }
    unsigned bytes = l->nconsts * sizeof(float);
    memcpy(g_frame_constants[slot].memory, l->consts, bytes);
    xv_gpu_flush_pump(g_frame_constants[slot].memory, bytes);
    g_frame_constants[slot].frame = frame;
    g_frame_constants[slot].ready = 1;
    g_constant_upload_bytes += bytes;
    if (bytes > g_constant_upload_high) g_constant_upload_high = bytes;
    if (++g_constant_upload_frames == 60) {
        XV_LOG("[frame-constants] 60 uploads: %llu KiB max %u KiB; draws direct %u ring %u rejected %u\n",
            (unsigned long long)(g_constant_upload_bytes / 1024), g_constant_upload_high / 1024,
            g_constant_direct_draws, g_constant_ring_draws, g_constant_bad_draws);
        g_constant_upload_frames = g_constant_upload_high = 0;
        g_constant_upload_bytes = 0;
        g_constant_direct_draws = g_constant_ring_draws = g_constant_bad_draws = 0;
    }
    return g_frame_constants[slot].memory;
}

/* Every shipped Halo vertex program exposes c[] as a complete float4 array at
 * offset zero in its default buffer. Verify reflection, including the buffer
 * size, once per loaded program; a packed/override layout retains SDK packing. */
static int frame_constants_raw_layout(unsigned slot, const xv_vshader_t *vs)
{
    if (!g_frame_constant_layout[slot]) {
        const SceGxmProgramParameter *p = vs->p_c;
        int raw = p && vs->desc->c_count &&
            sceGxmProgramParameterGetCategory(p) == SCE_GXM_PARAMETER_CATEGORY_UNIFORM &&
            sceGxmProgramParameterGetType(p) == SCE_GXM_PARAMETER_TYPE_F32 &&
            sceGxmProgramParameterGetComponentCount(p) == 4 &&
            sceGxmProgramParameterGetArraySize(p) == vs->desc->c_count &&
            sceGxmProgramParameterGetResourceIndex(p) == 0 &&
            sceGxmProgramParameterGetContainerIndex(p) == 14 &&
            sceGxmProgramGetDefaultUniformBufferSize(vs->prog) == vs->desc->c_count * 16u;
        g_frame_constant_layout[slot] = raw ? 1 : -1;
        if (!raw) XV_LOG("frame constants: packed layout retains SDK upload for %s\n", vs->desc->gxp);
    }
    return g_frame_constant_layout[slot] > 0;
}

static int bind_vertex_constants(SceGxmContext *ctx, const cmdlist_t *l,
    const cmd_t *c, const xv_vshader_t *vs, uint32_t frame)
{
    if (!vs->p_c) return 1;
    int err = -1;
    /* Pool exhaustion used to submit a draw with the previous draw's constants.
     * Reject it, and all malformed ranges, before touching any GPU buffer. */
    if (c->vs >= XV_MAX_VS || !c->const_n || c->const_n != vs->desc->c_count ||
        l->nconsts > XV_CONST_POOL || c->const_off > l->nconsts / 4 ||
        c->const_n > l->nconsts / 4 - c->const_off) goto bad;
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("XV_FRAME_CONSTANTS"); enabled = !e || atoi(e) != 0;
        XV_LOG("frame-owned vertex constants: %d\n", enabled);
    }
    if (enabled && frame_constants_raw_layout(c->vs, vs)) {
        const uint8_t *base = frame_constants_prepare(l, frame);
        if (!base) goto bad;
        err = sceGxmSetVertexDefaultUniformBuffer(ctx, base + c->const_off * 16u);
        if (err < 0) goto bad;
        ++g_constant_direct_draws;
        return 1;
    }
    void *ub = NULL;
    err = sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
    if (err < 0 || !ub) goto bad;
    err = sceGxmSetUniformDataF(ub, vs->p_c, 0, c->const_n * 4, &l->consts[c->const_off * 4]);
    if (err < 0) goto bad;
    ++g_constant_ring_draws;
    return 1;
bad:
    ++g_constant_bad_draws;
    static unsigned warnings;
    if (warnings++ < 12) XV_LOG("vertex constants rejected: frame %u vs %u offset %u count %u pool %u err %08X\n",
        frame, c->vs, c->const_off, c->const_n, l->nconsts, err);
    return 0;
}
/* End frame-owned vertex constants. */

/* ----------------------------------------------------------------------------------
 *  Init / shutdown
 * -------------------------------------------------------------------------------- */
static void index_reuse_shutdown(void);
int xv_d3d_init(const xv_vs_desc_t *const *table, unsigned count)
{
    g_table = table;
    g_table_count = count;
    for (int i = 0; i < XV_NUM_LISTS; ++i) {
        g_lists[i] = (cmdlist_t *)calloc(1, sizeof(cmdlist_t));
        if (!g_lists[i])
            return -1;
    }
    uint32_t size = (XV_SEQ_INDICES * 2 + XV_NUM_LISTS * XV_CLEAR_SLOTS * 4 * 16 + XV_NUM_LISTS * XV_QUAD_INDICES * 2 + XV_NUM_LISTS * XV_IM_VERTICES * XV_IM_STRIDE + XV_NUM_LISTS * XV_FRAME_INDICES * 2 + 0xFFF) & ~0xFFFu;
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
    g_im_vertices = (uint8_t *)(g_quad_indices + XV_NUM_LISTS * XV_QUAD_INDICES);
    g_frame_indices = (uint16_t *)(g_im_vertices + XV_NUM_LISTS * XV_IM_BYTES);

    memset(&S, 0, sizeof(S)); S.color_mask = 0xF;
    S.z_enable = 1; S.z_write = 1; S.z_func = X_D3DCMP_LESSEQUAL; S.cull = X_D3DCULL_CCW;
    S.src_blend = X_D3DBLEND_ONE; S.dst_blend = X_D3DBLEND_ZERO;
    for (unsigned i = 0; i < 16; i++) S.const_attr[i][3] = 1.0f;
    for (int i = 0; i < 4; ++i) {
        S.tex_min[i] = S.tex_mag[i] = X_D3DTEXF_POINT;
        S.tex_addr_u[i] = S.tex_addr_v[i] = X_D3DTADDRESS_WRAP;
    }
    XV_LOG("HLE up: %u recompiled vertex shaders known, %u cmd slots/frame\n", count, XV_MAX_CMDS);
    return 0;
}

static void rt_shutdown(void);
void xv_d3d_shutdown(void)
{
#if XV_POSE_PIPELINE
    xv_pose_pipeline_shutdown();
#endif
    xv_vertex_capture_shutdown();
    frame_constants_shutdown();
    xv_vertex_prepare_shutdown();
    xv_vertex_upload_shutdown();
    index_reuse_shutdown();
    if (g_visibility_memory) {
        sceGxmUnmapMemory(g_visibility_memory);
        sceKernelFreeMemBlock(g_visibility_uid);
        g_visibility_memory = NULL;
    }
    rt_shutdown();
    ps_links_shutdown();
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
    S.vsc_source = NULL;
    S.vsc_gen++; /* invalidate the command list's cached constant snapshot */
    for (unsigned i = 0; i < count; ++i) {
        int r = reg + (int)i + 96;
        if (r >= 0 && r < 192)
            memcpy(S.vsc[r], data + 4 * i, 16);
    }
}

void xv_d3d_SetVertexData4f(unsigned vreg, float x, float y, float z, float w)
{
    if (vreg >= 16) return;
    S.const_attr[vreg][0] = x; S.const_attr[vreg][1] = y; S.const_attr[vreg][2] = z; S.const_attr[vreg][3] = w;
}
void xv_d3d_SetAllAttributes(const float (*attributes)[4]) { memcpy(S.const_attr, attributes, sizeof S.const_attr); }

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
    case X_D3DTSS_BORDERCOLOR: S.tex_border[stage] = value; break;
    case X_D3DTSS_ADDRESSV:  S.tex_addr_v[stage] = (uint8_t)value; break;
    case X_D3DTSS_MAGFILTER: S.tex_mag[stage] = (uint8_t)value; break;
    case X_D3DTSS_MINFILTER: S.tex_min[stage] = (uint8_t)value; break;
    default: break;                    /* combiner-related states arrive with the PSDEF work */
    }
}

void xv_d3d_SetStencil(const xv_stencil *state) { S.stencil = *state; }
void xv_d3d_SetRenderState_ZEnable(uint32_t v)          { S.z_enable = v; }
void xv_d3d_SetRenderState_ZWriteEnable(uint32_t v)     { S.z_write = v; }
void xv_d3d_SetRenderState_ZFunc(uint32_t v)            { S.z_func = v; }
void xv_d3d_SetRenderState_CullMode(uint32_t v)         { S.cull = v; }
void xv_d3d_SetRenderState_AlphaBlendEnable(uint32_t v) { S.blend_enable = v; }
void xv_d3d_SetRenderState_ColorWriteEnable(uint32_t v) { S.color_mask = v; }
void xv_d3d_SetRenderState_SrcBlend(uint32_t v)         { S.src_blend = v; }
void xv_d3d_SetRenderState_DestBlend(uint32_t v)        { S.dst_blend = v; }
void xv_d3d_SetRenderState_BlendOp(uint32_t v)
{
    switch (v) {
    case 0x800a: S.blend_op = 1; break; /* subtract */
    case 0x800b: S.blend_op = 2; break; /* reverse subtract */
    case 0x8007: S.blend_op = 3; break; /* min */
    case 0x8008: S.blend_op = 4; break; /* max */
    default: S.blend_op = 0; break;    /* add, including reset/unset */
    }
}

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

/* D3D BORDER addressing samples the stage's border color outside [0,1]. GXM's
 * full-border clamp samples transparent black, which is what the projected
 * shadow/light textures need (their outside must contribute nothing); plain
 * CLAMP smeared the map's edge texels across the floor (stretched/repeated
 * silhouettes). A non-black border color keeps CLAMP and is logged once.
 * XV_BORDER_ADDR=0 restores the old mapping. */
static SceGxmTextureAddrMode addr_mode_border(uint32_t border)
{
    static int enabled = -1;
    if (enabled < 0) { const char *e = getenv("XV_BORDER_ADDR"); enabled = !e || atoi(e) != 0;
        XV_LOG("border addressing: %s\n", enabled ? "GXM full border (transparent black)" : "clamp (legacy)"); }
    if (!enabled) return SCE_GXM_TEXTURE_ADDR_CLAMP;
    if (border & 0xFFFFFFu) {
        static unsigned warned; if (warned++ < 4) XV_LOG("border color %08X is not black: keeping clamp for this stage\n", border);
        return SCE_GXM_TEXTURE_ADDR_CLAMP;
    }
    return SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER;
}
static SceGxmTextureAddrMode addr_mode_with(uint8_t x, uint32_t border);
static SceGxmTextureAddrMode addr_mode(uint8_t x) { return addr_mode_with(x, 0); }
static SceGxmTextureAddrMode addr_mode_with(uint8_t x, uint32_t border)
{
    switch (x) {
    case X_D3DTADDRESS_MIRROR:      return SCE_GXM_TEXTURE_ADDR_MIRROR;
    case X_D3DTADDRESS_CLAMP:
    case X_D3DTADDRESS_CLAMPTOEDGE: return SCE_GXM_TEXTURE_ADDR_CLAMP;
    case X_D3DTADDRESS_BORDER:      return addr_mode_border(border);
    default:                        return SCE_GXM_TEXTURE_ADDR_REPEAT;
    }
}

static cmdlist_t *cur_list(void);
/* Storage belongs to a guest pixel allocation, not its surface header. Released
 * entries retain their GXM objects for reuse, after both recorded lists retire. */
typedef struct {
    uint32_t data, owner, last_frame;
    unsigned w, h, fmt, bytes, driver_bytes;
    SceUID uid;
    void *mem;
    SceGxmRenderTarget *rt;
    SceGxmColorSurface color;
    SceGxmDepthStencilSurface depth;
    SceGxmTexture tex;
} rt_alias_t;
static rt_alias_t g_rt[XV_RT_SLOTS];
#include "xv_scene_census_plan.h"
#include "xv_query_boundary.h"
#include "xv_depth_store.h"
static unsigned g_rt_bytes;
extern void xv_render_target_drain(void); /* wait for pump AND GPU, outside a scene */

static int rt_formats(unsigned fmt, SceGxmColorFormat *cf, SceGxmTextureFormat *tf, unsigned *bpp)
{
    *bpp = 4;
    switch (fmt) {
    case 0x06: case 0x12:
        *cf = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ARGB; *tf = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; return 1;
    case 0x07: case 0x1e:
        *cf = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ARGB; *tf = SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB; return 1;
    case 0x05: case 0x11:
        *cf = SCE_GXM_COLOR_FORMAT_U5U6U5_RGB; *tf = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB; *bpp = 2; return 1;
    case 0x02: case 0x10:
        *cf = SCE_GXM_COLOR_FORMAT_U1U5U5U5_ARGB; *tf = SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ARGB; *bpp = 2; return 1;
    case 0x04: case 0x1d:
        *cf = SCE_GXM_COLOR_FORMAT_U4U4U4U4_ARGB; *tf = SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ARGB; *bpp = 2; return 1;
    case 0x00: case 0x13:
        *cf = SCE_GXM_COLOR_FORMAT_U8_R; *tf = SCE_GXM_TEXTURE_FORMAT_U8_1RRR; *bpp = 1; return 1;
    case 0x19: case 0x1f:
        *cf = SCE_GXM_COLOR_FORMAT_U8_A; *tf = SCE_GXM_TEXTURE_FORMAT_U8_R000; *bpp = 1; return 1;
    default: return 0;
    }
}
static void rt_destroy(rt_alias_t *r)
{
    if (r->rt) sceGxmDestroyRenderTarget(r->rt);
    if (r->mem) { sceGxmUnmapMemory(r->mem); sceKernelFreeMemBlock(r->uid); }
    g_rt_bytes -= r->bytes + r->driver_bytes;
    memset(r, 0, sizeof *r);
}
static void rt_shutdown(void)
{
    for (unsigned i = 0; i < XV_RT_SLOTS; ++i) rt_destroy(&g_rt[i]);
}
void xv_d3d_ReleaseRenderTarget(uint32_t data)
{
    data &= 0x03ffffffu;
    for (unsigned i = 0; i < XV_RT_SLOTS; ++i)
        if (g_rt[i].owner == data) g_rt[i].data = g_rt[i].owner = 0;
}
static rt_alias_t *rt_find(uint32_t data)
{
    data &= 0x03ffffffu;
    if (data) for (unsigned i = 0; i < XV_RT_SLOTS; ++i)
        if (g_rt[i].data == data) return &g_rt[i];
    return NULL;
}
const SceGxmTexture *xv_d3d_render_target_texture(uint32_t hdr)
{
    if (!hdr) return NULL;
    const uint32_t *h = xv_guest_ptr(hdr);
    rt_alias_t *r = rt_find(h[1]);
    if (!r || r->fmt != ((h[3] >> 8) & 255)) return NULL;
    r->last_frame = g_build_frame;
    return &r->tex;
}
static rt_alias_t *rt_register(uint32_t data, unsigned w, unsigned h, unsigned fmt)
{
    rt_alias_t *r = rt_find(data);
    if (r) {
        if (r->w != w || r->h != h || r->fmt != fmt) goto fail;
        r->last_frame = g_build_frame;
        return r;
    }
    SceGxmColorFormat cf; SceGxmTextureFormat tf; unsigned bpp;
    if (!w || !h || w > 1024 || h > 1024 || !rt_formats(fmt, &cf, &tf, &bpp)) goto fail;
    /* Prefer an idle entry with identical geometry/format; never alias two live
     * surfaces just because their dimensions match. */
    for (unsigned i = 0; i < XV_RT_SLOTS; ++i) {
        rt_alias_t *q = &g_rt[i];
        if (q->data || (q->mem && (uint32_t)(g_build_frame - q->last_frame) < XV_NUM_LISTS)) continue;
        if (!r) r = q;
        if (q->mem && q->w == w && q->h == h && q->fmt == fmt) { r = q; break; }
    }
    if (!r) goto fail;
    xv_render_target_drain();
    if (r->mem && (r->w != w || r->h != h || r->fmt != fmt)) rt_destroy(r);
    if (!r->mem) {
        unsigned stride = (w + 7) & ~7u, ds = (w + 31) & ~31u;
        unsigned color_bytes = (stride * h * bpp + 4095) & ~4095u;
        unsigned bytes = color_bytes + ((ds * ((h + 31) & ~31u) * 4 + 4095) & ~4095u);
        SceGxmRenderTargetParams rp; memset(&rp, 0, sizeof rp);
        rp.width = w; rp.height = h;
        rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE; rp.driverMemBlock = -1;
        unsigned driver_bytes = 0;
        if (bytes > XV_RT_BUDGET || g_rt_bytes > XV_RT_BUDGET - bytes) goto fail;
        SceUID uid = sceKernelAllocMemBlock("xv_rt", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, bytes, NULL);
        void *mem = NULL;
        if (uid < 0) goto fail;
        if (sceKernelGetMemBlockBase(uid, &mem) < 0 || sceGxmMapMemory(mem, bytes, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE) < 0) {
            sceKernelFreeMemBlock(uid); goto fail;
        }
        r->uid = uid; r->mem = mem; r->bytes = bytes;
        g_rt_bytes += bytes;
        if (xv_render_target_create(&rp, XV_RT_BUDGET - g_rt_bytes, &r->rt, &driver_bytes, "offscreen") < 0) {
            rt_destroy(r); goto fail;
        }
        r->driver_bytes = driver_bytes; g_rt_bytes += driver_bytes;
        if (sceGxmColorSurfaceInit(&r->color, cf, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, w, h, stride, mem) < 0 ||
            sceGxmTextureInitLinear(&r->tex, mem, tf, w, h, 1) < 0 ||
            sceGxmDepthStencilSurfaceInit(&r->depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
                SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR, ds, (uint8_t *)mem + color_bytes, NULL) < 0) {
            rt_destroy(r); goto fail;
        }
        sceGxmDepthStencilSurfaceSetForceLoadMode(&r->depth, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
        sceGxmDepthStencilSurfaceSetForceStoreMode(&r->depth, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
        sceGxmTextureSetMinFilter(&r->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&r->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetUAddrMode(&r->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&r->tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
        r->w = w; r->h = h; r->fmt = fmt;
        XV_LOG("RT slot %u: %ux%u fmt %02X, pool %u KB\n", (unsigned)(r - g_rt), w, h, fmt, g_rt_bytes >> 10);
    }
    memset(r->mem, 0, r->bytes);
    r->data = data & 0x03ffffffu; r->last_frame = g_build_frame;
    return r;
fail:
    { static unsigned n; if (n++ < 16) XV_LOG("RT %08X %ux%u fmt %02X unavailable; pass skipped\n", data, w, h, fmt); }
    return NULL;
}
void xv_d3d_SetRenderTarget(uint32_t surface_hdr, int is_backbuffer)
{
    cmdlist_t *l = cur_list();
    if (!surface_hdr) return; /* Xbox NULL retains the current color target. */
    if (is_backbuffer) { l->cur_pass = 0; return; }
    static int drop = -1;
    if (drop < 0) { const char *e = getenv("XV_DROP_RT"); drop = e && atoi(e) == 1; }
    l->cur_pass = 0xff;
    if (drop) return;
    const uint32_t *hdr = xv_guest_ptr(surface_hdr);
    uint32_t fw = hdr[3], sz = hdr[4];
    unsigned w = sz ? (sz & 0xfff) + 1 : 1u << ((fw >> 20) & 15);
    unsigned h = sz ? ((sz >> 12) & 0xfff) + 1 : 1u << ((fw >> 24) & 15);
    if (!hdr[1]) return;
    rt_alias_t *r = rt_register(hdr[1], w, h, (fw >> 8) & 255);
    if (r) {
        uint32_t parent = (hdr[0] & 0x00070000u) == 0x00050000u ? hdr[5] : 0;
        r->owner = (parent ? ((const uint32_t *)xv_guest_ptr(parent))[1] : hdr[1]) & 0x03ffffffu;
        l->cur_pass = (unsigned)(r - g_rt) + 1;
    }
}
static int recording_dropped(void) { return cur_list()->cur_pass == 0xff; }
static void captured_draw_complete(void *context,int ok)
{
    /* Recording owner only, before the command list is published. Preserve
     * later command/UI/query positions if an asynchronous allocation failed. */
    if(!ok) {
        cmd_t *c=context;c->kind=2;
        cur_list()->dropped++;cur_list()->drop_attributes++;
    }
}

int xv_d3d_record_ui(unsigned frame, unsigned batch)
{
    cmdlist_t *l = cur_list();
    if (recording_dropped() || l->nui >= 1024) return 0;
    unsigned i = l->nui++;
    l->ui[i].before = l->ncmds; l->ui[i].frame = frame;
    l->ui[i].batch = batch; l->ui[i].target = l->cur_pass;
    if (l->cur_pass) g_rt[l->cur_pass - 1].last_frame = g_build_frame;
    return 1;
}

static int draw_scan_override = -1;
static xv_rec_opt g_opt_index = XV_REC_OPT_INIT("XV_REC_INDEX");   /* retain_indices: see retain_new */
/* XV_REC_DRAW: exact memos of per-draw lookups whose inputs are immutable tables or the memo key itself (vertex
 * program handle by microcode hash, combiner table entry, reference layout per declaration and stream, texture-
 * coordinate scale per size word), and the draw's trace flags read once per draw. Verify recomputes each lookup
 * and compares. */
static xv_rec_opt g_opt_draw = XV_REC_OPT_INIT("XV_REC_DRAW");
static int g_draw_mode;   /* XV_REC_DRAW path of the draw being recorded (record_draw sets it) */
static xv_rec_opt g_opt_query = XV_REC_OPT_INIT("XV_REC_QUERY");
static void draw_memo_mismatch(const char *what, unsigned a, unsigned b);
static unsigned scan_index_calls, scan_constant_checks, scan_constant_reused;
static unsigned scan_reference_calls, scan_reference_fast;
static uint64_t scan_indices, scan_constant_bytes;
static void index_reuse_report(unsigned frames);
void xv_d3d_draw_scan_override(int enabled)
{ draw_scan_override = enabled < 0 ? -1 : !!enabled; }
static int draw_scan_neon(void)
{
    static int configured = -1;
    if (configured < 0) {
        const char *e = getenv("XV_DRAW_SCAN_NEON");
        /* Three native-resolution campaign comparisons reduce index work and
         * complete-frame time. Preserve explicit zero and benchmark restore. */
        configured = !e || atoi(e) != 0;
    }
    return draw_scan_override < 0 ? configured : draw_scan_override;
}
#ifdef XV_RUN_RECOMP
/* The recording thread borrows validated texture descriptors before applying
 * its captured sampler state. Texture lifetime belongs to the UI bridge. */
const SceGxmTexture *xv_ui_gxm_texture(uint32_t hdr);
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t hdr, uint32_t pal_guest);
void xv_ui_gxm_apply_texture_options(SceGxmTexture *texture);
int xv_ui_gxm_texture_opaque(const SceGxmTexture *texture);
static const SceGxmTexture *texture_source[4];
static unsigned sampler_hits, sampler_misses, texture_source_memo_hits, texture_source_memo_misses;
void xv_d3d_prep_cache_report(unsigned frames)
{
    XV_LOG("[draw-scan] %u frames: enabled %d; %u index copies / %llu indices; %u constant checks / %u unchanged / %llu KiB checked; exact bytes, draw order preserved\n",
        frames, draw_scan_neon(), scan_index_calls, (unsigned long long)scan_indices,
        scan_constant_checks, scan_constant_reused, (unsigned long long)(scan_constant_bytes >> 10));
    scan_index_calls = scan_constant_checks = scan_constant_reused = 0;
    scan_indices = scan_constant_bytes = 0;
    XV_LOG("[index-coverage] %u frames: %u reference copies / %u NEON batches (at least 256 indices)\n",
        frames, scan_reference_calls, scan_reference_fast);
    scan_reference_calls = scan_reference_fast = 0;
    index_reuse_report(frames);
    XV_REC_OPT_REPORT(&g_opt_index, frames, XV_LOG);
    XV_REC_OPT_REPORT(&g_opt_draw, frames, XV_LOG);
    XV_REC_OPT_REPORT(&g_opt_query, frames, XV_LOG);
    XV_LOG("[sampler-cache] %u frames: %u reused / %u prepared (texture validity still checked)\n", frames, sampler_hits, sampler_misses);
    sampler_hits = sampler_misses = 0;
    XV_LOG("[texture-source-memo] %u frames: %u stage lookups reused / %u resolved (same header+palette as the previous draw, same frame)\n", frames, texture_source_memo_hits, texture_source_memo_misses);
    texture_source_memo_hits = texture_source_memo_misses = 0;
    XV_LOG("[texture-prep] %u frames: %u stages prepared / %u unused skipped\n", frames, texture_stages_prepared, texture_stages_skipped);
    texture_stages_prepared = texture_stages_skipped = 0;
    XV_LOG("[depth-prepare] %u frames: enabled %d; %u draws omitted texture preparation using published shader proofs\n",
        frames, depth_prepare_enabled(), depth_prepare_hits);
    depth_prepare_hits = 0;
    XV_LOG("[opaque-material] %u frames: %u eligible / %u proven opaque (captured upload; alpha test retained otherwise)\n", frames, opaque_candidates, opaque_proven);
    opaque_candidates = opaque_proven = 0;
}
static const SceGxmTexture *texture_for(unsigned stage)
{
    static SceGxmTexture t[4];
    static uint32_t keys[4][8];
    static uint8_t valid[4];
    static int enabled = -1;
    if (enabled < 0) { const char *e = getenv("XV_SAMPLER_CACHE"); enabled = !e || atoi(e) != 0; }
    const SceGxmTexture *src = NULL;
    /* Per-frame source memo: consecutive draws mostly bind the same texture per stage (the 181 flare quads, the
     * per-cluster BSP draws), and the render-target + palette-cache lookups (with their 1 KiB palette compares)
     * were paid again for each. The cache entries are stable within a frame (no eviction pressure at 80 textures);
     * the memo is keyed by the recording frame so render-target aliases and streamed uploads never cross frames. */
    static uint32_t memo_hdr[4], memo_pal[4], memo_frame[4]; static const SceGxmTexture *memo_src[4]; static int memo_on = -1;
    if (memo_on < 0) { const char *e = getenv("XV_TEXTURE_SOURCE_MEMO"); memo_on = e ? atoi(e) != 0 : 1; }
    if (memo_on && memo_frame[stage] == g_build_frame + 1u && memo_hdr[stage] == S.tex_guest[stage] && memo_pal[stage] == S.pal_guest[stage]) {
        src = memo_src[stage]; texture_source_memo_hits++;
    } else {
        src = xv_d3d_render_target_texture(S.tex_guest[stage]);
        if (!src) src = xv_ui_gxm_texture_pal(S.tex_guest[stage], S.pal_guest[stage]);
        memo_frame[stage] = g_build_frame + 1u; memo_hdr[stage] = S.tex_guest[stage]; memo_pal[stage] = S.pal_guest[stage]; memo_src[stage] = src;
        texture_source_memo_misses++;
    }
    texture_source[stage] = src;
    if (!src) return NULL;
    /* Always resolve the live texture first: streaming, palettes, render-target
     * reuse and dirty pixels retain the cache owner's existing validation.
     * User filter options are immutable for this process (set before launch). */
    uint32_t key[8];
    _Static_assert(sizeof *src == 16, "GXM texture control word size");
    memcpy(key, src, sizeof *src);
    key[4] = S.tex_min[stage]; key[5] = S.tex_mag[stage];
    key[6] = S.tex_addr_u[stage] | (S.tex_border[stage] << 8); key[7] = S.tex_addr_v[stage];
    if (enabled && valid[stage] && !memcmp(keys[stage], key, sizeof key)) {
        sampler_hits++; return &t[stage];
    }
    sampler_misses++;
    t[stage] = *src;
    sceGxmTextureSetMinFilter(&t[stage], S.tex_min[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&t[stage], S.tex_mag[stage] == X_D3DTEXF_POINT ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    xv_ui_gxm_apply_texture_options(&t[stage]);
    sceGxmTextureSetUAddrMode(&t[stage], addr_mode_with(S.tex_addr_u[stage], S.tex_border[stage]));
    sceGxmTextureSetVAddrMode(&t[stage], addr_mode_with(S.tex_addr_v[stage], S.tex_border[stage]));
    memcpy(keys[stage], key, sizeof key); valid[stage] = 1;
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

void xd3d_r_visibility_begin(uint32_t w, uint32_t h)
{
    cmdlist_t *l=cur_list();
    l->active_visibility=0;
    if (l->nvisibility == XV_VISIBILITY_PER_FRAME) return;
    unsigned slot=l->nvisibility++;
    memset(&l->visibility[slot],0,sizeof l->visibility[slot]);
    l->visibility[slot].guest_area=(w && h && w<=4096 && h<=4096) ? w*h : 640u*480u;
    l->active_visibility=slot+1;
}
/* XV_REC_QUERY index: open addressing, slot + 1 per cell; cells are only ever filled (single writer: the issuer),
 * each after its slot's id/used, so a concurrent lookup either finds the slot or stops at an empty cell. */
#define VIS_MAP_CELLS 2048u
static uint16_t g_vis_map[VIS_MAP_CELLS];
static unsigned g_vis_count;
static unsigned vis_hash(uint32_t id) { return (id * 2654435761u) >> 21; }
static int vis_map_lookup(uint32_t id)
{
    for (unsigned h = vis_hash(id);; h = (h + 1) & (VIS_MAP_CELLS - 1)) {
        unsigned cell = __atomic_load_n(&g_vis_map[h], __ATOMIC_ACQUIRE);
        if (!cell) return -1;
        if (g_visibility_results[cell - 1].id == id) return (int)cell - 1;
    }
}
static void vis_map_add(uint32_t id, unsigned slot)
{
    unsigned h = vis_hash(id);
    while (g_vis_map[h]) h = (h + 1) & (VIS_MAP_CELLS - 1);
    __atomic_store_n(&g_vis_map[h], (uint16_t)(slot + 1), __ATOMIC_RELEASE);
}
/* The slot xv_visibility_read/find select (fast identity check, then the full scan), or -1. */
static int vis_scan(uint32_t id)
{
    unsigned i = id;
    if (i < XV_VISIBILITY_IDS && g_visibility_results[i].used && g_visibility_results[i].id == id) return (int)i;
    for (i = 0; i < XV_VISIBILITY_IDS; i++)
        if (g_visibility_results[i].used && g_visibility_results[i].id == id) return (int)i;
    return -1;
}
/* mode 1: the scan's slot (used), compared with the index; mode 2: the index. */
static int vis_slot(uint32_t id)
{
    int mode = xv_rec_opt_mode(&g_opt_query);
    int fast = vis_map_lookup(id);
    if (mode == 2) return fast;
    int slot = vis_scan(id);
    if (xv_rec_opt_result(&g_opt_query, slot == fast))
        XV_LOG("[rec-verify] XV_REC_QUERY mismatch frame %u: id %u scan %d index %d\n", g_build_frame, id, slot, fast);
    return slot;
}
static uint32_t vis_issue_fast(uint32_t id, uint16_t *slot, uint32_t *serial)
{
    int i = vis_map_lookup(id);
    if (i < 0) {
        if (g_vis_count >= XV_VISIBILITY_IDS) return XV_VISIBILITY_OUT_OF_MEMORY;
        i = (int)g_vis_count++;
        g_visibility_results[i].id = id; g_visibility_results[i].used = 1;
        vis_map_add(id, (unsigned)i);
    }
    uint32_t next = g_visibility_results[i].issued + 1u;
    if (!next) next = 1;
    __atomic_store_n(&g_visibility_results[i].issued, next, __ATOMIC_RELEASE);
    *slot = (uint16_t)i; *serial = next;
    return 0;
}
uint32_t xd3d_r_visibility_end(uint32_t id)
{
    cmdlist_t *l=cur_list();
    unsigned slot=l->active_visibility;
    l->active_visibility=0;
    if (!slot) return XV_VISIBILITY_OUT_OF_MEMORY;
    int mode = xv_rec_opt_mode(&g_opt_query);
    if (mode == 2) return vis_issue_fast(id, &l->visibility[slot-1].result_slot, &l->visibility[slot-1].serial);
    if (mode == 0) {
        unsigned before = g_vis_count;
        uint32_t rc = xv_visibility_issue(g_visibility_results,id,
            &l->visibility[slot-1].result_slot,&l->visibility[slot-1].serial);
        /* XV_REC_AB: keep the index complete while the scan path runs (a new ID takes slot `count`) */
        if (!rc && xv_rec_opt_maintain(&g_opt_query) && l->visibility[slot-1].result_slot == before &&
            g_visibility_results[before].id == id && vis_map_lookup(id) < 0) { vis_map_add(id, before); g_vis_count++; }
        return rc;
    }
    /* verify: predict the slot/serial from the index, then issue through the scan */
    int known = vis_map_lookup(id);
    unsigned expect_slot = known >= 0 ? (unsigned)known : g_vis_count;
    uint32_t expect_rc = known < 0 && g_vis_count >= XV_VISIBILITY_IDS ? XV_VISIBILITY_OUT_OF_MEMORY : 0;
    uint32_t expect_serial = expect_rc ? 0 : g_visibility_results[expect_slot].issued + 1u;
    if (!expect_rc && !expect_serial) expect_serial = 1;
    uint32_t rc = xv_visibility_issue(g_visibility_results,id,
        &l->visibility[slot-1].result_slot,&l->visibility[slot-1].serial);
    if (!rc && known < 0) {   /* the scan allocated: it must be the next prefix slot */
        vis_map_add(id, l->visibility[slot-1].result_slot);
        if (l->visibility[slot-1].result_slot == g_vis_count) g_vis_count++;
    }
    int equal = rc == expect_rc && (rc || (l->visibility[slot-1].result_slot == expect_slot && l->visibility[slot-1].serial == expect_serial));
    if (xv_rec_opt_result(&g_opt_query, equal))
        XV_LOG("[rec-verify] XV_REC_QUERY mismatch frame %u: issue id %u rc %08X/%08X slot %u/%u serial %u/%u\n", g_build_frame, id,
            rc, expect_rc, l->visibility[slot-1].result_slot, expect_slot, l->visibility[slot-1].serial, expect_serial);
    return rc;
}
uint32_t xd3d_r_visibility_result(uint32_t id, uint32_t *pixels)
{
    if (xv_rec_opt_mode(&g_opt_query) == 0) return xv_visibility_read(g_visibility_results,id,pixels);
    int i = vis_slot(id);
    if (i < 0) return XV_VISIBILITY_INVALID_ARGUMENT;
    /* xv_visibility_read on its selected slot */
    uint32_t issued=__atomic_load_n(&g_visibility_results[i].issued,__ATOMIC_ACQUIRE);
    if (!issued || __atomic_load_n(&g_visibility_results[i].completed,__ATOMIC_ACQUIRE) != issued)
        return XV_VISIBILITY_INCOMPLETE;
    *pixels=__atomic_load_n(&g_visibility_results[i].pixels,__ATOMIC_RELAXED);
    return 0;
}
static xv_visibility_result *vis_find(uint32_t id)
{
    if (xv_rec_opt_mode(&g_opt_query) == 0) return xv_visibility_find(g_visibility_results,id);
    int i = vis_slot(id);
    return i < 0 ? NULL : &g_visibility_results[i];
}
#ifdef XV_FLARE_QUERY_OVERLAP
uint32_t xd3d_r_visibility_generation(uint32_t id)
{
    if (xv_rec_opt_mode(&g_opt_query) == 0) return xv_visibility_generation(g_visibility_results,id);
    xv_visibility_result *r=vis_find(id);
    return r ? __atomic_load_n(&r->issued,__ATOMIC_ACQUIRE) : 0;
}
uint32_t xd3d_r_visibility_result_generation(uint32_t id,uint32_t serial,uint32_t *pixels)
{
    if (xv_rec_opt_mode(&g_opt_query) == 0) return xv_visibility_read_generation(g_visibility_results,id,serial,pixels);
    /* xv_visibility_read_generation on the selected slot */
    xv_visibility_result *r=vis_find(id);
    if (!r || !serial) return XV_VISIBILITY_INVALID_ARGUMENT;
    const xv_visibility_sample *h=&r->history[serial % XV_VISIBILITY_HISTORY];
    uint32_t first=__atomic_load_n(&h->sequence,__ATOMIC_SEQ_CST);
    if (first&1u) return XV_VISIBILITY_INCOMPLETE;
    uint32_t found=__atomic_load_n(&h->serial,__ATOMIC_SEQ_CST);
    uint32_t value=__atomic_load_n(&h->pixels,__ATOMIC_SEQ_CST);
    if (first!=__atomic_load_n(&h->sequence,__ATOMIC_SEQ_CST) || found!=serial)
        return XV_VISIBILITY_INCOMPLETE;
    *pixels=value;
    return 0;
}
#endif
uint32_t xd3d_r_visibility_result_stale(uint32_t id, uint32_t *pixels, uint32_t *behind)
{
    if (xv_rec_opt_mode(&g_opt_query) == 0) return xv_visibility_read_stale(g_visibility_results,id,pixels,behind);
    int i = vis_slot(id);
    if (i < 0) return XV_VISIBILITY_INVALID_ARGUMENT;
    uint32_t issued=__atomic_load_n(&g_visibility_results[i].issued,__ATOMIC_ACQUIRE);
    uint32_t completed=__atomic_load_n(&g_visibility_results[i].completed,__ATOMIC_ACQUIRE);
    if (!completed) return XV_VISIBILITY_INCOMPLETE;
    *pixels=__atomic_load_n(&g_visibility_results[i].pixels,__ATOMIC_RELAXED);
    if (behind) *behind=issued-completed;
    return 0;
}

extern int xk_wait_u32(const uint32_t *,uint32_t,uint64_t) __attribute__((weak));
extern void xk_os_scheduler_notify(void) __attribute__((weak));
extern uint64_t xk_os_monotonic_us(void) __attribute__((weak));
int xd3d_r_visibility_wait(uint32_t id,uint32_t timeout_us,uint32_t *ready_age_us,uint32_t *render_us)
{
    xv_visibility_result *r=vis_find(id);
    if (!r || !xk_wait_u32) return 0;
    uint32_t serial=__atomic_load_n(&r->issued,__ATOMIC_ACQUIRE);
    /* A query still being recorded must return INCOMPLETE so Halo can reach
     * Present. Park only after the pump has started its submitted generation. */
    if (!serial || __atomic_load_n(&r->submitted,__ATOMIC_ACQUIRE)!=serial) return 0;
    xk_wait_u32(&r->completed,serial,timeout_us);
    if (xk_os_monotonic_us && __atomic_load_n(&r->completed,__ATOMIC_ACQUIRE)==serial) {
        *ready_age_us=(uint32_t)xk_os_monotonic_us()-r->completed_us;
        *render_us=r->completed_us-r->submitted_us;
    }
    return 1;
}
#ifdef XV_FLARE_QUERY_OVERLAP
int xd3d_r_visibility_wait_generation(uint32_t id,uint32_t serial,uint32_t timeout_us)
{
    xv_visibility_result *r=vis_find(id);
    if (!r || !serial || !xk_wait_u32 ||
        __atomic_load_n(&r->submitted,__ATOMIC_ACQUIRE)!=serial) return 0;
    /* The serial retained before ID reuse is still the submitted generation.
     * The next Present remains a barrier and cannot submit a replacement. */
    xk_wait_u32(&r->completed,serial,timeout_us);
    return 1;
}
#endif

static cmd_t *new_cmd(void)
{
    cmdlist_t *l = cur_list();
    if (l->ncmds >= XV_MAX_CMDS) {
        l->dropped++; l->drop_commands++;
        return NULL;
    }
    cmd_t *c = &l->cmds[l->ncmds++];
    memset(c, 0, sizeof(*c));
    c->pass = (uint8_t)l->cur_pass;
    c->visibility = (uint16_t)l->active_visibility;
    c->occl = g_occl_cur;
    if (c->pass && c->pass <= XV_RT_SLOTS) g_rt[c->pass - 1].last_frame = g_build_frame;
    return c;
}

static int blend_mode(void)
{
    unsigned mask = S.color_mask & 0xF;
    unsigned src = S.blend_enable ? S.src_blend : X_D3DBLEND_ONE, dst = S.blend_enable ? S.dst_blend : X_D3DBLEND_ZERO;
    unsigned op = S.blend_enable ? S.blend_op : 0;
    if (!S.blend_enable && mask == 0xF)
        return BLEND_OPAQUE;
    for (unsigned i = 1; i < g_blend_combos; ++i)
        if (g_blend_combo[i].src == src && g_blend_combo[i].dst == dst && g_blend_combo[i].mask == mask && g_blend_combo[i].op == op)
            return (int)i;
    if (g_blend_combos < BLEND_NOCOLOR) {
        g_blend_combo[g_blend_combos].src = (uint8_t)src; g_blend_combo[g_blend_combos].dst = (uint8_t)dst; g_blend_combo[g_blend_combos].mask = (uint8_t)mask;
        g_blend_combo[g_blend_combos].op = (uint8_t)op;
        XV_LOG("blend variant %u: %u/%u mask %X op %u\n", g_blend_combos, src, dst, mask, op);
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
static int trace_frame_live(void) { return xd3d_hist_active && xd3d_hist_active(); }
int xd3d_vertex_trace_active(void) __attribute__((weak));
static int vertex_trace_frame_live(void) {
    return xd3d_vertex_trace_active ? xd3d_vertex_trace_active() : trace_frame_live();
}
/* XV_REC_DRAW: record_draw reads both flags once (-1 outside a draw or with the knob off). */
static int g_draw_trace = -1, g_draw_vtrace = -1;
static int trace_frame(void)
{
    if (g_draw_trace < 0) return trace_frame_live();
    if (g_draw_mode == 2) return g_draw_trace;
    int live = trace_frame_live();
    if (xv_rec_opt_result(&g_opt_draw, live == g_draw_trace)) draw_memo_mismatch("trace flag", (unsigned)g_draw_trace, (unsigned)live);
    return live;
}
static int vertex_trace_frame(void)
{
    if (g_draw_vtrace < 0) return vertex_trace_frame_live();
    if (g_draw_mode == 2) return g_draw_vtrace;
    int live = vertex_trace_frame_live();
    if (xv_rec_opt_result(&g_opt_draw, live == g_draw_vtrace)) draw_memo_mismatch("vertex trace flag", (unsigned)g_draw_vtrace, (unsigned)live);
    return live;
}
/* A later diagnostic request must never read a captured packed span with the
 * original declaration stride. Ordinary trace capture already declines it. */
#if XV_PACKED_VERTEX_LAYOUT
#define geometry_trace(c) (trace_frame() && !(c)->packed_vertex)
#else
#define geometry_trace(c) trace_frame()
#endif

/* The generated table is ordered by (vertex, canonical program, 2D cube mask).
 * Inactive combiner state cannot force a material onto a heuristic fragment. */
static int ps_entry_for(uint32_t vs, uint32_t key, unsigned c2d)
{
    unsigned lo = 0, hi = XV_PS_TABLE_COUNT;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const xv_ps_entry_t *e = &xv_ps_table[mid];
        int less = e->vs_fnv < vs || (e->vs_fnv == vs &&
            (e->ps_key < key || (e->ps_key == key && e->c2d_mask < c2d)));
        if (less) lo = mid + 1; else hi = mid;
    }
    if (lo < XV_PS_TABLE_COUNT && xv_ps_table[lo].vs_fnv == vs &&
        xv_ps_table[lo].ps_key == key && xv_ps_table[lo].c2d_mask == c2d) return (int)lo;
    return -1;
}

/* The combiner table is immutable: memoize every (vertex program, program key, 2D mask) result, misses included. */
static int ps_entry_memo(uint32_t vs, uint32_t key, unsigned c2d)
{
    int mode = g_draw_mode;
    if (mode <= 0) return ps_entry_for(vs, key, c2d);
    static struct { uint32_t vs, key; uint8_t c2d, valid; int16_t entry; } memo[128];
    unsigned m = ((vs ^ key * 2654435761u) + c2d * 40503u) * 2654435761u >> 25;
    if (memo[m].valid && memo[m].vs == vs && memo[m].key == key && memo[m].c2d == c2d) {
        if (mode == 2) return memo[m].entry;
        int entry = ps_entry_for(vs, key, c2d);
        if (xv_rec_opt_result(&g_opt_draw, entry == memo[m].entry)) draw_memo_mismatch("combiner entry", (unsigned)memo[m].entry, (unsigned)entry);
        return entry;
    }
    int entry = ps_entry_for(vs, key, c2d);
    memo[m].vs = vs; memo[m].key = key; memo[m].c2d = (uint8_t)c2d; memo[m].valid = 1; memo[m].entry = (int16_t)entry;
    return entry;
}

static uint32_t ps_key_from_capture(uint32_t hash)
{
    unsigned lo = 0, hi = XV_PS_CAPTURE_COUNT;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if (xv_ps_capture_keys[mid].hash < hash) lo = mid + 1; else hi = mid;
    }
    return lo < XV_PS_CAPTURE_COUNT && xv_ps_capture_keys[lo].hash == hash ? xv_ps_capture_keys[lo].key : 0;
}
static const SceGxmTexture *cube_fallback(void);


unsigned xv_d3d_last_draws, xv_d3d_draw_acc, xv_d3d_bsp_acc;   /* draw counters for the frame-time log */
unsigned xv_dbg_count[16];                                        /* instrumented guest functions (portal walker) */
static void *retain_immediate_bytes(const void *source, uint64_t bytes)
{
    unsigned list = g_build_frame % XV_NUM_LISTS;
    uint64_t aligned=(bytes+15u)&~UINT64_C(15);
    g_im_requested[list]+=aligned;
    if (aligned > XV_IM_BYTES-g_im_used[list]) return NULL;
    uint8_t *dst = g_im_vertices + list * XV_IM_BYTES + g_im_used[list];
    memcpy(dst,source,(size_t)bytes);
    g_im_used[list]+=(uint32_t)aligned;
    return dst;
}
static int snapshot_attributes(cmd_t *c)
{
    void *dst=retain_immediate_bytes(S.const_attr,sizeof S.const_attr);
    if (!dst) return 0;
    c->constant_stream = dst;
    return 1;
}

static unsigned index_bounds(const uint16_t *indices, unsigned count)
{
    unsigned maximum = 0;
    for (unsigned i = 0; i < count; i++)
        if (indices[i] > maximum) maximum = indices[i];
    return count ? maximum + 1 : 0;
}

static xv_index_cache *g_index_cache;
static unsigned index_reuse_hits, index_reuse_misses, index_reuse_ineligible;
static uint64_t index_reuse_compared, index_reuse_saved;
static int index_reuse_override = -1;
static unsigned index_metadata_reuploads, index_metadata_full;
static uint64_t index_metadata_bytes;
#ifndef XV_INDEX_METADATA_DEFAULT
#define XV_INDEX_METADATA_DEFAULT 0
#endif
static int index_metadata_enabled(void)
{
    static int configured=-1;
    if (configured<0) {
        const char *e=getenv("XV_INDEX_METADATA");
        configured=e ? atoi(e)==1 : XV_INDEX_METADATA_DEFAULT;
    }
    /* The existing off/on/off diagnostic continues to compare its original
     * within-frame policy. Every override transition resets all identities. */
    return index_reuse_override<0 && configured;
}
static void index_reuse_begin_frame(void)
{
    if (index_metadata_enabled()) xv_index_cache_new_frame(g_index_cache);
    else xv_index_cache_reset(g_index_cache);
}
/* Recording owner, after the submission drain. Invalidate CPU lookup metadata
 * without changing any indices already owned by a live frame. */
void xv_d3d_index_reuse_override(int enabled)
{
    index_reuse_override = enabled < 0 ? -1 : !!enabled;
    xv_index_cache_reset(g_index_cache);
}
static int index_reuse_enabled(void)
{
    static int configured=-1, failed;
    if (configured<0) {
        const char *e=getenv("XV_INDEX_REUSE");
        configured=e && atoi(e)!=0; /* hardware comparison required */
    }
    if (!(index_reuse_override < 0 ? configured || index_metadata_enabled() : index_reuse_override) || failed) return 0;
    if (!g_index_cache) {
        g_index_cache=malloc(sizeof *g_index_cache);
        if (!g_index_cache) { failed=1;return 0; }
        xv_index_cache_reset(g_index_cache);
    }
    return 1;
}

static void index_reuse_report(unsigned frames)
{
    XV_LOG("[index-reuse] %u frames: enabled %d hits %u rebuilt %u ineligible %u; compared %llu KiB avoided %llu KiB GPU copies; exact current-frame indices\n",
        frames,index_reuse_enabled(),index_reuse_hits,index_reuse_misses,index_reuse_ineligible,
        (unsigned long long)(index_reuse_compared/1024),(unsigned long long)(index_reuse_saved/1024));
    XV_LOG("[index-metadata] %u frames: enabled %d reuploads %u full %u; copied %llu KiB into current GPU slot; CPU metadata retained, prior GPU pointers discarded\n",
        frames,index_metadata_enabled() && g_index_cache!=NULL,
        index_metadata_reuploads,index_metadata_full,(unsigned long long)(index_metadata_bytes/1024));
    index_reuse_hits=index_reuse_misses=index_reuse_ineligible=0;
    index_reuse_compared=index_reuse_saved=0;
    index_metadata_reuploads=index_metadata_full=0;index_metadata_bytes=0;
}
static void index_reuse_shutdown(void)
{
    free(g_index_cache);g_index_cache=NULL;
    index_reuse_hits=index_reuse_misses=index_reuse_ineligible=0;
    index_reuse_compared=index_reuse_saved=0;
    index_metadata_reuploads=index_metadata_full=0;index_metadata_bytes=0;
}

/* One retain of guest indices: the recorded draw (real: counters and draw-profile stamps), or the XV_REC_INDEX
 * verify shadow (scratch pool, entry copy and coverage; no counters). */
typedef struct {
    xv_index_cache_entry *entry;  /* selected cache candidate, NULL when ineligible or reuse is off */
    uint16_t *pool;               /* this frame's GPU index pool */
    uint32_t *used;               /* its fill, in indices */
    xv_vertex_refs *refs;
    int *refs_valid;
    int real;
} index_retain_target;
static int index_scan_cached(void)
{
    static int cached_scan = -1;
    if (cached_scan < 0) {
        const char *e = getenv("XV_INDEX_SCAN_CACHED");
        cached_scan = !e || atoi(e) != 0;
        XV_LOG("index bounds: %s\n", cached_scan ? "cached snapshot" : "GPU copy scan (baseline)");
    }
    return cached_scan;
}
/* The original retain (XV_REC_INDEX 0/1): compare, then copy the mirror; on a miss copy the guest list into the
 * mirror, then scan/copy the mirror to the GPU slot. */
static int retain_old(const void **indices, unsigned count, unsigned *nverts, unsigned references,
    index_retain_target *t, uint64_t *sub)
{
    xv_index_cache_entry *entry = t->entry;
    if (entry) {
        if (entry->source==*indices && entry->count==count && entry->references==references && t->real)
            index_reuse_compared+=(uint64_t)count*2u;
        if (xv_index_cache_match(entry,*indices,count,references)) {
            if (!entry->retained) {
                /* CPU equality does not retire a GPU slot. A new frame
                 * needs a fresh append allocation, including on a hit. */
                if (*t->used > XV_FRAME_INDICES || count > XV_FRAME_INDICES-*t->used) {
                    if (t->real) index_metadata_full++;
                    return 0;
                }
                uint16_t *dst=t->pool+*t->used;
                memcpy(dst,entry->mirror,count*sizeof *dst);
                *t->used+=(count+7u)&~7u;
                entry->retained=dst;
                if (t->real) { index_metadata_reuploads++;index_metadata_bytes+=(uint64_t)count*2u;
                               scan_index_calls++;scan_indices+=count; }
            } else if (t->real) {
                index_reuse_hits++;index_reuse_saved+=(uint64_t)count*2u;
            }
            *indices=entry->retained;*nverts=entry->vertices;
            if (references) *t->refs=entry->refs;
            *t->refs_valid=references;
            if (t->real) xv_draw_profile_step(XV_DRAW_IDX_CACHE, sub);
            return 1;
        }
        if (t->real) index_reuse_misses++;
    }
    if (*t->used > XV_FRAME_INDICES || count > XV_FRAME_INDICES - *t->used) return 0;
    uint16_t *dst = t->pool + *t->used;
    const void *source=*indices, *captured=source;
    if (entry) {
        /* Capture once, then derive both GPU bytes and bounds/coverage from
         * that exact cached version. Never rescan mutable guest data later. */
        entry->source=NULL;
        memcpy(entry->mirror,source,count*sizeof(uint16_t));
        captured=entry->mirror;
    }
    if (t->real) xv_draw_profile_step(XV_DRAW_IDX_CACHE, sub);
    int cached_scan = index_scan_cached();
    if (t->real) { scan_index_calls++; scan_indices += count; }
    if (references) {
        int neon = draw_scan_neon();
        if (t->real) {
            scan_reference_calls++;
#if defined(__ARM_NEON)
            scan_reference_fast += neon && count >= 256;
#endif
        }
        *nverts = neon ?
            xv_index_copy_reference_bounds_neon(dst, captured, count, t->refs) :
            xv_index_copy_reference_bounds(dst, captured, count, t->refs);
        *t->refs_valid = 1;
    } else if (cached_scan) *nverts = draw_scan_neon() ?
        xv_index_copy_bounds_neon(dst, captured, count) : xv_index_copy_bounds(dst, captured, count);
    else {
        memcpy(dst, captured, count * sizeof *dst);
        *nverts = index_bounds(dst, count);
    }
    if (t->real) xv_draw_profile_step(XV_DRAW_IDX_SCAN, sub);
    *t->used += (count + 7u) & ~7u; /* keep every draw 16-byte aligned */
    if (entry) {
        entry->retained=dst;entry->vertices=*nverts;entry->count=count;
        entry->references=references;
        if (references) entry->refs=*t->refs;
        entry->source=source;
    }
    *indices = dst;
    return 1;
}
/* XV_REC_INDEX 1/2: the same results with one read of every guest index. A hit that needs this frame's GPU copy
 * copies the guest list while checking it against the mirror (xv_index_copy_if_equal); a miss writes the GPU slot
 * and the mirror from the same loads and derives bounds/coverage from the cached mirror or chunk
 * (xv_index_copy2_*), with register-accumulated coverage bits. */
static int retain_new(const void **indices, unsigned count, unsigned *nverts, unsigned references,
    index_retain_target *t, uint64_t *sub)
{
    xv_index_cache_entry *entry = t->entry;
    int fits = !(*t->used > XV_FRAME_INDICES || count > XV_FRAME_INDICES - *t->used);
    if (entry) {
        int same = entry->source==*indices && entry->count==count && entry->references==references;
        if (same && t->real) index_reuse_compared+=(uint64_t)count*2u;
        if (same) {
            int hit = 0;
            if (entry->retained) {
                hit = xv_bytes_equal(*indices, entry->mirror, count * sizeof(uint16_t));
                if (hit && t->real) { index_reuse_hits++;index_reuse_saved+=(uint64_t)count*2u; }
            } else if (fits) {
                uint16_t *dst = t->pool + *t->used;
                hit = xv_index_copy_if_equal(dst, *indices, entry->mirror, count);
                if (hit) {
                    *t->used += (count + 7u) & ~7u;
                    entry->retained = dst;
                    if (t->real) { index_metadata_reuploads++;index_metadata_bytes+=(uint64_t)count*2u;
                                   scan_index_calls++;scan_indices+=count; }
                }
            } else if (xv_bytes_equal(*indices, entry->mirror, count * sizeof(uint16_t))) {
                if (t->real) index_metadata_full++;
                return 0;
            }
            if (hit) {
                *indices=entry->retained;*nverts=entry->vertices;
                if (references) *t->refs=entry->refs;
                *t->refs_valid=references;
                if (t->real) xv_draw_profile_step(XV_DRAW_IDX_CACHE, sub);
                return 1;
            }
        }
        if (t->real) index_reuse_misses++;
    }
    if (!fits) return 0;
    uint16_t *dst = t->pool + *t->used;
    const void *source = *indices;
    uint16_t *mirror = entry ? entry->mirror : NULL;
    if (entry) entry->source = NULL;
    if (t->real) xv_draw_profile_step(XV_DRAW_IDX_CACHE, sub);
    int cached_scan = index_scan_cached();
    if (t->real) { scan_index_calls++; scan_indices += count; }
    if (references) {
        if (t->real) {
            scan_reference_calls++;
#if defined(__ARM_NEON)
            scan_reference_fast += draw_scan_neon() && count >= 256;
#endif
        }
        *nverts = xv_index_copy2_reference_bounds(dst, mirror, source, count, t->refs);
        *t->refs_valid = 1;
    } else if (cached_scan) *nverts = xv_index_copy2_bounds(dst, mirror, source, count);
    else {
        const void *captured = source;
        if (mirror) { memcpy(mirror, source, count * sizeof *mirror); captured = mirror; }
        memcpy(dst, captured, count * sizeof *dst);
        *nverts = index_bounds(dst, count);
    }
    if (t->real) xv_draw_profile_step(XV_DRAW_IDX_SCAN, sub);
    *t->used += (count + 7u) & ~7u;
    if (entry) {
        entry->retained=dst;entry->vertices=*nverts;entry->count=count;
        entry->references=references;
        if (references) entry->refs=*t->refs;
        entry->source=source;
    }
    *indices = dst;
    return 1;
}
static int index_verify_equal(int r_old, int r_new, const void *ind_old, const void *ind_new, unsigned nv_old, unsigned nv_new,
    const index_retain_target *o, const index_retain_target *n, uint32_t used_before, unsigned count, unsigned references,
    char *why, size_t why_n)
{
    /* Pointers into the two pools are compared by offset; others (an earlier draw's retained copy) directly. */
    #define IN_POOL(tg, p) ((const uint16_t *)(p) >= (tg)->pool && (const uint16_t *)(p) < (tg)->pool + XV_FRAME_INDICES)
    #define SAME_PTR(po, pn) ((po) == (pn) || (IN_POOL(o, po) && IN_POOL(n, pn) && \
        (const uint16_t *)(po) - o->pool == (const uint16_t *)(pn) - n->pool))
    if (r_old != r_new) { snprintf(why, why_n, "result %d/%d", r_old, r_new); return 0; }
    if (*o->used != *n->used) { snprintf(why, why_n, "pool fill %u/%u", *o->used, *n->used); return 0; }
    if (*o->refs_valid != *n->refs_valid) { snprintf(why, why_n, "refs valid %d/%d", *o->refs_valid, *n->refs_valid); return 0; }
    if (!r_old) return 1;
    if (nv_old != nv_new) { snprintf(why, why_n, "vertices %u/%u", nv_old, nv_new); return 0; }
    if (!SAME_PTR(ind_old, ind_new)) { snprintf(why, why_n, "index pointer"); return 0; }
    if (*o->used != used_before && memcmp(o->pool + used_before, n->pool + used_before, count * sizeof(uint16_t))) {
        snprintf(why, why_n, "GPU index bytes"); return 0; }
    if (*o->refs_valid && memcmp(o->refs, n->refs, sizeof *o->refs)) { snprintf(why, why_n, "coverage"); return 0; }
    const xv_index_cache_entry *eo = o->entry, *en = n->entry;
    if (eo) {
        if (eo->source != en->source || eo->count != en->count || eo->vertices != en->vertices ||
            eo->references != en->references || !SAME_PTR(eo->retained, en->retained)) {
            snprintf(why, why_n, "cache entry"); return 0; }
        if (eo->source && memcmp(eo->mirror, en->mirror, eo->count * sizeof(uint16_t))) { snprintf(why, why_n, "mirror"); return 0; }
        if (eo->source && references && memcmp(&eo->refs, &en->refs, sizeof eo->refs)) { snprintf(why, why_n, "entry coverage"); return 0; }
    }
    #undef SAME_PTR
    #undef IN_POOL
    return 1;
}
static int retain_indices(const void **indices, unsigned count, unsigned *nverts)
{
    g_draw_vertex_refs_valid = 0;
    if (!*indices || !count) return 0;
    unsigned list = g_build_frame % XV_NUM_LISTS;
    uintptr_t p = (uintptr_t)*indices;
    uintptr_t quad = (uintptr_t)(g_quad_indices + list * XV_QUAD_INDICES);
    /* These two buffers already have sufficient lifetimes. */
    if (*indices == g_seq_indices) {
        if (count > XV_SEQ_INDICES) return 0;
        *nverts = count;
        return 1;
    }
    if (p >= quad && p < quad + XV_QUAD_INDICES * 2) {
        if (count > (quad + XV_QUAD_INDICES * 2 - p) / 2) return 0;
        *nverts = index_bounds(*indices, count);
        return 1;
    }
    g_index_requested[list] += ((uint64_t)count + 7u) & ~7ull;
    unsigned references=xv_vertex_references_enabled()!=0;
    xv_index_cache_entry *entry=NULL;
    uint64_t sub = xv_draw_profile_begin();
    if (index_reuse_enabled()) {
        entry=xv_index_cache_select(g_index_cache,*indices,count);
        if (!entry) index_reuse_ineligible++;
    }
    index_retain_target real = { entry, g_frame_indices + list * XV_FRAME_INDICES, &g_index_used[list],
                                 &g_draw_vertex_refs, &g_draw_vertex_refs_valid, 1 };
    int mode = xv_rec_opt_mode(&g_opt_index);
    if (mode == 2) return retain_new(indices, count, nverts, references, &real, &sub);
    if (mode == 0) return retain_old(indices, count, nverts, references, &real, &sub);
    /* verify: the new path on scratch copies of every output, then the original path for the recorded draw */
    static uint16_t *shadow_pool; static xv_index_cache_entry *shadow_entry; static xv_vertex_refs shadow_refs;
    if (!shadow_pool) shadow_pool = malloc(XV_FRAME_INDICES * sizeof *shadow_pool);
    if (!shadow_entry) shadow_entry = malloc(sizeof *shadow_entry);
    if (!shadow_pool || !shadow_entry) return retain_old(indices, count, nverts, references, &real, &sub);
    uint32_t used_before = g_index_used[list], shadow_used = used_before;
    int shadow_valid = 0;
    if (entry) memcpy(shadow_entry, entry, sizeof *shadow_entry);
    index_retain_target shadow = { entry ? shadow_entry : NULL, shadow_pool, &shadow_used, &shadow_refs, &shadow_valid, 0 };
    const void *shadow_indices = *indices; unsigned shadow_nverts = 0;
    int r_new = retain_new(&shadow_indices, count, &shadow_nverts, references, &shadow, NULL);
    int r_old = retain_old(indices, count, nverts, references, &real, &sub);
    char why[64] = "";
    if (xv_rec_opt_result(&g_opt_index, index_verify_equal(r_old, r_new, *indices, shadow_indices,
            r_old ? *nverts : 0, r_new ? shadow_nverts : 0, &real, &shadow, used_before, count, references, why, sizeof why)))
        XV_LOG("[rec-verify] XV_REC_INDEX mismatch frame %u cmd %u: %s (count %u references %u cache %d)\n",
            g_build_frame, cur_list()->ncmds, why, count, references, entry != NULL);
    return r_old;
}

static uint32_t geometry_hash(const void *data, unsigned bytes)
{
    const uint8_t *p = data;
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < bytes; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static unsigned changed_geometry(const cmd_t *c)
{
    unsigned changed = 0;
    for (unsigned s = 0; s <= XV_MAX_STREAMS; s++) {
        const void *p = s == XV_MAX_STREAMS ? c->indices : c->streams[s];
        if (c->geometry_bytes[s] && geometry_hash(p, c->geometry_bytes[s]) != c->geometry_hash[s]) changed |= 1u << s;
    }
    return changed;
}
/* Diagnose writes between recording and GPU completion, without retaining or
 * changing guest data. Only the explicitly requested histogram frames are hashed. */
void xv_d3d_check_geometry(uint32_t frame)
{
    if (frame == UINT32_MAX) return;
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    unsigned checked = 0, changed = 0;
    for (unsigned i = 0; i < l->ncmds; i++) {
        cmd_t *c = &l->cmds[i];
        if (!c->geometry_bytes[XV_MAX_STREAMS]) continue;
        checked++;
        unsigned mask = changed_geometry(c);
        if (mask) {
            if (changed++ < 16) XV_LOG("[geometry] frame %u cmd %u %s changed mask %X (indices bit %u)\n",
                frame, i, g_vs[c->vs].vs.desc->gxp, mask, XV_MAX_STREAMS);
        }
    }
    if (checked) XV_LOG("[geometry] frame %u checked %u draws, %u changed before GPU completion\n", frame, checked, changed);
}

/* Include all cube/2D and alpha variants, plus the heuristic fallback. The
 * cube-mode stages still determine variant identity even if a compiler removed
 * their sampler. Texture-coordinate scales below remain captured for all stages:
 * dependent reads can use another stage's coordinates without sampling it. */
static unsigned draw_texture_mask(const cmd_t *c)
{
    static int enabled = -1;
    static uint8_t fallback[FS_KINDS];
    if (enabled < 0) { const char *e = getenv("XV_UNUSED_TEXTURES"); enabled = !e || atoi(e) != 0; }
    if (!(unused_textures_override < 0 ? enabled : unused_textures_override) || c->fs_kind >= FS_KINDS) return 15;
    if (!fallback[c->fs_kind])
        fallback[c->fs_kind] = 16 | xv_fshader_embedded_texture_mask(FS_GXP[c->fs_kind]);
    unsigned mask = fallback[c->fs_kind] & 15;
    if (c->ps_entry < 0) return mask;
    if ((unsigned)c->ps_entry >= XV_PS_TABLE_COUNT) return 15;
    if (!g_ps_texture_masks[c->ps_entry]) {
        const xv_ps_entry_t *entry = &xv_ps_table[c->ps_entry];
        unsigned first = (unsigned)c->ps_entry, last = first;
        while (first && xv_ps_table[first-1].vs_fnv == entry->vs_fnv &&
               xv_ps_table[first-1].ps_key == entry->ps_key) first--;
        unsigned family_mask = 0;
        for (last = first; last < XV_PS_TABLE_COUNT &&
             xv_ps_table[last].vs_fnv == entry->vs_fnv && xv_ps_table[last].ps_key == entry->ps_key; last++) {
            const xv_ps_entry_t *e = &xv_ps_table[last];
            family_mask |= e->cube_modes | xv_fshader_embedded_texture_mask(e->gxp);
            size_t len = strlen(e->gxp);
            char variant[160];
            if (len < 9 || len + 4 > sizeof variant || strcmp(e->gxp + len - 9, ".frag.gxp")) family_mask |= 15;
            else {
                snprintf(variant, sizeof variant, "%.*s_na.frag.gxp", (int)(len - 9), e->gxp);
                family_mask |= xv_fshader_embedded_texture_mask(variant);
                if (e->ps_key == 0x154066FDu) {
                    snprintf(variant, sizeof variant, "%.*s_gt.frag.gxp", (int)(len - 9), e->gxp);
                    family_mask |= xv_fshader_embedded_texture_mask(variant);
                }
            }
        }
        for (unsigned i = first; i < last; i++) g_ps_texture_masks[i] = 16 | family_mask;
    }
    return mask | (g_ps_texture_masks[c->ps_entry] & 15);
}

/* The audited family returns unchanged tex0 alpha in every cube/alpha variant.
 * User shader replacements invalidate that dependency proof. Keep all uncertain
 * addressing modes and non-2D resources on the original alpha-test path. */
static int opaque_material_enabled(void)
{
    static int enabled = -1, compatible;
    if (enabled < 0) {
        const char *e = getenv("XV_OPAQUE_MATERIAL"); enabled = !e || atoi(e) != 0;
        e = getenv("XV_SHADER_OVERRIDE"); compatible = !e || atoi(e) == 0;
        e = getenv("XV_ALPHA_SPECIALIZE"); compatible &= !e || atoi(e) != 0;
    }
    return compatible && (opaque_material_override < 0 ? enabled : opaque_material_override);
}
static int opaque_material_candidate(const cmd_t *c)
{
    if (c->ps_entry < 0 || (unsigned)c->ps_entry >= XV_PS_TABLE_COUNT ||
        xv_ps_table[c->ps_entry].ps_key != 0x154066FDu ||
        !(c->atest & (1u << 16)) || ((c->atest >> 8) & 7u) == 7 ||
        !xv_alpha_accepts_opaque(c->atest)) return 0;
    for (unsigned i = 0; i < 2; i++) {
        unsigned mode = i ? S.tex_addr_v[0] : S.tex_addr_u[0];
        if (mode != X_D3DTADDRESS_WRAP && mode != X_D3DTADDRESS_MIRROR &&
            mode != X_D3DTADDRESS_CLAMP && mode != X_D3DTADDRESS_CLAMPTOEDGE) return 0;
    }
    return 1;
}

/* One-frame remote diagnostics use captured descriptors, never guest texture
 * bytes or vertex readback. Keep packed/resident geometry and submission intact.
 * Constants are limited to projection rows used by CE's shadow producer and
 * receiver, and emitted only for draws that write/read an offscreen target. */
static void trace_draw_state(const cmd_t *c, const xv_vs_desc_t *d, unsigned texok)
{
    /* Bounded capture for loading draws that can pass between remote requests.
     * Never enables vertex readback, changes shader selection or reads pixels. */
    static int loading_enabled=-1;
    static unsigned loading_seen[2],loading_first[2];
    static const unsigned loading_offsets[4]={0,30,120,600};
    if(loading_enabled<0) {const char *e=getenv("XV_LOADING_TRACE");loading_enabled=e&&atoi(e)!=0;}
    int loading_kind=S.ps_key==0xC4B1822Bu?0:S.ps_key==0xC61481BCu?1:-1;
    int loading=0;
    if(loading_enabled&&loading_kind>=0&&loading_seen[loading_kind]<4) {
        if(!loading_seen[loading_kind])loading_first[loading_kind]=g_build_frame;
        loading=(uint32_t)(g_build_frame-loading_first[loading_kind])>=loading_offsets[loading_seen[loading_kind]];
    }
    if (!vertex_trace_frame()&&!loading) return;
    if(loading) {
        loading_seen[loading_kind]++;
        XV_LOG("[loading-state] frame %u sample %u key %08X entry %d blend-slot %u op %u; diagnostic frame, exclude timing\n",
            g_build_frame,loading_seen[loading_kind],S.ps_key,c->ps_entry,c->blend,S.blend_op);
        XV_LOG("[loading-attributes] frame %u color %.9g/%.9g/%.9g/%.9g tex1 %.9g/%.9g\n",
            g_build_frame,S.const_attr[3][0],S.const_attr[3][1],S.const_attr[3][2],S.const_attr[3][3],
            S.const_attr[10][0],S.const_attr[10][1]);
        const cmdlist_t *list=cur_list();
        for(unsigned i=0;i<list->ncmds&&i<8;i++) {
            const cmd_t *before=&list->cmds[i];
            if(before->kind==1) XV_LOG("[loading-clear] frame %u cmd %u pass %u flags %X color %08X\n",
                g_build_frame,i,before->pass,before->clear_flags,before->clear_color);
        }
    }
    unsigned command = cur_list()->ncmds - 1, rt_mask = 0;
    XV_LOG("[draw-state] frame %u cmd %u pass %u vs %s ps %08X key %08X tex-mask %X previous %X blend %u/%u/%u z %u/%u/%u mask %X atest %08X\n",
        g_build_frame, command, c->pass, d->gxp, S.ps_hash, S.ps_key, texok,
        c->previous_frame, S.blend_enable, S.src_blend, S.dst_blend,
        S.z_enable, S.z_write, S.z_func, S.color_mask, c->atest);
    for (unsigned t = 0; t < 4; ++t) {
        if (!S.tex_guest[t] && !(texok & (1u << t))) continue;
        XV_LOG("[draw-sampler] frame %u cmd %u stage %u guest %08X captured %u address %u/%u filter %u/%u scale %.9g/%.9g\n",
            g_build_frame, command, t, S.tex_guest[t], (texok >> t) & 1u,
            S.tex_addr_u[t], S.tex_addr_v[t], S.tex_min[t], S.tex_mag[t],
            c->texscale[t][0], c->texscale[t][1]);
        if(loading) XV_LOG("[loading-sampler] frame %u stage %u border %08X\n",g_build_frame,t,S.tex_border[t]);
        if (!(texok & (1u << t))) continue; /* Uncaptured slots are not descriptors. */
        const SceGxmTexture *tx = &c->tex[t];
        if(loading) XV_LOG("[loading-format] frame %u stage %u format %08X\n",g_build_frame,t,(unsigned)sceGxmTextureGetFormat(tx));
        const void *data = sceGxmTextureGetData(tx);
        for (unsigned r = 0; r < XV_RT_SLOTS; ++r)
            if (data && data == g_rt[r].mem) rt_mask |= 1u << r;
        XV_LOG("[draw-texture] frame %u cmd %u stage %u data %08X size %u/%u type %08X address %u/%u\n",
            g_build_frame, command, t, (unsigned)(uintptr_t)data,
            (unsigned)sceGxmTextureGetWidth(tx), (unsigned)sceGxmTextureGetHeight(tx),
            (unsigned)sceGxmTextureGetType(tx), (unsigned)sceGxmTextureGetUAddrMode(tx),
            (unsigned)sceGxmTextureGetVAddrMode(tx));
    }
    if (c->pass || rt_mask) {
        static const unsigned rows[] = {0,1,2,3,15,16,17,18,19,28,29,30,31};
        for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; ++i) {
            unsigned row = rows[i]; const float *v = S.vsc[96 + row];
            XV_LOG("[draw-projection] frame %u cmd %u read-rt %X c[%u] %.9g %.9g %.9g %.9g\n",
                g_build_frame, command, rt_mask, row, v[0], v[1], v[2], v[3]);
        }
    }
}

static unsigned record_textures(cmd_t *c, const xv_vs_desc_t *d, int immediate)
{
    /* textures */
    unsigned texok = 0, mask = draw_texture_mask(c);
    int candidate = opaque_material_candidate(c);
    opaque_candidates += candidate;
    c->opaque_alpha = 0;
    for (unsigned t = 0; t < 4; ++t) {
        if (!S.tex_guest[t])
            continue;
        if (!(mask & (1u << t))) { texture_stages_skipped++; continue; }
        texture_stages_prepared++;
#ifdef XV_RUN_RECOMP
        if (immediate) {
            extern uint32_t xd3d_backbuffer_data(void);
            uint32_t data = ((const X_D3DResource *)xv_guest_ptr(S.tex_guest[t]))->Data;
            if (data && data == xd3d_backbuffer_data()) {
                c->previous_frame |= (uint8_t)(1u << t);
                c->ntex = (uint8_t)(t + 1);
                continue;
            }
        }
#endif
        const SceGxmTexture *tex = texture_for(t);
        if (tex) {
            c->tex[t] = *tex;
            c->ntex = (uint8_t)(t + 1);
            texok |= 1u << t;
#ifdef XV_RUN_RECOMP
            if (!t && candidate && opaque_material_enabled() &&
                sceGxmTextureGetType(tex) != SCE_GXM_TEXTURE_CUBE &&
                xv_ui_gxm_texture_opaque(texture_source[0])) {
                c->opaque_alpha = 1; opaque_proven++;
            }
#endif
        }
    }
    c->loading_border_color = S.tex_border[1];
    c->loading_border_axes = (S.tex_addr_u[1] == X_D3DTADDRESS_BORDER ? 1 : 0) |
        (S.tex_addr_v[1] == X_D3DTADDRESS_BORDER ? 2 : 0);
    for (unsigned t = 0; t < 4; t++) {
        c->texscale[t][0] = c->texscale[t][1] = 1.0f;
        if (S.tex_guest[t]) {
            const uint32_t *header = (const uint32_t *)xv_guest_ptr(S.tex_guest[t]);
            uint32_t size = header[4];
            if (size) {
                /* XV_REC_DRAW: the scale is a pure function of the size word; keep the last one per stage. */
                static uint32_t memo_size[4]; static float memo_scale[4][2];
                int mode = g_draw_mode;
                if (mode == 2 && memo_size[t] == size) {
                    c->texscale[t][0] = memo_scale[t][0]; c->texscale[t][1] = memo_scale[t][1];
                    continue;
                }
                c->texscale[t][0] = 1.0f / ((size & 0xFFF) + 1);
                c->texscale[t][1] = 1.0f / (((size >> 12) & 0xFFF) + 1);
                if (mode == 1 && memo_size[t] == size &&
                    xv_rec_opt_result(&g_opt_draw, !memcmp(memo_scale[t], c->texscale[t], sizeof memo_scale[t])))
                    draw_memo_mismatch("texture scale", size, t);
                if (mode > 0) { memo_size[t] = size; memo_scale[t][0] = c->texscale[t][0]; memo_scale[t][1] = c->texscale[t][1]; }
            }
        }
    }
    if (c->ps_entry >= 0) {
        const xv_ps_entry_t *e = &xv_ps_table[c->ps_entry];
        unsigned c2d = 0;
        for (unsigned t = 0; t < 4; ++t)
            if ((e->cube_modes & texok & (1u << t)) &&
                sceGxmTextureGetType(&c->tex[t]) != SCE_GXM_TEXTURE_CUBE) c2d |= 1u << t;
        if (c2d) {
            int variant = ps_entry_memo(d->func_hash, e->ps_key, c2d);
            if (variant >= 0) c->ps_entry = (int16_t)variant;
        }
    }
    return texok;
}

static unsigned record_material(cmd_t *c, const xv_vs_desc_t *d, int immediate)
{
    c->depth_prepared = depth_prepare_enabled() &&
        xv_depth_proof_read(&g_depth_proofs, depth_prepare_key(c));
    if (c->depth_prepared) {
        /* new_cmd zeroed textures/previous_frame. This immutable command will
         * use the proved constant fragment even if the option changes later. */
        depth_prepare_hits++;
        return 0;
    }
    return record_textures(c, d, immediate);
}

static int vertex_reference_layout_scan(const xv_vs_desc_t *d, unsigned stream, unsigned stride);
/* Declarations are immutable: memoize (declaration, stream, stride). */
static int vertex_reference_layout(const xv_vs_desc_t *d, unsigned stream, unsigned stride)
{
    int mode = g_draw_mode;
    if (mode <= 0) return vertex_reference_layout_scan(d, stream, stride);
    static struct { const xv_vs_desc_t *d; unsigned stream, stride; int result; } memo[64];
    unsigned m = (unsigned)(((uintptr_t)d >> 3) * 2654435761u + stream * 40503u + stride * 97u) >> 26 & 63u;
    if (memo[m].d == d && memo[m].stream == stream && memo[m].stride == stride) {
        if (mode == 2) return memo[m].result;
        int r = vertex_reference_layout_scan(d, stream, stride);
        if (xv_rec_opt_result(&g_opt_draw, r == memo[m].result)) draw_memo_mismatch("reference layout", (unsigned)memo[m].result, (unsigned)r);
        return r;
    }
    int r = vertex_reference_layout_scan(d, stream, stride);
    memo[m].d = d; memo[m].stream = stream; memo[m].stride = stride; memo[m].result = r;
    return r;
}
static int vertex_reference_layout_scan(const xv_vs_desc_t *d, unsigned stream, unsigned stride)
{
    if (!stride || stream >= d->nstreams || stride != d->stride[stream]) return 0;
    for (unsigned i = 0; i < d->nattrs; i++) {
        const xv_attr_desc_t *a = &d->attrs[i];
        if (a->stream != stream) continue;
        unsigned size;
        switch (a->format) {
        case SCE_GXM_ATTRIBUTE_FORMAT_U8: case SCE_GXM_ATTRIBUTE_FORMAT_S8:
        case SCE_GXM_ATTRIBUTE_FORMAT_U8N: case SCE_GXM_ATTRIBUTE_FORMAT_S8N: size = 1; break;
        case SCE_GXM_ATTRIBUTE_FORMAT_U16: case SCE_GXM_ATTRIBUTE_FORMAT_S16:
        case SCE_GXM_ATTRIBUTE_FORMAT_U16N: case SCE_GXM_ATTRIBUTE_FORMAT_S16N:
        case SCE_GXM_ATTRIBUTE_FORMAT_F16: size = 2; break;
        case SCE_GXM_ATTRIBUTE_FORMAT_F32: size = 4; break;
        default: return 0;
        }
        if (!a->components || a->components > 4 ||
            a->offset + size * a->components > stride) return 0;
    }
    return 1;
}

static void draw_memo_mismatch(const char *what, unsigned memo, unsigned live)
{
    XV_LOG("[rec-verify] XV_REC_DRAW mismatch frame %u cmd %u: %s memo %u live %u\n", g_build_frame, cur_list()->ncmds, what, memo, live);
}
static void record_draw_body(uint32_t prim, uint32_t count, const void *indices, uint32_t base_vertex, const void *immediate);
static void record_draw(uint32_t prim, uint32_t count, const void *indices, uint32_t base_vertex, const void *immediate)
{
    g_draw_mode = xv_rec_opt_mode(&g_opt_draw);
    if (g_draw_mode > 0) { g_draw_trace = trace_frame_live(); g_draw_vtrace = vertex_trace_frame_live(); }
    record_draw_body(prim, count, indices, base_vertex, immediate);
    g_draw_trace = g_draw_vtrace = -1; g_draw_mode = 0;
}
static void record_draw_body(uint32_t prim, uint32_t count, const void *indices, uint32_t base_vertex, const void *immediate)
{
    uint64_t profile = xv_draw_profile_begin();
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
    /* Halo rebuilds visible-world indices as the camera turns, including while
     * the preceding frame is in flight. Holding the guest pointer mixes that
     * new triangle list with the preceding draw's vertices and camera. */
    unsigned nverts;
    if (!retain_indices(&indices, count, &nverts)) {
        cur_list()->ncmds--; cur_list()->dropped++; cur_list()->drop_indices++;
        return;
    }
    /* Range-only remote capture also uses this record, without the much more
     * expensive full histogram or changing packed vertex admission. */
    if (vertex_trace_frame()) {
        unsigned lo=65535,hi=0;
        const uint8_t *ix=indices;
        for(unsigned i=0;i<count;i++) {
            uint16_t value;memcpy(&value,ix+2u*i,2);
            if(value<lo)lo=value;if(value>hi)hi=value;
        }
        XV_LOG("[index-range] frame %u cmd %u prim %u count %u min %u max %u retained %u base %u immediate %u\n",
            g_build_frame,cur_list()->ncmds-1,prim,count,lo,hi,nverts,base_vertex,immediate!=NULL);
    }
    vs_slot_t *v = &g_vs[slot];
    xv_draw_profile_step(XV_DRAW_INDICES, &profile);
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
    static int pspair_log = -1; if (pspair_log < 0) { const char *e = getenv("XV_PSPAIR_LOG"); pspair_log = e ? atoi(e) != 0 : 0; }   /* offline input only: the 256-entry scan ran on every draw */
    if (pspair_log) {   /* which (vertex program, combiner program) pairs the game actually draws with: offline input */
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
    /* The immediate UI bridge recognizes these exact active combiner programs.
     * Use their equivalent built-in shaders with the captured blend state; the
     * font fallback whitened black radar texels and tinted the copy pass green. */
    int simple_ui = (d->func_hash == 0x1DAF0284u || d->func_hash == 0x4469E1F8u) &&
                    (S.ps_hash == 0x1A42D493u || S.ps_hash == 0x6C94962Bu);
    if (simple_ui) c->fs_kind = S.ps_hash == 0x1A42D493u ? FS_TEX0 : FS_TEXMOD;
    c->ps_entry = (int16_t)ps_entry_memo(d->func_hash, S.ps_key, 0);
    if (!simple_ui && c->ps_entry < 0 && c->fs_kind == FS_TEXMOD) {
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
    c->stencil = S.stencil;
    c->cull = (uint8_t)S.cull;
    { extern uint32_t xd3d_fog_color(void); c->fog_color = xd3d_fog_color(); }
    { extern uint32_t xd3d_alpha_test(void); c->atest = xd3d_alpha_test(); }
    { static const xv_vs_desc_t *bsp_d; static int bsp_is; if (d != bsp_d) { bsp_d = d; bsp_is = strstr(d->gxp, "halo_vs_16") != NULL; } if (bsp_is) xv_d3d_bsp_acc++; }   /* was a strstr per draw */
    for (unsigned a = 0; a < d->nattrs; a++) if (d->attrs[a].stream == XV_CONST_STREAM) {
        if (!snapshot_attributes(c)) {
            cur_list()->ncmds--; cur_list()->dropped++; cur_list()->drop_attributes++;
            return;
        }
        break;
    }

    xv_draw_profile_step(XV_DRAW_PROGRAM, &profile);
    /* Resolve all sources before handing upload-pool ownership to core 0.
     * Queued preparation owns copies; the synchronous fallback joins its
     * borrowed source loan before allowing guest execution to resume. */
    _Static_assert(XV_MAX_STREAMS <= XV_VERTEX_PREPARE_STREAMS, "stream batch capacity");
    xv_vertex_prepare_batch prep = {.slot=g_build_frame % XV_NUM_LISTS};
    unsigned prep_stream[XV_VERTEX_PREPARE_STREAMS];
    for (unsigned s = 0; s < d->nstreams; ++s) {
        if (immediate && s == 0) { c->streams[s] = immediate; continue; }
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
        const xv_vertex_refs *refs = g_draw_vertex_refs_valid &&
            vertex_reference_layout(d, s, stride) ? &g_draw_vertex_refs : NULL;
        if (!stride || nverts > UINT32_MAX / stride || prep.count==XV_VERTEX_PREPARE_STREAMS) {
            cur_list()->ncmds--; cur_list()->dropped++; cur_list()->drop_attributes++;
            return;
        }
        prep_stream[prep.count]=s;
        prep.streams[prep.count++]=(xv_vertex_prepare_stream){
            .source=p,.bytes=nverts*stride,.stride=stride,.refs=refs};
#if XV_PACKED_VERTEX_LAYOUT
        /* Keep the already-qualified sparse comparison when it can avoid
         * scanning most records; this prototype compares the whole prefix. */
        if(s==0 && !immediate && !trace_frame() && stride==32 && v->vs.packed_vprog &&
           !xv_vertex_refs_sparse(refs,nverts*stride,stride)) {
            prep.streams[prep.count-1].packed=XV_PACKED_PREFIX16;
            c->packed_vertex=XV_PACKED_PREFIX16;
        }
#endif
        if (vertex_trace_frame()) XV_LOG("[hist] stream %u vb %08X common %08X data %08X vertices %u stride %u\n",
            s, S.stream_guest[s], vb->Common, vb->Data, nverts, stride);
        /* Only the owned upload is published; the cached source stays on CPU. */
    }
    uint64_t sub = xv_draw_profile_begin();
    if (indices)
        xv_gpu_flush(indices, count * 2);
    xv_draw_profile_step(XV_DRAW_FLUSH, &sub);
    static int capture_diagnostics=-1;
    if(capture_diagnostics<0)capture_diagnostics=getenv("XV_SKIN_DUMP") || getenv("XV_FOG_DUMP") ||
        getenv("XV_MESH_DUMP") || getenv("XV_DUMP_VS");
    const void **capture_targets[XV_VERTEX_PREPARE_STREAMS];
    for(unsigned i=0;i<prep.count;i++)capture_targets[i]=&c->streams[prep_stream[i]];
    int captured=!trace_frame() && !capture_diagnostics &&
        (!prep.count || xv_vertex_capture_submit(&prep,capture_targets,captured_draw_complete,c));
    if(!captured) {
        xv_vertex_capture_drain();
        xv_vertex_prepare_begin(&prep);
    }
    xv_draw_profile_step(XV_DRAW_SUBMIT, &sub);
    xv_draw_profile_step(XV_DRAW_STREAMS, &profile);
    /* constants: snapshot the window this program reads.  Consecutive draws with unchanged c[] share one
     * snapshot (Halo draws hundreds of BSP pieces per frame with full-window programs: 768 floats each) */
    cmdlist_t *l = cur_list();
    if (v->vs.p_c) {
        if (l->nconsts && l->last_gen == S.vsc_gen && l->last_base == d->c_base && l->last_n == d->c_count) {
            c->const_off = l->last_off; c->const_n = d->c_count;
        } else if (l->nconsts + d->c_count * 4 <= XV_CONST_POOL) {
            c->const_off = l->nconsts / 4;
            c->const_n = d->c_count;
            xv_constant_window_copy(&l->consts[l->nconsts], S.vsc, d->c_base, d->c_count);
            l->nconsts += d->c_count * 4u;
            l->last_gen = S.vsc_gen; l->last_off = c->const_off; l->last_n = d->c_count; l->last_base = d->c_base;
        } else {
            l->const_dropped++;               /* rejected during replay; never reuse stale constants */
        }
    }

    xv_draw_profile_step(XV_DRAW_CONSTANTS, &profile);
    unsigned texok = record_material(c, d, immediate != NULL);
    xv_draw_profile_step(XV_DRAW_TEXTURES, &profile);
    /* Descriptor/constant state is already captured even when vertex streams
     * are still worker-owned. Trace both preparation paths without joining. */
    trace_draw_state(c, d, texok);
    if(captured) {
        /* Worker inputs are private, and the command cannot be submitted until
         * its streams are resolved by the recording owner's frame drain. */
        xv_draw_profile_step(XV_DRAW_STREAMS,&profile);
        xv_draw_profile_step(XV_DRAW_DIAGNOSTICS,&profile);
        return;
    }
    if (!xv_vertex_prepare_finish(&prep)) {
        cur_list()->ncmds--; cur_list()->dropped++; cur_list()->drop_attributes++;
        return;
    }
    for (unsigned i=0;i<prep.count;i++) c->streams[prep_stream[i]]=prep.streams[i].result;
    /* This stage now measures owner setup plus its remaining join, not worker
     * execution. The worker reports its overlapping duration separately. */
    xv_draw_profile_step(XV_DRAW_STREAMS, &profile);
    if (geometry_trace(c)) {
        /* Opt-in capture for offline skin/mesh diagnosis. Defaults to the first
         * requested histogram frame; allow up to four for lighting A/B captures. */
        const char *dump_vs = getenv("XV_MESH_DUMP");
        static uint32_t dump_frame = UINT32_MAX;
        static unsigned dump_frames;
        if (dump_vs && strstr(d->gxp, dump_vs) && c->streams[0]) {
            const char *frames = getenv("XV_MESH_DUMP_FRAMES");
            int limit = frames ? atoi(frames) : 1;
            if (limit < 1) limit = 1;
            if (limit > 4) limit = 4;
            if (dump_frame != g_build_frame && dump_frames < (unsigned)limit) {
                dump_frame = g_build_frame; dump_frames++;
            }
            unsigned stride = S.stream_stride[0] ? S.stream_stride[0] : d->stride[0];
            if (immediate) stride = d->stride[0]; /* packed UI/flare layout, not the prior VB */
            if (dump_frame == g_build_frame && stride && nverts <= (4u * 1024 * 1024) / stride) {
                char path[128];
                snprintf(path, sizeof path, "ux0:data/xita/mesh_%u_%u.bin", g_build_frame, cur_list()->ncmds - 1);
                FILE *f = fopen(path, "wb");
                if (f) {
                    uint32_t header[] = { 0x4853454D, d->func_hash, S.ps_hash, nverts, stride, count, prim, c->atest };
                    fwrite(header, sizeof header, 1, f);
                    fwrite(S.vsc, sizeof S.vsc, 1, f);
                    fwrite(S.psc, sizeof S.psc, 1, f);
                    fwrite(xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]), stride, nverts, f);
                    fwrite(indices, 2, count, f);
                    fclose(f);
                }
            }
        }
        for (unsigned s = 0; s < d->nstreams; s++) {
            unsigned stride = S.stream_stride[s] ? S.stream_stride[s] : d->stride[s];
            if (immediate && s == 0) stride = d->stride[s];
            if (c->streams[s] && stride && nverts <= (4u * 1024 * 1024) / stride) {
                c->geometry_bytes[s] = nverts * stride;
                c->geometry_hash[s] = geometry_hash(xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[s]), c->geometry_bytes[s]);
            }
        }
        if (indices && count <= 65536) {
            c->geometry_bytes[XV_MAX_STREAMS] = count * 2;
            c->geometry_hash[XV_MAX_STREAMS] = geometry_hash(indices, count * 2);
        }
    }

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
        if (dump && c->streams[0] && shown < 160
#if XV_PACKED_VERTEX_LAYOUT
            && !c->packed_vertex
#endif
            ) {
            int skinned = 0;
            for (unsigned a = 0; a < d->nattrs; ++a) if (d->attrs[a].offset == 28 && d->attrs[a].format == SCE_GXM_ATTRIBUTE_FORMAT_U8N) skinned = 1;
            const uint8_t *vb0 = (const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]);
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
                XV_LOG("[skin] BIG guest vb %08X (+%u) odd=%u:%s\n", S.stream_guest[0], (unsigned)((const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]) - (const uint8_t *)xv_guest_ptr(S.stream_guest[0])), odd, ob);
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

    xv_draw_profile_step(XV_DRAW_DIAGNOSTICS, &profile);
    if (geometry_trace(c)) {
        XV_LOG("[stencil] cmd %u vs %s enable %u func %u ref %u mask %02X/%02X ops %u/%u/%u\n",
            cur_list()->ncmds-1, d->gxp, c->stencil.enabled, c->stencil.func,
            c->stencil.ref, c->stencil.read_mask, c->stencil.write_mask,
            c->stencil.fail, c->stencil.depth_fail, c->stencil.pass);
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
            unsigned st = immediate ? d->stride[0] : (S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]);
            char zb[400] = {0}; int k = 0;
            for (unsigned i = 0; st >= 3 * sizeof(float) && i < nverts && i < 6 && k < 360; ++i) {
                const float *p = (const float *)((const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]) + i * st); float v[4] = { p[0], p[1], p[2], 1.0f }, w[4];
                for (int r = 0; r < 3; ++r) w[r] = S.vsc[60 + r][0] * v[0] + S.vsc[60 + r][1] * v[1] + S.vsc[60 + r][2] * v[2] + S.vsc[60 + r][3];   /* node 0 */
                w[3] = 1.0f; float o[4];
                for (int r = 0; r < 4; ++r) o[r] = S.vsc[r][0] * w[0] + S.vsc[r][1] * w[1] + S.vsc[r][2] * w[2] + S.vsc[r][3] * w[3];
                k += snprintf(zb + k, sizeof zb - k, " [%.2f %.2f %.7f %.2f]", o[0] / o[3], o[1] / o[3], o[2] / o[3], o[3]);
            }
            XV_LOG("[hist]   zoff clip (x/w y/w z/w w):%s\n", zb);
            XV_LOG("[hist]   zoff state mask %X blend %u atest %08X rows z %.7g %.7g %.7g %.7g w %.7g %.7g %.7g %.7g\n",
                   S.color_mask, c->blend, c->atest,
                   S.vsc[2][0], S.vsc[2][1], S.vsc[2][2], S.vsc[2][3],
                   S.vsc[3][0], S.vsc[3][1], S.vsc[3][2], S.vsc[3][3]);
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
    if (geometry_trace(c) && getenv("XV_HIST_CONSTS")) {      /* XV_HIST_CONSTS="12,13,14,24,26": D3D vertex constant registers to print per draw */
        char cb[512]; int n = 0; const char *e = getenv("XV_HIST_CONSTS");
        while (*e && n < 440) { int reg = atoi(e); const float *m = S.vsc[96 + (reg < -96 ? -96 : reg > 95 ? 95 : reg)];
            n += snprintf(cb + n, sizeof cb - n, " c[%d]=%.3f,%.3f,%.3f,%.3f", reg, m[0], m[1], m[2], m[3]);
            while (*e && *e != ',') e++; if (*e == ',') e++; }
        XV_LOG("[hist]   consts:%s\n", cb);
    }
    if (geometry_trace(c) && getenv("XV_DUMP_VS") && strstr(d->gxp, getenv("XV_DUMP_VS")) && d->nstreams > 1 && c->streams[1]) {
        /* raw stream-1 bytes of the first vertices (lightmap uv / normal packing checks) */
        unsigned st = S.stream_stride[1] ? S.stream_stride[1] : d->stride[1]; char b[200]; int n = 0;
        for (unsigned k = 0; k < 4 && n < 180; ++k) { const uint8_t *p = (const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[1]) + k * st; n += snprintf(b + n, sizeof b - n, " |"); for (unsigned j = 0; j < st && j < 16; ++j) n += snprintf(b + n, sizeof b - n, " %02X", p[j]); }
        XV_LOG("[hist]   stream1 stride %u (decl %u) guest %08X:%s\n", st, d->stride[1], S.stream_guest[1], b);
    }
    if (geometry_trace(c) && getenv("XV_DUMP_VS") && strstr(d->gxp, getenv("XV_DUMP_VS")) && c->streams[0]) {
        unsigned st = S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]; char b[300]; int n = 0;
        for (unsigned k = 0; k < 3 && n < 280; ++k) { const uint8_t *p = (const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]) + k * st; const float *f = (const float *)p;
            n += snprintf(b + n, sizeof b - n, " | pos %.2f %.2f %.2f uv@24 %.3f %.3f raw", f[0], f[1], f[2], f[6], f[7]);
            for (unsigned j = 12; j < st && j < 32; ++j) n += snprintf(b + n, sizeof b - n, " %02X", p[j]); }
        XV_LOG("[hist]   stream0 stride %u (decl %u):%s\n", st, d->stride[0], b);
    }
    if (geometry_trace(c) && strstr(d->gxp, getenv("XV_DUMP_VS") ? getenv("XV_DUMP_VS") : "\001") && c->streams[0]) {
        /* first three vertices through the c[0..3] rows the microcode uses for oPos (dph) */
        for (unsigned k = 0; k < 3; ++k) {
            const uint16_t *ix = (const uint16_t *)indices; unsigned vi = ix ? ix[k] : k;
            const float *p = (const float *)((const uint8_t *)xv_vertex_upload_readback(g_build_frame % XV_NUM_LISTS, c->streams[0]) + vi * (S.stream_stride[0] ? S.stream_stride[0] : d->stride[0]));
            float o[4]; for (unsigned r = 0; r < 4; ++r) { const float *m = S.vsc[96 + d->c_base + r]; o[r] = p[0] * m[0] + p[1] * m[1] + p[2] * m[2] + m[3]; }
            XV_LOG("[hist]   v%u idx %u pos %.2f %.2f %.2f -> clip %.2f %.2f %.2f w %.2f (ndc %.2f %.2f z %.3f)\n", k, vi, p[0], p[1], p[2], o[0], o[1], o[2], o[3], o[0] / o[3], o[1] / o[3], o[2] / o[3]);
        }
        for (unsigned r = 0; r < 4; ++r) { const float *m = S.vsc[96 + d->c_base + r]; XV_LOG("[hist]   c[%d] %.3f %.3f %.3f %.3f\n", d->c_base + (int)r, m[0], m[1], m[2], m[3]); }
        { const uint16_t *ix = (const uint16_t *)indices; char b[200]; int n = 0;
          for (unsigned k = 0; ix && k < 24 && k < count && n < 180; ++k) n += snprintf(b + n, sizeof b - n, " %u", ix[k]);
          XV_LOG("[hist]   indices @%p (guest ib %08X base %u):%s\n", indices, S.indices_dbg, base_vertex, b); }
    }
    if (geometry_trace(c))
        XV_LOG("[hist] cmd %u: draw %s ps %08X prim %u n %u base %u tex %08X/%08X/%08X/%08X ntex %u blend %u/%u%s z %u/%u cull %u c[%d..%d] c0 %.2f %.2f %.2f %.2f query %u color-mask %X\n",
               l->ncmds - 1, d->gxp, S.ps_hash, prim, count, base_vertex, S.tex_guest[0], S.tex_guest[1], S.tex_guest[2], S.tex_guest[3], c->ntex, S.src_blend, S.dst_blend, S.blend_enable ? "" : "(off)",
               S.z_enable, S.z_write, S.cull, d->c_base, d->c_base + (int)d->c_count, S.vsc[96 + d->c_base][0], S.vsc[96 + d->c_base][1], S.vsc[96 + d->c_base][2], S.vsc[96 + d->c_base][3], (unsigned)c->visibility, S.color_mask);
    xv_draw_profile_step(XV_DRAW_DIAGNOSTICS, &profile);
}

void xv_d3d_DrawVertices(uint32_t prim, uint32_t start_vertex, uint32_t vertex_count)
{
    if (vertex_count > XV_SEQ_INDICES)
        vertex_count = XV_SEQ_INDICES;
    record_draw(prim, vertex_count, g_seq_indices, start_vertex, NULL);
}

void xv_d3d_DrawIndexedVertices(uint32_t prim, uint32_t vertex_count, uint32_t indices_guest)
{
    record_draw(prim, vertex_count, xv_guest_ptr(indices_guest), 0, NULL);
}
void xv_d3d_DrawIndexedVerticesBase(uint32_t prim, uint32_t index_count, uint32_t indices_guest, uint32_t base_vertex)
{
    S.indices_dbg = indices_guest;
    record_draw(prim, index_count, xv_guest_ptr(indices_guest), base_vertex, NULL);
}

void xv_d3d_DrawImmediate(uint32_t prim, const void *vertices, uint32_t count)
{
    xv_d3d_DrawImmediateStrided(prim, vertices, count, XV_IM_STRIDE);
}
void xv_d3d_DrawImmediateStrided(uint32_t prim, const void *vertices, uint32_t count, uint32_t stride)
{
    int slot = handle_to_slot(S.vs_handle);
    if (!vertices || !count || !stride || stride > XV_IM_STRIDE || recording_dropped() || slot < 0) return;
    const xv_vs_desc_t *desc = g_vs[slot].vs.desc;
    if (!desc || desc->nstreams != 1 || desc->stride[0] != stride) return;
    void *dst=retain_immediate_bytes(vertices,(uint64_t)count*stride);
    if (!dst) {
        cur_list()->dropped++; cur_list()->drop_immediate++;
        return;
    }
    /* Uncached GPU-mapped scratch, retained until this frame's fence completes. */
    record_draw(prim, count, g_seq_indices, 0, dst);
}

/* Recomp-build helpers: the kernel's D3D object model (recomp/kernel/xd3d.c) owns the state and hands
 * it over per draw, so it needs handle lookup by microcode hash, bulk constants, and a frame boundary
 * that does not present (the game's own Present does). */
static uint32_t handle_for_hash_scan(uint32_t fnv);
/* Registration only appends slots and never rewrites one: the first slot with this microcode hash is permanent. */
uint32_t xv_d3d_handle_for_hash(uint32_t fnv)
{
    int mode = xv_rec_opt_mode(&g_opt_draw);
    if (mode <= 0) return handle_for_hash_scan(fnv);
    static struct { uint32_t fnv, handle; } memo[32];
    unsigned m = fnv * 2654435761u >> 27;
    if (memo[m].handle && memo[m].fnv == fnv) {
        if (mode == 2) return memo[m].handle;
        uint32_t h = handle_for_hash_scan(fnv);
        if (xv_rec_opt_result(&g_opt_draw, h == memo[m].handle)) draw_memo_mismatch("vertex program handle", memo[m].handle, h);
        return h;
    }
    uint32_t h = handle_for_hash_scan(fnv);
    if (h) { memo[m].fnv = fnv; memo[m].handle = h; }
    return h;
}
static uint32_t handle_for_hash_scan(uint32_t fnv)
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
void xv_d3d_SetAllConstants(const float (*vsc)[4])
{
    S.vsc_source = NULL;
    scan_constant_checks++; scan_constant_bytes += sizeof S.vsc;
    int equal = draw_scan_neon() ? xv_bytes_equal(S.vsc, vsc, sizeof S.vsc) :
                                  !memcmp(S.vsc, vsc, sizeof S.vsc);
    if (equal) { scan_constant_reused++; return; }
    memcpy(S.vsc, vsc, sizeof S.vsc);
    S.vsc_gen++;
}
void xv_d3d_SetTrackedConstants(const float (*vsc)[4], uint32_t *dirty_lo, uint32_t *dirty_hi)
{
    unsigned lo = *dirty_lo, hi = *dirty_hi;
    /* Only the producer knows whether unchanged rows are still identical.
     * A different producer or an intervening UI/generic write requires a full
     * synchronization, even when the producer has no new dirty registers. */
    if (S.vsc_source != vsc || lo > 192 || hi > 192 ||
        (lo > hi && !(lo == 192 && hi == 0))) {
        xv_d3d_SetAllConstants(vsc);
    } else {
        size_t bytes = hi > lo ? (size_t)(hi - lo) * 16u : 0;
        scan_constant_checks++; scan_constant_bytes += bytes;
        int equal = !bytes || (draw_scan_neon() ? xv_bytes_equal(S.vsc[lo], vsc[lo], bytes) :
                                                 !memcmp(S.vsc[lo], vsc[lo], bytes));
        if (equal) scan_constant_reused++;
        else { memcpy(S.vsc[lo], vsc[lo], bytes); S.vsc_gen++; }
    }
    S.vsc_source = vsc;
    *dirty_lo = 192; *dirty_hi = 0;
}
void xv_d3d_SetPixelShader(uint32_t hash, uint32_t key, const float (*psc)[4]) { S.ps_hash = hash; S.ps_key = key ? key : ps_key_from_capture(hash); if (psc) memcpy(S.psc, psc, sizeof(S.psc)); }
/* Keep real resource shortages distinct and avoid per-frame log I/O during a
 * shortage. Opt-in memory samples also expose headroom when nothing drops. */
static void report_draw_drops(cmdlist_t *l)
{
    static int detail = -1, previous_drop;
    if (detail < 0) { const char *e = getenv("XV_LOG_DRAW_DROPS"); detail = e && atoi(e); }
    if ((l->dropped && !previous_drop) ||
        (g_build_frame % 60u == 0 && (l->dropped || detail))) {
        unsigned list = g_build_frame % XV_NUM_LISTS;
        XV_LOG("[draw-memory] frame %u drops %u (commands %u indices %u attributes %u immediate %u); commands %u/%u indices %u/%u requested %llu immediate %u/%u requested %llu\n",
            g_build_frame, l->dropped, l->drop_commands, l->drop_indices,
            l->drop_attributes, l->drop_immediate, l->ncmds, XV_MAX_CMDS,
            g_index_used[list], XV_FRAME_INDICES, (unsigned long long)g_index_requested[list],
            g_im_used[list], (unsigned)XV_IM_BYTES, (unsigned long long)g_im_requested[list]);
    }
    previous_drop = l->dropped != 0;
}

uint32_t xv_d3d_EndFrame(void)
{
#if XV_POSE_PIPELINE
    xv_pose_pipeline_end();
#endif
    xv_vertex_capture_drain();
    cmdlist_t *l = cur_list();
    report_draw_drops(l);
    if (l->const_dropped) XV_LOG("frame %u: %u draw(s) without constants (pool full: %u floats)\n", g_build_frame, l->const_dropped, l->nconsts);
    xv_d3d_last_draws = l->ncmds;
    extern unsigned xv_ui_gxm_record_frame(void);
    l->ui_frame = xv_ui_gxm_record_frame();
    g_record_pass = l->cur_pass;
    xv_vertex_upload_seal(g_build_frame % XV_NUM_LISTS);
    uint32_t done = g_build_frame++;
    return done;
}
unsigned xv_d3d_record_slot(void) { return g_build_frame % XV_NUM_LISTS; }
void xv_d3d_BeginFrame(void)
{
    { extern void xv_rec_ab_frame(void) __attribute__((weak)); if (xv_rec_ab_frame) xv_rec_ab_frame(); }   /* XV_REC_AB */
#if XV_POSE_PIPELINE
    xv_pose_pipeline_begin(g_build_frame);
#endif
    xv_vertex_capture_begin_slot(g_build_frame % XV_NUM_LISTS);
    index_reuse_begin_frame();
    xv_vertex_upload_reset(g_build_frame % XV_NUM_LISTS);
    g_frame_constants[g_build_frame % XV_NUM_LISTS].ready = 0;
    g_lists[g_build_frame % XV_NUM_LISTS]->cur_pass = g_record_pass;
    cmdlist_t *next = g_lists[g_build_frame % XV_NUM_LISTS];
    next->nui = 0; next->ncmds = 0; next->nconsts = 0; next->dropped = 0; next->const_dropped = 0; g_quad_used[g_build_frame % XV_NUM_LISTS] = 0;
    g_im_used[g_build_frame % XV_NUM_LISTS] = 0;
    g_im_requested[g_build_frame % XV_NUM_LISTS] = 0;
    g_index_used[g_build_frame % XV_NUM_LISTS] = 0;
    g_index_requested[g_build_frame % XV_NUM_LISTS] = 0;
    next->drop_commands = next->drop_indices = next->drop_attributes = next->drop_immediate = 0;
    next->nvisibility = next->active_visibility = 0;
    next->occl_n = 0; next->occl_armed = 0;
    next->visibility_gpu_ready = 0;

}

void xv_d3d_Clear(uint32_t flags, uint32_t color_argb, float z, uint32_t stencil)
{
    if (recording_dropped()) return;
    if (trace_frame()) XV_LOG("[hist] cmd %u: Clear flags %X color %08X z %.3f\n", cur_list()->ncmds, flags, color_argb, z);
    cmdlist_t *l=cur_list();
    unsigned clear_flags=(flags&X_D3DCLEAR_TARGET ? 1u : 0u)|(flags&X_D3DCLEAR_ZBUFFER ? 2u : 0u)|
        (flags&X_D3DCLEAR_STENCIL ? 4u : 0u);
    /* Consecutive clears of the same attachments can be one draw. A recorded
     * UI batch or visibility query makes the earlier clear observable. */
    if (l->ncmds && !l->active_visibility &&
        (!l->nui || l->ui[l->nui-1].before<l->ncmds)) {
        cmd_t *previous=&l->cmds[l->ncmds-1];
        if (previous->kind==1 && previous->pass==l->cur_pass && !previous->visibility) {
            if (clear_flags&1) previous->clear_color=color_argb;
            if (clear_flags&2) previous->clear_z=z;
            if (clear_flags&4) previous->clear_stencil=(uint8_t)stencil;
            previous->clear_flags|=clear_flags;
            return;
        }
    }
    cmd_t *c = new_cmd();
    if (!c)
        return;
    c->kind = 1;
    c->clear_flags = (uint8_t)clear_flags;
    c->clear_color = color_argb;
    c->clear_z = z;
    c->clear_stencil = (uint8_t)stencil;
}

void xv_d3d_Swap(void)
{
    xv_vertex_capture_drain();
    cmdlist_t *l = cur_list();
    report_draw_drops(l);
    xv_vertex_upload_seal(g_build_frame % XV_NUM_LISTS);
    g_build_frame++;
    index_reuse_begin_frame();
    /* reset the list the NEXT frame will use (the pump is done with it: at most one
       frame is in flight beyond the one just submitted) */
    cmdlist_t *next = g_lists[g_build_frame % XV_NUM_LISTS];
    next->nui = 0; next->ncmds = 0; next->nconsts = 0; next->dropped = 0;
    g_im_used[g_build_frame % XV_NUM_LISTS] = 0;
    g_im_requested[g_build_frame % XV_NUM_LISTS] = 0;
    g_index_used[g_build_frame % XV_NUM_LISTS] = 0;
    g_index_requested[g_build_frame % XV_NUM_LISTS] = 0;
    next->drop_commands = next->drop_indices = next->drop_attributes = next->drop_immediate = 0;
    next->nvisibility = next->active_visibility = 0;
    next->occl_n = 0; next->occl_armed = 0;
    next->visibility_gpu_ready = 0;
    xv_present();
}

/* ----------------------------------------------------------------------------------
 *  Replay (pump thread)
 * -------------------------------------------------------------------------------- */
/* XV_OCCL != 0 widens every core's counter array to the census size once, before the buffer is allocated. */
static int occl_on(void)
{
    if (g_occl_enabled < 0) {
        const char *e=getenv("XV_OCCL"); g_occl_enabled = e && atoi(e) != 0;
        if (g_occl_enabled) g_vis_core_words=XV_CENSUS_CORE_WORDS;
    }
    return g_occl_enabled;
}
static void visibility_draw_state(SceGxmContext *ctx, cmdlist_t *l, const cmd_t *c)
{
    unsigned slot=c && c->kind==0 && l->visibility_gpu_ready ? c->visibility : 0;
    if (!slot && c && l->census && l->visibility_gpu_ready) {
        unsigned i=(unsigned)(c-l->cmds);
        if (i<XV_CENSUS_CORE_WORDS-XV_VISIBILITY_PER_FRAME) slot=XV_VISIBILITY_PER_FRAME+i+1;
    }
    if (!slot && c && c->occl && l->occl_armed && l->visibility_gpu_ready && (c->kind==0 || c->kind==3))
        slot=(c->kind==3 ? XV_OCCL_PROXY0 : XV_OCCL_REAL0)+c->occl;   /* index + 1 */
    if (slot == l->visibility_draw_slot) return;
    if (slot) {
        if (slot<=XV_VISIBILITY_PER_FRAME)
        l->visibility[slot-1].render_area=c->pass && c->pass<=XV_RT_SLOTS ?
            g_rt[c->pass-1].w*g_rt[c->pass-1].h : l->visibility_back_area;
        sceGxmSetFrontVisibilityTestIndex(ctx,slot-1);
        sceGxmSetBackVisibilityTestIndex(ctx,slot-1);
        sceGxmSetFrontVisibilityTestEnable(ctx,SCE_GXM_VISIBILITY_TEST_ENABLED);
        sceGxmSetBackVisibilityTestEnable(ctx,SCE_GXM_VISIBILITY_TEST_ENABLED);
    } else {
        sceGxmSetFrontVisibilityTestEnable(ctx,SCE_GXM_VISIBILITY_TEST_DISABLED);
        sceGxmSetBackVisibilityTestEnable(ctx,SCE_GXM_VISIBILITY_TEST_DISABLED);
    }
    l->visibility_draw_slot=slot;
}
void xv_d3d_visibility_prepare(SceGxmContext *ctx, uint32_t frame, unsigned w, unsigned h)
{
    cmdlist_t *l=g_lists[frame % XV_NUM_LISTS];
    XV_VP_BEGIN(l,frame);
    l->visibility_gpu_ready=0;
    l->visibility_back_area=w*h;
    l->visibility_draw_slot=UINT32_MAX;
    visibility_draw_state(ctx,l,NULL);
    if (g_census_period < 0) {
        const char *e=getenv("XV_FRAG_CENSUS"); g_census_period=e?atoi(e):0;
        if (g_census_period < 0) g_census_period=0;
        if (g_census_period) g_vis_core_words=XV_CENSUS_CORE_WORDS;
        XV_LOG("fragment census: %s (every %d frames)\n", g_census_period?"on":"off", g_census_period);
    }
    l->census=g_census_period && frame>600 && frame%(unsigned)g_census_period==0;
    if (occl_on() && !g_occl_quads && g_occl_quads_uid < 0) {
        unsigned qb=(XV_NUM_LISTS*XV_OCCL_SLOTS*4*16+4095u)&~4095u;
        g_occl_quads_uid=sceKernelAllocMemBlock("xv_occl_quads",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,qb,NULL);
        void *qp=NULL;
        if (g_occl_quads_uid>=0 && sceKernelGetMemBlockBase(g_occl_quads_uid,&qp)>=0 && sceGxmMapMemory(qp,qb,SCE_GXM_MEMORY_ATTRIB_READ)>=0) g_occl_quads=qp;
        XV_LOG("[occl] proxy quads %s (%u bytes)\n", g_occl_quads?"ready":"FAILED", qb);
    }
    l->occl_armed=occl_on() && g_occl_quads && l->occl_n && !l->census;
    if (!l->nvisibility && !l->census && !l->occl_armed) return;
    uint32_t submitted_us=xk_os_monotonic_us?(uint32_t)xk_os_monotonic_us():0;
    for (unsigned i=0;i<l->nvisibility;i++)
        if (l->visibility[i].serial)
            xv_visibility_submit(g_visibility_results,l->visibility[i].result_slot,l->visibility[i].serial,submitted_us);
    static int attempted;
    if (!attempted) {
        attempted=1;
        unsigned bytes=XV_NUM_LISTS * XV_VISIBILITY_WORDS * sizeof(uint32_t);
        g_visibility_uid=sceKernelAllocMemBlock("xv_visibility",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,bytes,NULL);
        if (g_visibility_uid >= 0) {
            void *p=NULL;
            if (sceKernelGetMemBlockBase(g_visibility_uid,&p) >= 0 &&
                sceGxmMapMemory(p,bytes,SCE_GXM_MEMORY_ATTRIB_READ|SCE_GXM_MEMORY_ATTRIB_WRITE) >= 0)
                g_visibility_memory=p;
            else { sceKernelFreeMemBlock(g_visibility_uid); g_visibility_uid=-1; }
        }
        if (!g_visibility_memory) XV_LOG("visibility buffer allocation failed; reporting zero coverage\n");
    }
    if (!g_visibility_memory) return;
    uint32_t *p=g_visibility_memory+(frame % XV_NUM_LISTS)*XV_VISIBILITY_WORDS;
    if (l->census) memset(p,0,XV_VISIBILITY_WORDS*sizeof *p);
    else for (unsigned core=0;core<XV_VISIBILITY_GPU_CORES;core++) {
        memset(p+core*g_vis_core_words,0,XV_VISIBILITY_PER_FRAME*sizeof *p);
        if (l->occl_armed) memset(p+core*g_vis_core_words+XV_OCCL_REAL0,0,(XV_CENSUS_CORE_WORDS-XV_OCCL_REAL0)*sizeof *p);
    }
    int err=sceGxmSetVisibilityBuffer(ctx,p,XV_VISIBILITY_STRIDE);
    l->visibility_gpu_ready=err >= 0;
    sceGxmSetFrontVisibilityTestOp(ctx,SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
    sceGxmSetBackVisibilityTestOp(ctx,SCE_GXM_VISIBILITY_TEST_OP_INCREMENT);
    if (err < 0) XV_ONCE(warn_visibility,"visibility buffer rejected %08X; reporting zero coverage\n",err);
}
int xv_d3d_has_visibility(uint32_t frame)
{
    return g_lists[frame % XV_NUM_LISTS]->nvisibility != 0;
}
void xv_d3d_visibility_complete(uint32_t frame)
{
    cmdlist_t *l=g_lists[frame % XV_NUM_LISTS];
    const uint32_t *p=g_visibility_memory ?
        g_visibility_memory+(frame % XV_NUM_LISTS)*XV_VISIBILITY_WORDS : NULL;
    uint32_t completed_us=xk_os_monotonic_us?(uint32_t)xk_os_monotonic_us():0;
    for (unsigned i=0;i<l->nvisibility;i++) {
        if (!l->visibility[i].serial) continue;
        uint64_t total=0;
        if (l->visibility_gpu_ready && p)
            for (unsigned core=0;core<XV_VISIBILITY_GPU_CORES;core++)
                total+=p[core*g_vis_core_words+i];
        uint32_t pixels=xv_visibility_scale(total,l->visibility[i].guest_area,l->visibility[i].render_area);
        xv_visibility_publish_timed(g_visibility_results,l->visibility[i].result_slot,l->visibility[i].serial,pixels,completed_us);
        static unsigned shown; static int log_all=-1;
        if (log_all<0) log_all=getenv("XV_LOG_VISIBILITY")!=NULL;
        if (shown++<12 || (log_all && frame % 60u==0))
            XV_LOG("[visibility] frame %u id %u pixels %u raw %llu area %u/%u GPU %d\n",frame,
                g_visibility_results[l->visibility[i].result_slot].id,pixels,(unsigned long long)total,l->visibility[i].guest_area,l->visibility[i].render_area,l->visibility_gpu_ready);
    }
    if (l->nvisibility && xk_os_scheduler_notify) xk_os_scheduler_notify();
    XV_VP_COMPLETE(l,frame);
}

/* ---- XV_OCCL: recording-thread API for recomp/kernel/xk_occlusion.c -------------------------------------------- */
unsigned xv_d3d_occl_begin(uint32_t handle, unsigned flags)   /* tag the following draws with a new object slot */
{
    if (!occl_on()) return 0;
    cmdlist_t *l = cur_list();
    if (l->occl_n >= XV_OCCL_SLOTS) return 0;
    unsigned slot = ++l->occl_n;
    l->occl_handle[slot - 1] = handle; l->occl_flags[slot - 1] = (uint8_t)flags;
    g_occl_cur = (uint16_t)slot;
    return slot;
}
void xv_d3d_occl_end(void) { g_occl_cur = 0; }
uint32_t xv_d3d_occl_build_frame(void) { return g_build_frame; }
const float *xv_d3d_occl_matrix(void) { return &S.vsc[0][0]; }   /* c[-96..-93]: world -> clip rows */
int xv_d3d_occl_proxy(unsigned slot, float x0, float y0, float x1, float y1, float z)
{
    if (!slot || slot > cur_list()->occl_n) return 0;
    cmd_t *c = new_cmd();
    if (!c) return 0;
    c->kind = 3; c->occl = (uint16_t)slot; c->visibility = 0;
    c->texscale[0][0] = x0; c->texscale[0][1] = y0; c->texscale[0][2] = x1; c->texscale[0][3] = y1; c->clear_z = z;
    cur_list()->occl_flags[slot - 1] |= 4;
    return 1;
}
/* After final completion: each object's own depth-passing samples and its proxy's, to the kernel's table. */
void xv_d3d_occl_complete(uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    if (!l->occl_armed) return;
    l->occl_armed = 0;
    extern void xv_occl_result(uint32_t, uint32_t, uint32_t, uint32_t, unsigned) __attribute__((weak));
    if (!xv_occl_result || !l->visibility_gpu_ready || !g_visibility_memory) return;
    const uint32_t *p = g_visibility_memory + (frame % XV_NUM_LISTS) * XV_VISIBILITY_WORDS;
    for (unsigned i = 0; i < l->occl_n; i++) {
        uint64_t real = 0, proxy = 0;
        for (unsigned core = 0; core < XV_VISIBILITY_GPU_CORES; core++) {
            real += p[core * g_vis_core_words + XV_OCCL_REAL0 + i];
            proxy += p[core * g_vis_core_words + XV_OCCL_PROXY0 + i];
        }
        xv_occl_result(l->occl_handle[i], frame, (uint32_t)real, (uint32_t)proxy, l->occl_flags[i]);
    }
}

/* Final completion of a census frame: depth-passing samples per command (all four cores). The
 * counters measure coverage, not GPU time; blended and discarding draws are what they rank. */
void xv_d3d_frag_census_complete(uint32_t frame)
{
    cmdlist_t *l=g_lists[frame % XV_NUM_LISTS];
    if (!l->census) return;
    l->census=0;
    if (!l->visibility_gpu_ready || !g_visibility_memory) return;
    const uint32_t *p=g_visibility_memory+(frame % XV_NUM_LISTS)*XV_VISIBILITY_WORDS;
    enum { TOP=24, PS_TOP=12 };
    struct { uint32_t n; unsigned i; } top[TOP]; unsigned ntop=0;
    struct { int16_t entry; uint8_t kind, blend; uint32_t n, draws; } ps[64]; unsigned nps=0;
    struct { uint16_t vs; uint32_t n, draws, empty, idx, empty_idx, first, discard; } vsa[48]; unsigned nvsa=0;
    uint64_t indices=0, empty_indices=0;
    uint64_t total=0, clears=0, back=0, rt=0, opaque=0, blended=0, nocolor=0, tail=0, tail_blended=0, queries=0;
    unsigned last_query=0, draws=0, empty=0, n=l->ncmds, switches=0, prev_frame=0, tail_switches=0;
    for (unsigned i=0;i<n;i++) if (l->cmds[i].kind==0 && l->cmds[i].visibility) last_query=i+1;
    for (unsigned i=0;i<n;i++) {   /* target changes = scene breaks (tile store + reload of the previous target) */
        if (i && l->cmds[i].pass!=l->cmds[i-1].pass) { switches++; if (i>=last_query) tail_switches++; }
        if (l->cmds[i].kind==0 && l->cmds[i].previous_frame) prev_frame++;
    }
    if (n>XV_CENSUS_CORE_WORDS-XV_VISIBILITY_PER_FRAME) n=XV_CENSUS_CORE_WORDS-XV_VISIBILITY_PER_FRAME;
    for (unsigned i=0;i<n;i++) {
        const cmd_t *c=&l->cmds[i];
        if (c->kind==2 || c->kind==3) continue;
        uint32_t v=0;
        if (c->kind==0 && c->visibility) {
            for (unsigned core=0;core<XV_VISIBILITY_GPU_CORES;core++) v+=p[core*g_vis_core_words+c->visibility-1];
            queries+=v; continue;
        }
        for (unsigned core=0;core<XV_VISIBILITY_GPU_CORES;core++) v+=p[core*g_vis_core_words+XV_VISIBILITY_PER_FRAME+i];
        total+=v;
        if (c->kind==1) { clears+=v; continue; }
        draws++; if (!v) empty++;
        indices+=c->index_count; if (!v) empty_indices+=c->index_count;
        { unsigned k=0; while (k<nvsa && vsa[k].vs!=c->vs) k++;
          if (k==nvsa && nvsa<48) { vsa[nvsa].vs=c->vs; vsa[nvsa].n=vsa[nvsa].draws=vsa[nvsa].empty=vsa[nvsa].idx=vsa[nvsa].empty_idx=vsa[nvsa].discard=0; vsa[nvsa].first=i; nvsa++; }
          if (k<nvsa) { vsa[k].n+=v; vsa[k].draws++; vsa[k].idx+=c->index_count; if (!v) { vsa[k].empty++; vsa[k].empty_idx+=c->index_count; }
                        if (i<XV_CENSUS_CORE_WORDS && g_census_fs[i].valid && g_census_fs[i].discard) vsa[k].discard++; } }
        if (c->pass) rt+=v; else back+=v;
        int blend_on=c->blend!=BLEND_OPAQUE && c->blend<BLEND_NOCOLOR && g_blend_combo[c->blend].mask;
        if (c->blend==BLEND_NOCOLOR || !g_blend_combo[c->blend].mask) nocolor+=v; else if (blend_on) blended+=v; else opaque+=v;
        if (i+1>last_query && last_query) { tail+=v; if (blend_on) tail_blended+=v; }
        unsigned k=0; while (k<nps && !(ps[k].entry==c->ps_entry && ps[k].kind==c->fs_kind && ps[k].blend==c->blend)) k++;
        if (k==nps && nps<64) { ps[nps].entry=c->ps_entry; ps[nps].kind=c->fs_kind; ps[nps].blend=c->blend; ps[nps].n=0; ps[nps].draws=0; nps++; }
        if (k<nps) { ps[k].n+=v; ps[k].draws++; }
        unsigned pos=ntop<TOP?ntop++:TOP;
        if (pos==TOP) { if (v<=top[TOP-1].n) continue; pos=TOP-1; }
        while (pos>0 && top[pos-1].n<v) { top[pos]=top[pos-1]; pos--; } /* top[TOP] never read: pos<TOP here */
        top[pos].n=v; top[pos].i=i;
    }
    unsigned area=l->visibility_back_area?l->visibility_back_area:1;
    XV_LOG("[frag-census] frame %u: %u cmds (%u draws, %u empty) last query cmd %u; samples %llu = %.1f screens (back %.1f rt %.1f clears %.1f); opaque %.1f blended %.1f no-color %.1f; after the last query %.1f (blended %.1f); query quads %.2f; target switches %u (%u after the last query), previous-frame samplers %u\n",
        frame,l->ncmds,draws,empty,last_query,(unsigned long long)total,(double)total/area,(double)back/area,(double)rt/area,(double)clears/area,
        (double)opaque/area,(double)blended/area,(double)nocolor/area,(double)tail/area,(double)tail_blended/area,(double)queries/area,switches,tail_switches,prev_frame);
    for (unsigned t=0;t<ntop;t++) {
        const cmd_t *c=&l->cmds[top[t].i];
        unsigned tw=c->ntex?sceGxmTextureGetWidth(&c->tex[0]):0, th=c->ntex?sceGxmTextureGetHeight(&c->tex[0]):0;
        XV_LOG("[frag-census]  #%u cmd %u %.2f screens pass %u blend %u(%u/%u m%X) ps %s kind %u zf %u zw %u idx %u tex %u t0 %ux%u fmt %08X atest %05X prepared %u\n",
            t,top[t].i,(double)top[t].n/area,c->pass,c->blend,g_blend_combo[c->blend].src,g_blend_combo[c->blend].dst,g_blend_combo[c->blend].mask,
            c->ps_entry>=0?xv_ps_table[c->ps_entry].gxp:"-",c->fs_kind,c->depth_func_idx,c->depth_write,c->index_count,c->ntex,tw,th,
            c->ntex?(unsigned)sceGxmTextureGetFormat(&c->tex[0]):0u,c->atest&0x1FFFFu,c->depth_prepared);
    }
    XV_LOG("[frag-census]  indices %llu, of which %llu in zero-sample draws\n",(unsigned long long)indices,(unsigned long long)empty_indices);
    for (unsigned a=0;a<16 && a<nvsa;a++) {
        unsigned b=a; for (unsigned k=a+1;k<nvsa;k++) if (vsa[k].idx>vsa[b].idx) b=k;
        typeof(vsa[0]) tmp=vsa[a]; vsa[a]=vsa[b]; vsa[b]=tmp;
        const xv_vs_desc_t *d=vsa[a].vs<XV_MAX_VS?g_vs[vsa[a].vs].vs.desc:NULL;
        const cmd_t *fc=&l->cmds[vsa[a].first];
        unsigned fi=vsa[a].first<XV_CENSUS_CORE_WORDS?vsa[a].first:0;
        XV_LOG("[frag-census]  vs %u %s: %u draws (%u zero-sample, %u discard), %u indices (%u in zero-sample draws), %.2f screens; first cmd %u ps %s blend %u(%u/%u m%X) zf %u zw %u atest %05X stencil %u/%u/%u alpha-mode %u depth-only %u zrep %u prim %X psc0 %.3f %.3f %.3f %.3f psc16 %.3f %.3f %.3f stencil-ops %u/%u/%u\n",
            vsa[a].vs,d&&d->gxp?d->gxp:"-",vsa[a].draws,vsa[a].empty,vsa[a].discard,vsa[a].idx,vsa[a].empty_idx,(double)vsa[a].n/area,
            vsa[a].first,fc->ps_entry>=0?xv_ps_table[fc->ps_entry].gxp:"-",fc->blend,g_blend_combo[fc->blend].src,g_blend_combo[fc->blend].dst,g_blend_combo[fc->blend].mask,
            fc->depth_func_idx,fc->depth_write,fc->atest&0x1FFFFu,fc->stencil.enabled,fc->stencil.func,fc->stencil.pass,g_census_fs[fi].alpha_mode,g_census_fs[fi].depth_only,g_census_fs[fi].replaces_depth,(unsigned)fc->prim,
            fc->psc[0][0],fc->psc[0][1],fc->psc[0][2],fc->psc[0][3],fc->psc[16][0],fc->psc[16][1],fc->psc[16][2],fc->stencil.fail,fc->stencil.depth_fail,fc->stencil.pass);
    }
    for (unsigned a=0;a<PS_TOP && a<nps;a++) {
        unsigned b=a; for (unsigned k=a+1;k<nps;k++) if (ps[k].n>ps[b].n) b=k;
        typeof(ps[0]) tmp=ps[a]; ps[a]=ps[b]; ps[b]=tmp;
        XV_LOG("[frag-census]  program %s kind %u blend %u: %.2f screens over %u draws\n",
            ps[a].entry>=0?xv_ps_table[ps[a].entry].gxp:"-",ps[a].kind,ps[a].blend,(double)ps[a].n/area,ps[a].draws);
    }
    memset(g_census_fs,0,sizeof g_census_fs);
}

#if XV_QUERY_PREFIX_PUBLISH
#if !defined(XV_QUERY_BOUNDARY) || !defined(XV_FLARE_QUERY_OVERLAP)
#error XV_QUERY_PREFIX_PUBLISH requires exact query-boundary/history support
#endif
/* Earlier publication must not evict an older still-owned packet's retained
 * generation. Count frames, not serial distance: repeated IDs and wrap can
 * collide in the four-entry history even with only two frames in flight. */
int xv_d3d_visibility_publication_safe(uint32_t frame,const uint32_t *prior,unsigned count)
{
    uint32_t protected[(XV_VISIBILITY_IDS*XV_VISIBILITY_HISTORY+31u)/32u]={0};
    if(count>=XV_FRAME_SLOTS)return 0;
    const cmdlist_t *next=g_lists[frame%XV_NUM_LISTS];
    if(next->nvisibility>XV_VISIBILITY_PER_FRAME)return 0;
    for(unsigned p=0;p<count;p++) {
        if(prior[p]%XV_NUM_LISTS==frame%XV_NUM_LISTS)return 0;
        const cmdlist_t *old=g_lists[prior[p]%XV_NUM_LISTS];
        if(old->nvisibility>XV_VISIBILITY_PER_FRAME)return 0;
        for(unsigned i=0;i<old->nvisibility;i++) {
            if(!old->visibility[i].serial)continue;
            unsigned slot=old->visibility[i].result_slot;
            if(slot>=XV_VISIBILITY_IDS)return 0;
            unsigned key=slot*XV_VISIBILITY_HISTORY+old->visibility[i].serial%XV_VISIBILITY_HISTORY;
            protected[key/32u]|=1u<<(key%32u);
        }
    }
    for(unsigned i=0;i<next->nvisibility;i++) {
        if(!next->visibility[i].serial)continue;
        unsigned slot=next->visibility[i].result_slot;
        if(slot>=XV_VISIBILITY_IDS)return 0;
        unsigned key=slot*XV_VISIBILITY_HISTORY+next->visibility[i].serial%XV_VISIBILITY_HISTORY;
        if(protected[key/32u]&(1u<<(key%32u)))return 0;
    }
    return 1;
}
#endif

static SceGxmBlendFactor alpha_blend_factor(SceGxmBlendFactor f)
{
    switch (f) {
    case SCE_GXM_BLEND_FACTOR_SRC_COLOR: return SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case SCE_GXM_BLEND_FACTOR_DST_COLOR: return SCE_GXM_BLEND_FACTOR_DST_ALPHA;
    case SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return SCE_GXM_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return f;
    }
}

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
    static const SceGxmBlendFunc ops[] = { SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_SUBTRACT,
        SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT, SCE_GXM_BLEND_FUNC_MIN, SCE_GXM_BLEND_FUNC_MAX };
    bi->colorFunc = ops[g_blend_combo[blend].op];
    bi->alphaFunc = bi->colorFunc;
    SceGxmBlendFactor sf = d3d_blend_factor(g_blend_combo[blend].src), df = d3d_blend_factor(g_blend_combo[blend].dst);
    bi->colorSrc = sf; bi->colorDst = df;
    bi->alphaSrc = alpha_blend_factor(sf);
    bi->alphaDst = alpha_blend_factor(df);
    return bi;
}

static SceGxmFragmentProgram *fragment_for(vs_slot_t *v, unsigned kind, unsigned blend, const SceGxmProgramParameter **p_tex0)
{
    if (!v->fs_loaded[kind][blend]) {
        SceGxmBlendInfo bi; const SceGxmBlendInfo *pbi = blend_info_for(blend, &bi);
        v->fs_loaded[kind][blend] = 1;                 /* cache the attempt either way: a failure must not be retried */
        /* This already-embedded no-alpha combiner returns constant (1,1,1,0).
         * Color is masked off; no varying/sampler/uniform input is required.
         * A passthrough COLOR0 shader cannot link to every Xbox vertex shader. */
        const char *path = kind == FS_COLOR && blend == BLEND_NOCOLOR ?
            "builtin:xita-depth" : FS_GXP[kind];
        if (xv_fshader_load(&v->fs[kind][blend], path, &v->vs, pbi) != 0) {
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
                xv_gpu_flush_pump(mem, 8 * 8 * sizeof(uint32_t));
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
                xv_gpu_flush_pump(mem, 16384);
                g_cube_fallback_ok = sceGxmTextureInitCube(&g_cube_fallback, mem, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 16, 16, 1) == SCE_OK;
            }
        }
    }
    return g_cube_fallback_ok ? &g_cube_fallback : NULL;
}

/* combiner program from the table; NULL when it cannot be linked (caller falls back to fragment_for) */
static xv_fshader_t *fragment_for_ps_policy(vs_slot_t *v, int entry, unsigned blend, int alpha_mode, unsigned replace)
{
    if (entry < 0 || (unsigned)entry >= XV_PS_TABLE_COUNT || blend >= BLEND_MODES) return NULL;
    if (alpha_mode < 0 || alpha_mode > 3) return NULL;
    if (alpha_mode == 2 && xv_ps_table[entry].ps_key != 0x154066FDu)
        return fragment_for_ps_policy(v, entry, blend, 0, replace);
    unsigned vs = (unsigned)(v - g_vs);
    unsigned bucket = (vs * 131u + (unsigned)entry * 33u + blend + alpha_mode * 521u + replace * 977u) & (XV_PS_BUCKETS - 1);
    for (unsigned p = g_ps_buckets[bucket]; p; p = g_ps_links[p - 1].next) {
        ps_link_t *l = &g_ps_links[p - 1];
        if (l->vs == vs && l->entry == entry && l->blend == blend && l->alpha_mode == alpha_mode && l->replace_blend == replace) {
            if(!l->failed)return &l->fs;
            if(replace)return fragment_for_ps_policy(v,entry,blend,alpha_mode,0);
            return alpha_mode ? fragment_for_ps_policy(v,entry,blend,0,0) : NULL;
        }
    }
    if (g_ps_count == XV_PS_LINKS) {
        XV_ONCE(warn_ps_full, "combiner link cache full (%u): using fallback\n", g_ps_count);
        if(replace)return fragment_for_ps_policy(v,entry,blend,alpha_mode,0);
        return alpha_mode ? fragment_for_ps_policy(v, entry, blend, 0, 0) : NULL;
    }
    ps_link_t *l = &g_ps_links[g_ps_count++];
    l->vs = (uint16_t)vs; l->next = g_ps_buckets[bucket];
    g_ps_buckets[bucket] = (uint16_t)g_ps_count;
    l->entry = (int16_t)entry; l->blend = (uint8_t)blend; l->failed = 0; l->alpha_mode = alpha_mode;
    l->replace_blend=replace;
    SceGxmBlendInfo bi; const SceGxmBlendInfo *pbi = blend_info_for(blend, &bi);
    if(replace) { bi.colorFunc=SCE_GXM_BLEND_FUNC_NONE;bi.alphaFunc=SCE_GXM_BLEND_FUNC_NONE; }
    const char *path = xv_ps_table[entry].gxp; char variant[160];
    if (alpha_mode) {
        size_t len = strlen(path);
        if (len < 9 || len + 4 > sizeof variant || strcmp(path + len - 9, ".frag.gxp")) {
            l->failed = 1;
            return fragment_for_ps_policy(v, entry, blend, 0, replace);
        }
        snprintf(variant, sizeof variant, "%.*s_%s.frag.gxp", (int)(len - 9), path, alpha_mode == 3 ? "az" : alpha_mode == 2 ? "gt" : "na");
        path = variant;
    }
    if (xv_fshader_load(&l->fs, path, &v->vs, pbi) != 0) {
        static unsigned n; if (n++ < 12) XV_LOG("combiner %s does not link against %s (blend %u) - using heuristic fragment\n", xv_ps_table[entry].gxp, v->vs.desc ? v->vs.desc->gxp : "?", blend);
        l->failed = 1;
        if(replace)return fragment_for_ps_policy(v,entry,blend,alpha_mode,0);
        return alpha_mode ? fragment_for_ps_policy(v, entry, blend, 0, 0) : NULL;
    }
    if(replace)XV_LOG("[replace-blend] linked vs %u entry %d blend %u mask %X alpha-mode %d; source replacement, channels preserved\n",vs,entry,blend,g_blend_combo[blend].mask,alpha_mode);
    if (alpha_mode == 3) { static unsigned n; if (n++ < 16) XV_LOG("[alpha-zero] linked %s vs %u blend %u (%u/%u m%X): discard-free, zero source on a failed test\n", path, vs, blend, g_blend_combo[blend].src, g_blend_combo[blend].dst, g_blend_combo[blend].mask); }
    l->fs.alpha_test_mode = alpha_mode;
    return &l->fs;
}
static xv_fshader_t *fragment_for_ps_mode(vs_slot_t *v, int entry, unsigned blend, int alpha_mode)
{ return fragment_for_ps_policy(v,entry,blend,alpha_mode,
      replace_blend_enabled() && replace_blend_eligible(blend)); }
static xv_fshader_t *fragment_for_ps(vs_slot_t *v, int entry, unsigned blend)
{ return fragment_for_ps_mode(v, entry, blend, 0); }
/* XV_ALPHA_ZERO (opt-in, default 0): a failed alpha test writes a zero source instead of discarding, through the
 * `_az` programs (tools/specialize_ps_alphazero.py). PowerVR runs any program with discard as punch-through:
 * every depth-passing fragment is shaded before visibility resolves and the tile pipeline waits on it (the a10
 * bridge's unlit specular pass: 33 draws, zero surviving samples, ~20 ms of GPU per frame). Identical output only
 * where a zero source leaves the target unchanged: no depth write, stencil pass op KEEP, not a visibility query,
 * and color writes off or an ADD blend whose destination factor is ONE, INV_SRC_COLOR or INV_SRC_ALPHA. */
static int alpha_zero_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("XV_ALPHA_ZERO"); enabled = e && atoi(e) != 0;   /* opt-in: perf160 a10 early part got WORSE (GPU completion 114-126 -> 128-160 ms): the pass's cost is the shading, not punch-through */
        const char *o = getenv("XV_SHADER_OVERRIDE"); if (o && atoi(o)) enabled = 0;
        const char *n = getenv("XV_NO_ATEST"); if (n && atoi(n)) enabled = 0;
        XV_LOG("alpha-zero variants (no discard where a zero source is a no-op): %s\n", enabled ? "on" : "off");
    }
    return enabled;
}
static int alpha_zero_safe(const cmd_t *c)
{
    if (c->visibility || c->depth_write || c->blend >= BLEND_MODES) return 0;
    if (c->stencil.enabled && c->stencil.pass != 0) return 0;   /* discard would have skipped the pass op */
    if (!g_blend_combo[c->blend].mask) return 1;
    if (g_blend_combo[c->blend].op) return 0;
    unsigned d = g_blend_combo[c->blend].dst;
    return d == X_D3DBLEND_ONE || d == X_D3DBLEND_INVSRCCOLOR || d == X_D3DBLEND_INVSRCALPHA;
}
/* Captured function and enable are prerequisites: the dedicated program keeps
 * only the GREATER comparison. Opaque coverage still takes the earlier path. */
static int material_alpha_mode(const cmd_t *c, int no_alpha)
{
    if (no_alpha) return 1;
    if (cutout_enabled() && c->ps_entry >= 0 && (unsigned)c->ps_entry < XV_PS_TABLE_COUNT &&
        xv_ps_table[c->ps_entry].ps_key == 0x154066FDu &&
        (c->atest & (1u << 16)) && ((c->atest >> 8) & 7u) == 4) return 2;
    if ((c->atest & (1u << 16)) && alpha_zero_enabled() && alpha_zero_safe(c)) return 3;
    return 0;
}
static int draw_needs_alpha_test(uint32_t atest)
{
    /* ALWAYS is equivalent to disabled, including NaN alpha: preserve the
     * original return's saturate arithmetic in either variant. */
    return (atest & (1u << 16)) && ((atest >> 8) & 7u) != 7u;
}

static xv_fshader_t *depth_only_fragment(vs_slot_t *v, const cmd_t *c, xv_fshader_t *original)
{
    static int enabled = -1;
    if (enabled < 0) { const char *e=getenv("XV_DEPTH_ONLY_SHADER"); enabled=!e || atoi(e)!=0; }
    /* Use the successfully linked program's actual effects. Alpha testing,
     * texture-mode clipping and shader depth writes must preserve coverage.
     * The constant fragment has no discard, inputs or depth output; its color
     * is masked off. Failed links retain the original shader. */
    if (!enabled || !original || !original->fprog || c->blend >= BLEND_MODES ||
        g_blend_combo[c->blend].mask || original->uses_discard || original->replaces_depth)
        return original;
    if (!fragment_for(v, FS_COLOR, BLEND_NOCOLOR, NULL)) return original;
    return &v->fs[FS_COLOR][BLEND_NOCOLOR];
}

static void depth_prepare_learn(const cmd_t *c, const xv_fshader_t *linked, const xv_fshader_t *depth)
{
    /* 'linked' is NULL for heuristic fallbacks. Check the actual linked alpha
     * variant, not the requested mode: failed _na links can return mode 0. */
    if (!linked || !linked->fprog || linked->alpha_test_mode != 1 ||
        linked->uses_discard || linked->replaces_depth ||
        !depth || depth == linked || !depth->fprog) return;
    xv_depth_proof_publish(&g_depth_proofs, depth_prepare_key(c));
}

static SceGxmTexture g_previous_frame_texture, g_scene_backbuffer_texture;
void xv_d3d_SetPreviousFrameTexture(const SceGxmTexture *texture) { g_previous_frame_texture = *texture; }
void xv_d3d_SetSceneBackbufferTexture(const SceGxmTexture *texture) { g_scene_backbuffer_texture = *texture; }
int xv_d3d_uses_previous_frame(uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    for (unsigned i = 0; i < l->ncmds; ++i) if (l->cmds[i].previous_frame && !l->cmds[i].pass) return 1;
    return 0;
}
/* Check feedback after resolving the actual linked shader's active samplers,
 * previous-frame substitution, and cube/2D fallback. Unused bound stages do
 * not sample anything and cannot create feedback. */
static int bind_draw_textures(xv_texture_state *state, SceGxmContext *ctx, const cmd_t *c,
    const xv_fshader_t *fs, unsigned cube_mask
#ifdef XV_SCENE_CENSUS
    ,uint32_t *census_reads
#endif
    )
{
    const void *target=c->pass && c->pass<=XV_RT_SLOTS ? g_rt[c->pass-1].mem : NULL;
    for (unsigned t = 0; t < 4; ++t) {
        if (fs->tex_index[t] < 0) continue;                       /* program does not sample this stage */
        const SceGxmTexture *tx = t < c->ntex ? &c->tex[t] : NULL;
        if (c->previous_frame & (1u << t))
            tx = c->pass ? &g_scene_backbuffer_texture : &g_previous_frame_texture;
        int want_cube = (cube_mask & (1u << t)) != 0;
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
            if (!fb) return 0;
            tx = fb;
        }
        if (target && sceGxmTextureGetData(tx)==target) {
            XV_ONCE(warn_feedback, "RT feedback draw skipped (active sampler reads its color target)\n");
            return 0;
        }
#ifdef XV_SCENE_CENSUS
        if(census_reads) {
            const void *data=sceGxmTextureGetData(tx);unsigned mask=0;
            for(unsigned j=0;j<XV_RT_SLOTS;j++)if(data && data==g_rt[j].mem)mask|=1u<<j;
            if(data && data==sceGxmTextureGetData(&g_scene_backbuffer_texture))mask|=1u<<8;
            if(data && data==sceGxmTextureGetData(&g_previous_frame_texture))mask|=1u<<9;
            *census_reads|=mask?mask:1u<<10; /* Other/alias-unknown, never a no-dependency proof. */
        }
#endif
        /* Activate only when the linked binary advertises the matching uniform.
         * Old installed loading shaders retain their native address modes. */
        SceGxmTexture border_texture;
        if (t == 1 && fs->p_border1 && c->loading_border_axes) {
            border_texture = *tx;
            if (c->loading_border_axes & 1) sceGxmTextureSetUAddrMode(&border_texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
            if (c->loading_border_axes & 2) sceGxmTextureSetVAddrMode(&border_texture, SCE_GXM_TEXTURE_ADDR_CLAMP);
            tx = &border_texture;
        }
        unsigned result = xv_texture_state_bind(state, ctx, (unsigned)fs->tex_index[t], tx);
#ifdef XV_SCENE_CENSUS
        if(census_reads && result==XV_TEXTURE_BIND_ERROR)*census_reads|=1u<<11;
#endif
        xv_render_profile_texture(result == XV_TEXTURE_UNCHANGED, result == XV_TEXTURE_BIND_ERROR);
    }
    return 1;
}
#ifndef XV_TEXTURE_STATE_CACHE_DEFAULT
#define XV_TEXTURE_STATE_CACHE_DEFAULT 0
#endif
#if XV_TEXTURE_STATE_CACHE_DEFAULT != 0 && XV_TEXTURE_STATE_CACHE_DEFAULT != 1
#error XV_TEXTURE_STATE_CACHE_DEFAULT must be 0 or 1
#endif
static int texture_state_override = -1;
void xv_d3d_texture_state_override(int enabled)
{
    __atomic_store_n(&texture_state_override, enabled < 0 ? -1 : !!enabled, __ATOMIC_RELEASE);
}
static int texture_state_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("XV_TEXTURE_STATE_CACHE");
        enabled = e ? atoi(e) != 0 : XV_TEXTURE_STATE_CACHE_DEFAULT;
        XV_LOG("texture binding cache: %d (XV_TEXTURE_STATE_CACHE)\n", enabled);
    }
    int override = __atomic_load_n(&texture_state_override, __ATOMIC_ACQUIRE);
    return override < 0 ? enabled : override;
}
/* Dashboard/environment handoff is complete; neither recorder nor pump has
 * started. Initialize both retained lazy policies before their owning threads. */
void xv_d3d_configure_render_preparation(void)
{
    XV_LOG("[render-preparation] process-start texture-cache %d depth-prepare %d available %d; explicit environment overrides startup defaults\n",
        texture_state_enabled(), depth_prepare_enabled(), xv_depth_prepare_available());
}
/* A failed reservation leaves no constants for this draw. Never submit using
 * a previous draw's buffer; a partial write is equally unusable. */
static int bind_fragment_constants(SceGxmContext *ctx, const cmd_t *c,
    const xv_fshader_t *fs, uint32_t frame, unsigned command)
{
    if (!fs->p_psc && !fs->p_fogcolor && !fs->p_atest && !fs->p_texscale && !fs->p_border1) return 1;
    void *fub = NULL;
    const char *stage = "reserve";
    int err = XV_RENDER_CALL(XV_RENDER_FRAGMENT_UNIFORM,
        sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fub));
    if (err != 0 || !fub) goto fail;
    if (fs->p_psc) {
        stage = "psc";
        err = sceGxmSetUniformDataF(fub, fs->p_psc, 0, 18 * 4, &c->psc[0][0]);
        if (err != 0) goto fail;
    }
    if (fs->p_fogcolor) {
        uint32_t fc = c->fog_color;
        float fog[4] = { ((fc >> 16) & 0xFF) / 255.0f, ((fc >> 8) & 0xFF) / 255.0f,
            (fc & 0xFF) / 255.0f, ((fc >> 24) & 0xFF) / 255.0f };
        stage = "fog";
        err = sceGxmSetUniformDataF(fub, fs->p_fogcolor, 0, 4, fog);
        if (err != 0) goto fail;
    }
    if (fs->p_texscale) {
        stage = "texscale";
        err = sceGxmSetUniformDataF(fub, fs->p_texscale, 0, 16, &c->texscale[0][0]);
        if (err != 0) goto fail;
    }
    if (fs->p_border1) {
        uint32_t b = c->loading_border_color;
        float sign = c->ntex > 1 && sceGxmTextureGetMagFilter(&c->tex[1]) == SCE_GXM_TEXTURE_FILTER_POINT ? -1.0f : 1.0f;
        float data[8] = { ((b >> 16) & 255) / 255.0f, ((b >> 8) & 255) / 255.0f,
            (b & 255) / 255.0f, (b >> 24) / 255.0f,
            sign * (c->ntex > 1 ? sceGxmTextureGetWidth(&c->tex[1]) : 1),
            sign * (c->ntex > 1 ? sceGxmTextureGetHeight(&c->tex[1]) : 1),
            !!(c->loading_border_axes & 1), !!(c->loading_border_axes & 2) };
        stage = "loading-border";
        err = sceGxmSetUniformDataF(fub, fs->p_border1, 0, 8, data);
        if (err != 0) goto fail;
    }
    if (fs->p_atest) {
        uint32_t at = c->atest;
        static int noat = -1;
        if (noat < 0) { const char *e = getenv("XV_NO_ATEST"); noat = e ? atoi(e) : 0; }
        float av[4] = { (at & 0xFF) / 255.0f, (float)((at >> 8) & 7),
            (noat || !((at >> 16) & 1)) ? 0.0f : 1.0f, 0.0f };
        stage = "atest";
        err = sceGxmSetUniformDataF(fub, fs->p_atest, 0, 4, av);
        if (err != 0) goto fail;
    }
    return 1;
fail:
    { static unsigned warnings;
      if (warnings++ < 8) XV_LOG("[fragment-uniform] frame %u cmd %u %s failed 0x%08X buffer %p; draw skipped\n",
          frame, command, stage, (unsigned)err, fub); }
    return 0;
}

static void render_range(SceGxmContext *ctx, cmdlist_t *l, unsigned first, unsigned end, unsigned *clear_slot_io, uint32_t frame, unsigned clear_limit)
{
    unsigned clear_slot = *clear_slot_io;
    xv_draw_state state = {0};
    xv_stencil_cache stencil_state = {0};
    xv_texture_state textures;
    textures.valid = 0;
    xv_texture_state *texture_state = texture_state_enabled() ? &textures : NULL;
    for (unsigned i = first; i < end; ++i) {
        cmd_t *c = &l->cmds[i];
        if(c->kind==2)continue;
        visibility_draw_state(ctx,l,c);
        if (c->kind == 1) {
            state.valid = 0;
            stencil_state.valid = 0;
            textures.valid = 0;
            int slot = handle_to_slot(g_clear_vs);
            if (slot < 0 || clear_slot >= clear_limit)
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
            xv_stencil_clear(ctx, c->clear_flags & 4, c->clear_stencil);
            sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
            sceGxmSetFrontDepthWriteEnable(ctx, (c->clear_flags & 2) ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
            sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
            sceGxmSetVertexProgram(ctx, v->vs.vprog);
            sceGxmSetFragmentProgram(ctx, fp);
            sceGxmSetVertexStream(ctx, 0, q);
            xv_gpu_flush_pump(q, 4 * sizeof *q);
            XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, SCE_GXM_INDEX_FORMAT_U16, g_seq_indices, 4));
            continue;
        }
        if (c->kind == 3) {   /* XV_OCCL proxy: the object's screen rectangle at its nearest depth, depth-tested only */
            state.valid = 0;
            stencil_state.valid = 0;
            textures.valid = 0;
            int slot = handle_to_slot(g_clear_vs);
            if (slot < 0 || !g_occl_quads || !c->occl || c->occl > XV_OCCL_SLOTS || !l->occl_armed)
                continue;
            vs_slot_t *v = &g_vs[slot];
            SceGxmFragmentProgram *fp = fragment_for(v, FS_COLOR, BLEND_NOCOLOR, NULL);
            if (!fp)
                continue;
            struct { float x, y, z; uint8_t b, g, r, a; } *q =
                (void *)(g_occl_quads + ((frame % XV_NUM_LISTS) * XV_OCCL_SLOTS + c->occl - 1) * 4 * 16);
            float x0 = c->texscale[0][0], y0 = c->texscale[0][1], x1 = c->texscale[0][2], y1 = c->texscale[0][3], z = c->clear_z;
            q[0] = (typeof(q[0])){ x0, y0, z, 0, 0, 0, 0 }; q[1] = (typeof(q[0])){ x1, y0, z, 0, 0, 0, 0 };
            q[2] = (typeof(q[0])){ x1, y1, z, 0, 0, 0, 0 }; q[3] = (typeof(q[0])){ x0, y1, z, 0, 0, 0, 0 };
            xv_stencil_clear(ctx, 0, 0);
            sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_LESS_EQUAL);
            sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
            sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
            sceGxmSetVertexProgram(ctx, v->vs.vprog);
            sceGxmSetFragmentProgram(ctx, fp);
            sceGxmSetVertexStream(ctx, 0, q);
            xv_gpu_flush_pump(q, 4 * sizeof *q);
            XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLE_FAN, SCE_GXM_INDEX_FORMAT_U16, g_seq_indices, 4));
            continue;
        }

        { /* XV_WCLAMP: clamp 1/w so triangles that cross the near plane (a vertex at w~=0) do not explode into
           * huge stretched polygons - Halo relies on D3D near-plane clipping that GXM does not do the same way
           * (the "geometry bugs out when looking/walking" artifact). Value = max 1/w; tune with XV_WCLAMP_VAL. */
          static int wc = -1; static float wcv = 16.0f;
          if (wc < 0) { const char *e = getenv("XV_WCLAMP"); wc = e ? atoi(e) : 0; const char *ev = getenv("XV_WCLAMP_VAL"); if (ev) wcv = (float)atof(ev); }
          if (wc) { sceGxmSetWClampEnable(ctx, SCE_GXM_WCLAMP_MODE_ENABLED); sceGxmSetWClampValue(ctx, wcv); }   /* per draw: GXM state is per-scene */
        }
        vs_slot_t *v = &g_vs[c->vs];
        static int specialize_alpha = -1;
        if (specialize_alpha < 0) {
            const char *e = getenv("XV_ALPHA_SPECIALIZE");
            specialize_alpha = e ? atoi(e) != 0 : 1;
            XV_LOG("alpha-disabled shader specialization: %d\n", specialize_alpha);
        }
        int no_alpha = specialize_alpha && (!draw_needs_alpha_test(c->atest) || c->opaque_alpha);
        int alpha_mode = material_alpha_mode(c, no_alpha);
        xv_fshader_t *fs = c->depth_prepared ? &v->fs[FS_COLOR][BLEND_NOCOLOR] : c->ps_entry >= 0 ?
            XV_RENDER_CALL(XV_RENDER_SHADER_LOOKUP, fragment_for_ps_mode(v, c->ps_entry, c->blend, alpha_mode)) : NULL;
        xv_fshader_t *linked = c->depth_prepared ? NULL : fs;
        /* Heuristic fragments use only 2D samplers, even if the requested combiner used cubes. */
        unsigned cube_mask = !c->depth_prepared && fs ? xv_ps_table[c->ps_entry].cube_mask : 0;
        SceGxmFragmentProgram *fp = fs ? fs->fprog : fragment_for(v, c->fs_kind, c->blend, NULL);
        if (!fp)
            continue;
        if (!fs) fs = &v->fs[c->fs_kind][c->blend];
        xv_fshader_t *depth_fs = c->depth_prepared ? fs : depth_only_fragment(v, c, fs);
        int depth_only = c->depth_prepared || depth_fs != fs;
        if (!c->depth_prepared) depth_prepare_learn(c, linked, depth_fs);
        if (c->visibility && c->geometry_bytes[XV_MAX_STREAMS]) {
            /* The retained histogram marker belongs to this command, unlike
             * trace_frame(), whose recording frame can advance during replay. */
            unsigned samplers = 0;
            for (unsigned t = 0; t < 4; ++t)
                if (depth_fs->tex_index[t] >= 0) samplers |= 1u << t;
            XV_LOG("[query-replay] frame %u cmd %u query %u mask %X entry %d atest %08X alpha-mode %d discard %u depth-write %u depth-only %u samplers %X prepared %u\n",
                frame, i, (unsigned)c->visibility, g_blend_combo[c->blend].mask,
                (int)c->ps_entry, c->atest, alpha_mode, (unsigned)fs->uses_discard,
                (unsigned)fs->replaces_depth, depth_only, samplers, (unsigned)c->ntex);
        }
        if (depth_fs != fs) {
            fs = depth_fs; fp = fs->fprog; cube_mask = 0;
        }
        if (l->census && i < XV_CENSUS_CORE_WORDS)
            g_census_fs[i] = (typeof(g_census_fs[0])){ 1, fs->uses_discard, fs->replaces_depth, (uint8_t)depth_only, (uint8_t)alpha_mode };
        if (depth_only) xv_render_profile_depth_only(c->index_count);
#ifdef XV_SCENE_CENSUS
        int census_sample=xv_sc_sampled(i);uint32_t census_reads=0;
#endif
        if (!bind_draw_textures(texture_state,ctx,c,fs,cube_mask
#ifdef XV_SCENE_CENSUS
            ,census_sample?&census_reads:NULL
#endif
            )) continue;
        xv_render_profile_work(c->ps_entry >= 0 && (unsigned)c->ps_entry < XV_PS_TABLE_COUNT ? xv_ps_table[c->ps_entry].ps_key : 0,
            c->index_count, no_alpha && !fs->p_atest);
        xv_stencil_bind_cached(&stencil_state, ctx, &c->stencil);
        xv_draw_state_bind(&state, ctx, DEPTH_FUNCS[c->depth_func_idx],
            c->depth_write ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED,
            c->cull == X_D3DCULL_NONE ? SCE_GXM_CULL_NONE
                : c->cull == X_D3DCULL_CW ? SCE_GXM_CULL_CW : SCE_GXM_CULL_CCW,
#if XV_PACKED_VERTEX_LAYOUT
            c->packed_vertex ? v->vs.packed_vprog : v->vs.vprog, fp);
#else
            v->vs.vprog, fp);
#endif

        if (!XV_RENDER_CALL(XV_RENDER_VERTEX_UNIFORM, bind_vertex_constants(ctx, l, c, &v->vs, frame)))
            continue;
        xv_vshader_set_streams(ctx, &v->vs, c->streams);
        if (v->vs.const_stream != 0xFF && c->constant_stream)
            sceGxmSetVertexStream(ctx, v->vs.const_stream, c->constant_stream);
        if (!bind_fragment_constants(ctx, c, fs, frame, i))
            continue;
        if (fs->alpha_test_mode == 2) xv_render_profile_cutout(c->index_count);
        int draw_result=XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, (SceGxmPrimitiveType)c->prim, SCE_GXM_INDEX_FORMAT_U16, c->indices, c->index_count));
#ifdef XV_SCENE_CENSUS
        if(census_sample && draw_result>=0) {
            xv_sc_sample sample={i,c->ps_entry>=0 && (unsigned)c->ps_entry<XV_PS_TABLE_COUNT?xv_ps_table[c->ps_entry].ps_key:0,
                v->vs.desc?v->vs.desc->func_hash:0,(uint32_t)(uintptr_t)fp,(uint32_t)(uintptr_t)fs->id,(uint32_t)(int32_t)c->ps_entry,
                (depth_only?2u:linked?1u:0u)|((unsigned)fs->alpha_test_mode<<8)|((unsigned)c->blend<<16)|((unsigned)c->fs_kind<<24),census_reads,1};
            xv_sc_sample_draw(&sample);
        }
#else
        (void)draw_result;
#endif
    }
    *clear_slot_io = clear_slot;
}

void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    unsigned clear_slot = 0;
    render_range(ctx, l, 0, l->ncmds, &clear_slot, frame, XV_LEGACY_CLEAR_SLOTS);
    visibility_draw_state(ctx,l,NULL);
    XV_VP_REPLAYED(frame,0);
}
int xv_d3d_has_render_targets(uint32_t frame)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    for (unsigned i = 0; i < l->ncmds; ++i) if (l->cmds[i].pass) return 1;
    for (unsigned i = 0; i < l->nui; ++i) if (l->ui[i].target) return 1;
    return 0;
}
/* Caller supplies the current display buffer. Leaves a backbuffer scene open for
 * UI and the existing EndScene/heartbeat/flip. Only called for frames using RTT. */
int xv_d3d_render_targets(SceGxmContext *ctx, uint32_t frame,
    SceGxmRenderTarget *back, SceGxmSyncObject *sync,
    const SceGxmColorSurface *color, const SceGxmDepthStencilSurface *depth,
    unsigned back_width, unsigned back_height, int depth_tail_readonly)
{
    cmdlist_t *l = g_lists[frame % XV_NUM_LISTS];
    SceGxmDepthStencilSurface bd = *depth;
    XV_DS_SETUP(l, depth_tail_readonly);
    sceGxmDepthStencilSurfaceSetForceStoreMode(&bd, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
    unsigned clear_slot = 0, current = 0xff;
    int open = 0;
    static int queue_passes = -1;
    if (queue_passes < 0) {
        const char *e = getenv("XV_RT_QUEUE");
        queue_passes = e ? atoi(e) != 0 : 1;
        XV_LOG("RT pass queue: %s (XV_RT_QUEUE=%d)\n",
            queue_passes ? "enabled" : "disabled", queue_passes);
    }
    /* Fragment passes remain ordered on this context across frames. Resource
     * replacement drains owners; ordinary target reuse needs no CPU Finish. */
    unsigned i = 0, u = 0;
    for (;;) {
        int ui = u < l->nui && l->ui[u].before <= i;
        int done = i >= l->ncmds && !ui;
        unsigned target = ui ? l->ui[u].target : done ? 0 : l->cmds[i].pass;
        if (target > XV_RT_SLOTS || (target && !g_rt[target - 1].rt)) {
            XV_LOG("RT invalid target %u at frame %u command %u UI %u\n", target, frame, i, u);
            XV_VP_ERROR(frame,VP_ERR_TARGET);
            if (open) {
                int end_result=XV_RENDER_END(current, sceGxmEndScene(ctx, NULL, NULL));
                XV_VP_END(frame,i,u,end_result);
                (void)end_result;
                xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                sceGxmFinish(ctx);
                xv_render_profile_stage(XV_RENDER_SUBMIT);
            }
            return -1;
        }
        if (target != current) {
            if (open) {
                const SceGxmNotification *end_fence=XV_QB_NOTIFICATION(frame,i,u);
#ifdef XV_SCENE_CENSUS
                end_fence=xv_sc_end(current,i,u,end_fence,SC_QUERY);
#endif
                int err = XV_RENDER_END(current, sceGxmEndScene(ctx, NULL, end_fence));
#ifdef XV_SCENE_CENSUS
                xv_sc_ended(err);
#endif
                XV_QB_SUBMITTED(frame,i,u,err);
                XV_VP_END(frame,i,u,err);
                /* Successive scenes on this context preserve fragment order:
                 * later passes may sample earlier color/depth stores without
                 * blocking the CPU here. Clear slots, UI batches and geometry
                 * stay owned until this frame's final fragment notification.
                 * Keep a synchronous diagnostic fallback and drain on errors. */
                if (!queue_passes || err < 0) {
                    xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                    sceGxmFinish(ctx);
                    xv_render_profile_stage(XV_RENDER_SUBMIT);
                }
                if (err < 0) { XV_LOG("RT EndScene failed %08X\n", err); return -1; }
                XV_DS_STORED(current);
                open = 0;
            }
            rt_alias_t *r = target ? &g_rt[target - 1] : NULL;
            XV_DS_SURFACE(l,i,u,target,&bd,back_width,back_height);
            int err = XV_RENDER_CALL(XV_RENDER_SCENE_BEGIN, sceGxmBeginScene(ctx, 0, r ? r->rt : back, NULL, NULL,
                r ? NULL : sync, r ? &r->color : color, r ? &r->depth : &bd));
            if (err < 0) {
                xv_render_profile_stage(XV_RENDER_TARGET_FINISH);
                sceGxmFinish(ctx);
                xv_render_profile_stage(XV_RENDER_SUBMIT);
                XV_LOG("RT BeginScene target %u failed %08X\n", target, err);
                XV_VP_ERROR(frame,VP_ERR_BEGIN);
                return -1;
            }
#ifdef XV_SCENE_CENSUS
            const SceGxmDepthStencilSurface *sc_depth=r?&r->depth:&bd;
            xv_sc_open(target,sceGxmDepthStencilSurfaceGetForceLoadMode(sc_depth),sceGxmDepthStencilSurfaceGetForceStoreMode(sc_depth));
#endif
            if (!target) {
                sceGxmDepthStencilSurfaceSetForceLoadMode(&bd, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
            }
            unsigned w = r ? r->w : back_width, h = r ? r->h : back_height;
            sceGxmSetViewport(ctx, w * 0.5f, w * 0.5f, h * 0.5f, -(float)h * 0.5f, 0.5f, 0.5f);
            current = target; open = 1;
        }
        if (done) break;
        if (ui) {
            visibility_draw_state(ctx,l,NULL);
            extern void xv_ui_gxm_replay_batch(SceGxmContext *, unsigned, unsigned, const void *);
            xv_ui_gxm_replay_batch(ctx, l->ui[u].frame, l->ui[u].batch, target ? g_rt[target - 1].mem : NULL);
            ++u;
        } else {
            /* Replay consecutive mesh commands together so the local state
             * cache survives adjacent draws. Preserve every UI/target boundary. */
            unsigned end=i+1;
            while (end<l->ncmds && l->cmds[end].pass==target &&
                   (u>=l->nui || end<l->ui[u].before)) ++end;
            render_range(ctx, l, i, end, &clear_slot, frame, XV_CLEAR_SLOTS);
            i=end;
        }
    }
    extern void xv_ui_gxm_replay_overlay(SceGxmContext *, unsigned);
    visibility_draw_state(ctx,l,NULL);
    xv_ui_gxm_replay_overlay(ctx, l->ui_frame);
    XV_VP_REPLAYED(frame,1);
    return 0;
}

/* The clear quad's vertex shader is a runtime-owned program registered by main.c. */
void xv_d3d_set_clear_shader(uint32_t handle) { g_clear_vs = handle; }

static uint32_t gl_blend_to_d3d(uint32_t gl)
{
    switch (gl) { case 0: return 1; case 1: return 2; case 0x300: return 3; case 0x301: return 4; case 0x302: return 5; case 0x303: return 6;
                  case 0x304: return 7; case 0x305: return 8; case 0x306: return 9; case 0x307: return 10; case 0x308: return 11; default: return 2; }
}
void xv_d3d_SyncDrawState(const struct xd3d_state *state,
                        const float (*attributes)[4],const uint32_t texture_state[4][5])
{
    xv_d3d_SetAllAttributes(attributes);
    xv_d3d_SetPixelShader(state->ps_hash, state->ps_key, state->psc);
    for (unsigned i = 0; i < 4; ++i) {
        xv_d3d_SetStreamSource(i, state->stream_vb[i], state->stream_stride[i]);
        xv_d3d_SetTexture(i, state->texture[i]);
        xv_d3d_SetTexturePalette(i, state->palette[i]);
        /* XDK 3925 orders ADDRESSU/V at 10/11 and MAG/MINFILTER at 13/14.
         * The deferred setter already updates this guest table. Leaving GXM's
         * mesh defaults here forced point sampling even for filtered lightmaps. */
        xv_d3d_SetTextureStageState(i, X_D3DTSS_ADDRESSU, texture_state[i][0]);
        xv_d3d_SetTextureStageState(i, X_D3DTSS_ADDRESSV, texture_state[i][1]);
        xv_d3d_SetTextureStageState(i, X_D3DTSS_BORDERCOLOR, texture_state[i][2]);
        xv_d3d_SetTextureStageState(i, X_D3DTSS_MAGFILTER, texture_state[i][3]);
        xv_d3d_SetTextureStageState(i, X_D3DTSS_MINFILTER, texture_state[i][4]);
    }
    /* the kernel model keeps the NV2A/GL tokens the game wrote; xv_d3d speaks D3D enums */
    xv_d3d_SetStencil(&state->stencil);
    xv_d3d_SetRenderState_ZEnable(state->z_enable);
    xv_d3d_SetRenderState_ZWriteEnable(state->z_write);
    xv_d3d_SetRenderState_ZFunc(state->z_func >= 0x200 && state->z_func <= 0x207 ? state->z_func - 0x200 + 1 : 4);
    xv_d3d_SetRenderState_CullMode(state->cull == 0x900 ? 2 : state->cull == 0x901 ? 3 : 1);   /* GL_CW / GL_CCW / none */
    xv_d3d_SetRenderState_AlphaBlendEnable(state->alpha_blend);
    xv_d3d_SetRenderState_BlendOp(state->blend_op);
    { uint32_t m = state->color_mask; xv_d3d_SetRenderState_ColorWriteEnable(((m >> 16) & 1) | ((m >> 7) & 2) | ((m & 1) << 2) | ((m >> 21) & 8)); }   /* -> D3D bits R1 G2 B4 A8 */
    xv_d3d_SetRenderState_SrcBlend(gl_blend_to_d3d(state->src_blend));
    xv_d3d_SetRenderState_DestBlend(gl_blend_to_d3d(state->dst_blend));
}
