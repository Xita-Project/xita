#include "xv_version.h"
/*
 * xv_ui_gxm.c - hardware (GXM) renderer for the recompiled engine's immediate-mode UI path.
 * See xv_ui_gxm.h for the design; see recomp/kernel/xd3d.c for the caller (xd3d_r_* hooks).
 *
 * Flat C, no per-frame heap churn: rings are allocated once, batches live in a fixed pool, and the
 * only work on the game fiber is a tight pack loop that streams vertices straight into GPU memory.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>

#include "xv_shader.h"
#include "xv_frame_slots.h"
#include "xv_gpu_upload.h"
#include "xv_vertex_upload.h"
#include "xv_vertex_capture.h"
#include "xv_render_profile.h"
#include "xv_cpu.h"
#include "xv_d3d.h"
#include "xv_stencil_gxm.h"
#include "xv_ui_gxm.h"
#include "xv_settings.h"
#include "dashboard/font.h"
#include "shaders/xv_layouts.h"           /* xv_vs_halo_vs_03, xv_vs_clear, xv_halo_vs[] */

#include "xv_log.h"
#include "xv_draw_profile.h"
#include "xv_flare_clip.h"
#define UI_LOG(...)  xv_logf("[xv/ui] " __VA_ARGS__)

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((uint32_t)(a) - 1))
#define UI_DECL_VA       0x001E13ECu       /* Halo's screen-space UI vertex declaration */
#define UI_MAX_QUADS     4096u
#define UI_MAX_VERTS     (UI_MAX_QUADS * 4u)
#define UI_MAX_BATCHES   1024u
extern volatile uint64_t xv_pump_us_acc;              /* main.c: render time spent on the pump thread */
#define UI_FRAMES        XV_FRAME_SLOTS    /* independently retired recording slots */
#define UI_TEX_CACHE     768u
#define UI_TEX_MAXDIM    256u              /* decode no mip level larger than this (level textures are 512-1024) */
#define UI_TEX_BAD       1024u
#define UI_MAX_PROGS     4u                /* vertex programs sharing the UI declaration (vs_03/04/38) */
#define UI_MAX_STAGES    4u                /* NV2A texture stages */
#define OVL_MAX_QUADS    768u              /* debug overlay: 7-segment digits drawn with the clear program */

/* read a guest dword through the runtime's guest->host mapping */
/* X_G (page-table guest->host) comes from recomp/kernel/xd3d.h -> ../xv_x86rt.h */
static inline uint32_t guest_u32(uint32_t addr) { return *(const uint32_t *)X_G(addr); }

/* clear-quad vertex: matches xv_vs_clear (position float3 @0, D3DCOLOR @12, stride 16) */
typedef struct { float x, y, z; uint32_t color; } clr_vtx;

/* One packed UI vertex.  Exactly matches xv_vs_halo_vs_03's stream 0 (stride 20, 32-bit aligned):
 * position float2 @0, color1 float2 @8 (v4, an input to the shader's uv math), texcoord0 D3DCOLOR @16
 * (v9, the vertex diffuse). */
typedef struct { float x, y; float u, v; uint32_t color; } ui_vtx;

typedef struct {
    uint32_t      first_vertex;            /* into the current frame's vertex slot */
    uint32_t      nquads;
    uint8_t       prog;                    /* UI vertex program index (by microcode FNV) */
    uint8_t       stage;                   /* texture stage the combiner reads its image from */
    int           has_tex;
    SceGxmTexture tex;
    float         cwin[XV_MAX_ATTRS * 4];  /* c[] snapshot (c_count float4) */
    float         tint[4];
} ui_batch;

typedef struct {
    ui_vtx   *verts;                       /* points inside the GPU vertex ring */
    uint32_t  vcount;
    ui_batch  batches[UI_MAX_BATCHES];
    uint32_t  bcount;
    uint32_t  clear_argb;
    int       has_clear;
    int       overflow;
    xv_dash_graphics_view settings;
} ui_frame;

/* built texture control words, cached by (guest data pointer, format word) */
typedef struct { uint32_t data, fmtword, palsum; SceGxmTexture tex; int valid; uint32_t sum, bytes, source_data; unsigned checked; uint16_t stable; uint8_t dirty, opaque, pinned, rgba_layout; int16_t next; } ui_tex_entry;   /* source_data: selected mip's physical address; data remains the resource/cache identity */   /* stable: consecutive unchanged re-checks; palsum: P8 palette hash; sum: source content hash; next: hash chain */

/* one UI vertex program + the fragment programs linked against it (one per texcoord set it writes) */
typedef struct {
    const xv_vs_desc_t *desc;
    xv_vshader_t        vs;
    xv_fshader_t        fs[UI_MAX_STAGES];
    int                 fs_ok[UI_MAX_STAGES];
    int                 ntex;              /* oT0..oT(ntex-1) written by this program */
} ui_prog;

static struct {
    int              ready;

    ui_prog          prog[UI_MAX_PROGS];
    uint32_t         nprog;

    xv_vshader_t     clear_vs;
    xv_fshader_t     clear_fs, settings_fs;

    /* GPU-mapped memory */
    SceUID           vbuf_uid;  ui_vtx  *vbuf;         /* UI_FRAMES * UI_MAX_VERTS */
    SceUID           ibuf_uid;  uint16_t *ibuf;        /* UI_MAX_QUADS * 6, built once */
    SceUID           clr_uid;   clr_vtx *clrbuf;       /* UI_FRAMES * (4 fullscreen verts + OVL_MAX_QUADS*4 overlay) */
    SceUID           settings_uid; clr_vtx *settingsbuf;
    SceUID           dec_uid;   uint8_t *dec_base;     /* Immutable decoded RGBA / native BC texture uploads */
    uint32_t         dec_off, dec_cap;

    ui_frame         frame[UI_FRAMES];
    uint32_t         rec;                              /* frame being recorded (game fiber) */
    volatile int32_t pub;                              /* last published frame, or -1 (replay) */

    ui_tex_entry     texcache[UI_TEX_CACHE];
    uint32_t         texcount;
    int16_t          texhash[1024];                    /* (data ^ fmtword) -> first entry, chained via ->next */
    int              tex_purge;                        /* pool/cache exhausted: wipe after this frame */
    unsigned         rec_frame;                        /* frames recorded (dynamic-texture check cadence) */
    uint32_t         tex_purges, tex_purge_frame;
    struct { uint32_t data, fmtword; } bad[UI_TEX_BAD]; unsigned nbad;
} g;

/* ---- GPU memory ------------------------------------------------------------------------------- */
static void *ui_gpu_alloc(uint32_t size, SceUID *uid)
{
    void *base = NULL;
    size = ALIGN_UP(size, 4 * 1024);
    *uid = sceKernelAllocMemBlock("xv_ui", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (*uid < 0) { UI_LOG("alloc %u KB failed: 0x%08X\n", size >> 10, *uid); return NULL; }
    sceKernelGetMemBlockBase(*uid, &base);
    int err = sceGxmMapMemory(base, size, SCE_GXM_MEMORY_ATTRIB_READ);
    if (err != SCE_OK) { UI_LOG("map failed: 0x%08X\n", err); sceKernelFreeMemBlock(*uid); return NULL; }
    return base;
}


/* ---- CPU texture decode to linear RGBA8 (bytes R,G,B,A == GXM U8U8U8U8_ABGR) --------------------
 * The GPU's hardware twiddle does not match the NV2A layout for Xbox's swizzled/DXT UI atlas, so we
 * de-swizzle + decompress once on the CPU (textures are cached).  Same logic proved correct on host. */
#include "xv_texture_worker.h"
#include "xv_texture_alpha.h"
#include "xv_rgba_layout.h"
#include "xv_geometry_worker.h"
#include "xv_quality_settings.h"
/* GXM (PowerVR) twiddle: same min-square/long-axis scheme, but Y occupies the even (low) bits and X the
 * odd ones - the transpose of NV2A's order.  Used when handing block-compressed data to the GPU as-is. */
static void gxm_unswz(unsigned w, unsigned h, unsigned idx, unsigned *ox, unsigned *oy){
    unsigned by=ui_morton1(idx), bx=ui_morton1(idx>>1), lw=0, lh=0, t;
    for(t=w;t>1;t>>=1)lw++; for(t=h;t>1;t>>=1)lh++;
    unsigned c=lw<lh?lw:lh, m=(1u<<c)-1;
    if(lw>=lh){*ox=(bx&m)|((idx>>(2*c))<<c);*oy=by&m;} else {*oy=(by&m)|((idx>>(2*c))<<c);*ox=bx&m;}
}
static const uint32_t *g_cur_pal;       /* palette (guest memory, D3DCOLOR) for the P8 texture being decoded */
static uint32_t g_cur_palsum;

static int ui_decode(const uint8_t *src, unsigned fmt, unsigned w, unsigned h, unsigned pitch, int linear, uint32_t *dst)
{
    /* Resolve diagnostics on the caller; the worker reads only its job inputs. */
    static int configured, transpose, flat;
    if (!configured) {
        transpose = getenv("XV_SWZ_T") != NULL;
        flat = getenv("XV_P8_FLAT") != NULL;
        configured = 1;
    }
    xv_texture_job job = { src, dst, g_cur_pal, fmt, w, h, pitch, linear, transpose, flat, 0 };
    return xv_texture_decode(&job);
}
/* Comparison override is changed only after the render queue drains. Cache
 * variants keep both layouts immutable until the normal texture-pool purge. */
#ifndef XV_RGBA_SWIZZLED_DEFAULT
#define XV_RGBA_SWIZZLED_DEFAULT 0
#endif
#if XV_RGBA_SWIZZLED_DEFAULT != 0 && XV_RGBA_SWIZZLED_DEFAULT != 1
#error "XV_RGBA_SWIZZLED_DEFAULT must be 0 or 1"
#endif
static int g_rgba_swizzled_override=-1;
static int ui_rgba_swizzled(void)
{
    static int configured=-1;
    if (configured<0) {
        configured=xv_quality_int("XV_RGBA_SWIZZLED",XV_RGBA_SWIZZLED_DEFAULT,0,1);
        UI_LOG("[rgba-layout] startup %s (build default %d); eligible decoded textures only\n",
               configured ? "swizzled" : "linear", XV_RGBA_SWIZZLED_DEFAULT);
    }
    return g_rgba_swizzled_override<0 ? configured : g_rgba_swizzled_override;
}
void xv_ui_gxm_rgba_layout_override(int enabled)
{
    g_rgba_swizzled_override=enabled<0 ? -1 : !!enabled;
    UI_LOG("[rgba-layout] decoded power-of-two textures %s; immutable cache variants\n",
           ui_rgba_swizzled() ? "swizzled" : "linear");
}
static int ui_extended_bc(void)
{
    static int option=-1;
    if (option<0) option=xv_quality_int("XV_EXTENDED_BC",0,0,1);
    return option;
}
static int ui_reorder_bc(const uint8_t *src,uint8_t *dst,unsigned fmt,unsigned w,unsigned h)
{
    xv_texture_job job={src,(uint32_t *)dst,NULL,fmt,w,h,0,0,0,0,1};
    return xv_texture_decode(&job);
}

/* ---- texture control words: CPU-decode Xbox atlas -> linear RGBA8, cached ----------------------- */
static int xbox_fmt_to_gxm(unsigned fmt, SceGxmTextureFormat *out, int *linear, int *bc)
{
    *linear = 0; *bc = 0;
    switch (fmt) {
    case 0x06: *out = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; return 0;   /* A8R8G8B8 swizzled */
    case 0x07: *out = SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB; return 0;   /* X8R8G8B8 swizzled */
    case 0x05: *out = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB;    return 0;   /* R5G6B5   swizzled */
    case 0x02: *out = SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ARGB; return 0;   /* A1R5G5B5 swizzled */
    case 0x03: *out = SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1RGB; return 0;   /* X1R5G5B5 swizzled */
    case 0x04: *out = SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ARGB; return 0;   /* A4R4G4B4 swizzled */
    case 0x00: *out = SCE_GXM_TEXTURE_FORMAT_U8_1RRR;       return 0;   /* L8       swizzled */
    case 0x01: *out = SCE_GXM_TEXTURE_FORMAT_U8_RRRR;       return 0;   /* AL8      swizzled (alpha = luminance) */
    case 0x0B: *out = SCE_GXM_TEXTURE_FORMAT_U8_1RRR;       return 0;   /* P8 (treated as L8) swizzled */
    case 0x13: *out = SCE_GXM_TEXTURE_FORMAT_U8_1RRR;       *linear = 1; return 0;   /* LIN_L8 */
    case 0x19: *out = SCE_GXM_TEXTURE_FORMAT_U8_R000;       return 0;   /* A8       swizzled */
    case 0x1F: *out = SCE_GXM_TEXTURE_FORMAT_U8_R000;       *linear = 1; return 0;   /* LIN_A8 */
    case 0x1B: *out = SCE_GXM_TEXTURE_FORMAT_U8_RRRR;       *linear = 1; return 0;   /* LIN_AL8 */
    case 0x1A: *out = SCE_GXM_TEXTURE_FORMAT_U8U8_GRRR;     return 0;   /* A8L8     swizzled */
    case 0x20: *out = SCE_GXM_TEXTURE_FORMAT_U8U8_GRRR;     *linear = 1; return 0;   /* LIN_A8L8 */
    case 0x0C: *out = SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;     *bc = 1; return 0;   /* DXT1 */
    case 0x0E: *out = SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR;     *bc = 1; return 0;   /* DXT3 */
    case 0x0F: *out = SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR;     *bc = 1; return 0;   /* DXT5 */
    case 0x12: *out = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB; *linear = 1; return 0;   /* LIN_A8R8G8B8 */
    case 0x1E: *out = SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB; *linear = 1; return 0;   /* LIN_X8R8G8B8 */
    case 0x11: *out = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB;    *linear = 1; return 0;   /* LIN_R5G6B5 */
    case 0x3F: *out = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR; *linear = 1; return 0;   /* LIN_A8B8G8R8 */
    default:   return -1;
    }
}

extern uint64_t xk_os_monotonic_us(void);
static uint64_t g_dec_us; static unsigned g_dec_n;   /* per-frame texture decode/upload cost (reset by the frame-time log) */
static uint32_t ui_tex_hash(const void *p, uint32_t bytes)
{   /* FNV-1a over dwords, striding through big textures (a font cache is 32 KB: hashed fully) */
    const uint32_t *w = (const uint32_t *)p; uint32_t n = bytes / 4, step = n > 8192 ? n / 8192 : 1, h = 2166136261u;
    for (uint32_t i = 0; i < n; i += step) h = (h ^ w[i]) * 16777619u;
    return h;
}
/* P8 bindings repeatedly hash the same 256 palette words. Compare every byte
 * before reusing the FNV result, so CPU writes within a frame, address reuse
 * and physical aliases remain visible. The comparison has no dependent
 * multiply chain. Four slots tolerate the usual alternating palette bindings. */
static struct { uint32_t words[256], sum; int valid; } g_palette_hash[4];
static unsigned g_palette_reused, g_palette_hashed;
static int g_palette_override=-1;
void xv_palette_cache_override(int enabled) { g_palette_override=enabled; }
static uint32_t ui_palette_hash(const uint32_t *palette)
{
    static int enabled = -1;
    if (enabled < 0) { const char *e=getenv("XV_PALETTE_HASH_CACHE"); enabled=!e || atoi(e)!=0; }
    int use_cache=g_palette_override<0?enabled:g_palette_override;
    unsigned slot=((uintptr_t)palette >> 10) & 3u;
    if (use_cache && g_palette_hash[slot].valid &&
        !memcmp(g_palette_hash[slot].words,palette,sizeof g_palette_hash[slot].words)) {
        g_palette_reused++;
        return g_palette_hash[slot].sum;
    }
    g_palette_hashed++;
    if (!use_cache) return ui_tex_hash(palette,1024) | 1u;
    memcpy(g_palette_hash[slot].words,palette,sizeof g_palette_hash[slot].words);
    g_palette_hash[slot].sum=ui_tex_hash(g_palette_hash[slot].words,1024) | 1u;
    g_palette_hash[slot].valid=1;
    return g_palette_hash[slot].sum;
}
static unsigned ui_fmt_bpp(unsigned fmt)
{
    switch (fmt) {
    case 0x00: case 0x01: case 0x0B: case 0x19: case 0x13: case 0x1F: case 0x1B: return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x1A: case 0x20: case 0x10: case 0x11: case 0x1C: case 0x1D: return 2;
    default: return 4;
    }
}
static int ui_is_pow2(unsigned v) { return v && !(v & (v - 1)); }

/* coverage != 0: the UI quad path wants luminance-only formats (L8) as RGB=white, A=L - its fixed fragment
 * programs modulate by alpha and cannot read .b the way Halo's combiners do for the font cache.  The mesh
 * path (real combiner programs) gets the faithful NV2A sample (L,L,L,1).  Cached separately. */
static const SceGxmTexture *ui_texture_for_pal(uint32_t hdr, int coverage, uint32_t pal_guest);
static const SceGxmTexture *ui_texture_for(uint32_t hdr, int coverage) { return ui_texture_for_pal(hdr, coverage, 0); }
static const SceGxmTexture *ui_texture_for_pal(uint32_t hdr, int coverage, uint32_t pal_guest)
{
    if (!hdr) return NULL;
    extern const SceGxmTexture *xv_d3d_render_target_texture(uint32_t hdr);
    const SceGxmTexture *rt = xv_d3d_render_target_texture(hdr);
    if (rt) return rt;
    uint32_t data = guest_u32(hdr + 4);              /* X_D3DPixelContainer.Data (guest phys) */
    uint32_t fmtword = guest_u32(hdr + 12);
    uint32_t sizeword = guest_u32(hdr + 16);
    if (!data) return NULL;

    unsigned fmt0 = (fmtword >> 8) & 0xFF;
    /* P8 with a palette bound: decode through it (bump maps hold normals in the palette); its identity is
     * part of the cache key.  Without a palette P8 falls back to luminance. */
    g_cur_pal = NULL; g_cur_palsum = 0;
    if (fmt0 == 0x0B && pal_guest) { g_cur_pal = (const uint32_t *)X_G(pal_guest); g_cur_palsum = ui_palette_hash(g_cur_pal); }
    int lum_only = (fmt0 == 0x00 || fmt0 == 0x13 || (fmt0 == 0x0B && !g_cur_pal));        /* L8 swz, LIN_L8, P8-as-L8 */
    /* Alpha-only font fallback is a UI policy, never a mutation of samples
     * consumed by real material combiners (smoke, dark surfaces, meter ramps). */
    int alpha_coverage = coverage && !lum_only;
    if (coverage) fmtword |= 0x80000000u;                                 /* separate cache identity */
    /* Textures the game creates at run time (headers live in the kernel heap, e.g. the 128x128 font cache
     * that glyphs are rasterised into on demand) change under us: hash their contents once per frame and
     * re-decode on change.  Map textures (headers in the game heap) are immutable. */
    int dynamic = hdr >= 0x03D00000u;
    /* Map textures are NOT immutable for us: Halo streams bitmap pixels into the game heap asynchronously,
     * and the game binds a bitmap before its read has landed (the first bind of the a10 hull plating and the
     * Blood Gulch base walls saw stale/unstreamed bytes: "striped" textures for the rest of the level).  Real
     * hardware re-reads memory every frame, so re-hash the source: every frame for dynamic textures, and on a
     * 1-in-16 rotating cadence for map textures until 24 consecutive checks come back unchanged. */
    unsigned bucket = ((data ^ (fmtword * 2654435761u)) >> 7) & 1023u;
    ui_tex_entry *re = NULL;                       /* set when a cached texture's source changed: re-decode in place */
    for (int16_t i = g.texhash[bucket]; i >= 0; i = g.texcache[i].next)
        if (g.texcache[i].valid && g.texcache[i].data == data && g.texcache[i].fmtword == fmtword && g.texcache[i].palsum == g_cur_palsum) {
            ui_tex_entry *e = &g.texcache[i];
            if (e->rgba_layout && e->rgba_layout != (ui_rgba_swizzled() ? 2 : 1)) continue;
            /* tiny textures (Halo's 4x4 stage dummies, 1D ramps) are rewritten by the CPU, never by a file read,
             * so the read-based invalidation cannot catch them: re-hash them every frame (64 bytes, free) */
            int due = e->bytes && e->checked != g.rec_frame &&
                      (dynamic || e->dirty || e->bytes <= 1024 || (e->stable < 24 && ((g.rec_frame + (unsigned)i) & 15u) == 0));
            if (due) {
                e->checked = g.rec_frame;
                uint32_t sum = ui_tex_hash(X_G(0x80000000u | e->source_data), e->bytes);
                if (sum != e->sum) { re = e; static unsigned n; if (n++ < 12) UI_LOG("tex %08X fmt %02X changed (check %u%s): refreshing upload\n", data, (fmtword >> 8) & 0xFF, e->stable, e->dirty ? ", file read" : ""); e->dirty = 0; break; }   /* stale: re-decode below */
                e->dirty = 0;
                if (!dynamic) e->stable++;
            }
            return &e->tex;
        }

    for (unsigned i = 0; i < g.nbad; ++i) if (g.bad[i].data == data && g.bad[i].fmtword == fmtword) return NULL;
#define UI_TEX_FAIL() do { if (g.nbad < UI_TEX_BAD) { g.bad[g.nbad].data = data; g.bad[g.nbad].fmtword = fmtword; g.nbad++; } return NULL; } while (0)
    SceGxmTextureFormat gf; int linear = 0, bc = 0;
    unsigned fmt = fmt0;
    if (xbox_fmt_to_gxm(fmt, &gf, &linear, &bc) != 0) { UI_LOG("unhandled tex fmt %02X\n", fmt); UI_TEX_FAIL(); }

    unsigned w, h, pitch = 0, mips = ((fmtword >> 16) & 0xF); if (!mips) mips = 1;
    if (sizeword) { w = (sizeword & 0xFFF) + 1; h = ((sizeword >> 12) & 0xFFF) + 1; pitch = (((sizeword >> 24) & 0xFF) + 1) * 64; linear = 1; }
    else          { w = 1u << ((fmtword >> 20) & 0xF); h = 1u << ((fmtword >> 24) & 0xF); }
    if (!w || !h || w > 4096 || h > 4096) UI_TEX_FAIL();
    if (g.texcount >= UI_TEX_CACHE) { g.tex_purge = 1; return NULL; }

    /* Cube maps (Format bit 2): six faces back to back, each a full swizzled/DXT mip chain padded to 128
     * bytes. Preserve complete BC chains when enabled; otherwise build RGBA mips. */
    if ((fmtword & 4u) && !sizeword && w == h && ui_is_pow2(w) && w >= 8 && w <= 256) {
        int isdxt_c = (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F); unsigned bs_c = (fmt == 0x0C) ? 8 : 16, bpp_c = ui_fmt_bpp(fmt);
        uint32_t face_bytes = 0; { unsigned lw = w, lh = h; for (unsigned l = 0; l < mips; ++l) { face_bytes += isdxt_c ? ((lw + 3) / 4) * ((lh + 3) / 4) * bs_c : lw * lh * bpp_c; lw = lw > 1 ? lw >> 1 : 1; lh = lh > 1 ? lh >> 1 : 1; } }
        face_bytes = (face_bytes + 127) & ~127u;
        /* GXM cube layout (Vita3K texture cache, matches the hardware): unless the control word says "no mip
         * chain" (mip_count 0xF), every face is laid out as if it carried the WHOLE chain down to 1x1 and the
         * faces are aligned to 2 KB (32-bit faces >= 16x16).  We used to pack six level-0 faces back to back,
         * so faces 1..5 were fetched from the wrong offsets: the normalisation cube gave bogus light vectors
         * (black contour bands on bump-lit cliffs) and reflections showed the wrong sky.  Upload the full,
         * box-filtered chain per face - that also gives filtered reflections at a distance. */
        unsigned levels = 1; { unsigned lw = w; while (lw > 1) { lw >>= 1; levels++; } }
        int keep_bc=ui_extended_bc() && isdxt_c && mips>=levels;
        uint32_t face_stride = 0; { unsigned lw = w; for (unsigned l = 0; l < levels; ++l) { face_stride += keep_bc ? xv_bc_level_bytes(lw,lw,bs_c) : lw * lw * 4u; lw >>= 1; } }
        if (w >= (keep_bc ? 32u : 16u)) face_stride = ALIGN_UP(face_stride, 2048);
        uint32_t need = ALIGN_UP(6u * face_stride, 64);
        if (g.dec_off + need > g.dec_cap) { g.tex_purge = 1; return NULL; }
        if (re) re->valid = 0;                                    /* source changed: drop the stale entry, build a fresh one (its pool space is reclaimed at the next purge) */
        uint32_t *dst = (uint32_t *)(g.dec_base + g.dec_off);
        static uint32_t tmp[256 * 256], tmp2[128 * 128];
        const uint8_t *base = X_G(0x80000000u | data);
        memset(dst, 0, need);
        for (unsigned f = 0; f < 6; ++f) {
            if (keep_bc) {
                const uint8_t *in=base+f*face_bytes;
                uint8_t *out=(uint8_t *)dst+f*face_stride;
                for (unsigned l=0,lw=w;l<levels;++l,lw>>=1) {
                    unsigned bytes=xv_bc_level_bytes(lw,lw,bs_c);
                    if (ui_reorder_bc(in,out,fmt,lw,lw)) UI_TEX_FAIL();
                    in+=bytes;out+=bytes;
                }
                continue;
            }
            if (ui_decode(base + f * face_bytes, fmt, w, h, w * bpp_c, 0, tmp) != 0) { UI_LOG("cube face decode failed fmt %02X\n", fmt); UI_TEX_FAIL(); }
            uint32_t *lvl = (uint32_t *)((uint8_t *)dst + f * face_stride); uint32_t *cur = tmp, *nxt = tmp2;
            for (unsigned l = 0, lw = w; l < levels; ++l, lw >>= 1) {
                for (unsigned i = 0; i < lw * lw; ++i) { unsigned x, y; gxm_unswz(lw, lw, i, &x, &y); lvl[i] = cur[y * lw + x]; }
                lvl += lw * lw;
                if (lw > 1) {                                             /* next level: 2x2 box filter per channel */
                    unsigned nw = lw >> 1;
                    for (unsigned y = 0; y < nw; ++y) for (unsigned x = 0; x < nw; ++x) {
                        uint32_t a = cur[(2 * y) * lw + 2 * x], b = cur[(2 * y) * lw + 2 * x + 1], c2 = cur[(2 * y + 1) * lw + 2 * x], d2 = cur[(2 * y + 1) * lw + 2 * x + 1], o = 0;
                        for (unsigned sh = 0; sh < 32; sh += 8) o |= ((((a >> sh) & 0xFF) + ((b >> sh) & 0xFF) + ((c2 >> sh) & 0xFF) + ((d2 >> sh) & 0xFF) + 2) >> 2) << sh;
                        nxt[y * nw + x] = o;
                    }
                    uint32_t *t = cur; cur = nxt; nxt = t;
                }
            }
        }
        { static int dump = -1; if (dump < 0) dump = getenv("XV_TEXDUMP") != NULL;
          if (dump) { char path[128]; sceIoMkdir("ux0:data/xita/texdump", 0777);
            sceClibSnprintf(path, sizeof path, "ux0:data/xita/texdump/cube_%08X_%02X_%ux%u.ppm", data, fmt, w, h);
            SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
            if (fd >= 0) { char hdr[32]; int n = sceClibSnprintf(hdr, sizeof hdr, "P6\n%u %u\n255\n", w * 6, h); sceIoWrite(fd, hdr, n);
              static uint8_t row[256 * 6 * 3];
              for (unsigned y = 0; y < h; ++y) { for (unsigned f = 0; f < 6; ++f) { ui_decode(base + f * face_bytes, fmt, w, h, w * bpp_c, 0, tmp);
                  for (unsigned x = 0; x < w; ++x) { uint32_t p = tmp[y * w + x]; row[(f * w + x) * 3] = p & 0xFF; row[(f * w + x) * 3 + 1] = (p >> 8) & 0xFF; row[(f * w + x) * 3 + 2] = (p >> 16) & 0xFF; } }
                sceIoWrite(fd, row, w * 6 * 3); }
              sceIoClose(fd); } } }
        xv_gpu_flush(dst, need);
        ui_tex_entry *e = &g.texcache[g.texcount];
        int err = sceGxmTextureInitCube(&e->tex, dst, keep_bc ? gf : SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitCube(%ux%u fmt %02X, %u mips) failed 0x%08X\n", w, h, fmt, levels, err); UI_TEX_FAIL(); }
        sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        g.dec_off += need;
        e->data = data; e->fmtword = fmtword; e->palsum = g_cur_palsum; e->valid = 1; g.texcount++;
        e->opaque = e->pinned = 0; /* Cube reinterpretation is not covered by the initial proof. */
        e->next = g.texhash[bucket]; g.texhash[bucket] = (int16_t)(e - g.texcache);
        e->source_data = data;
        e->bytes = 6u * face_bytes; if (e->bytes > 512 * 1024) e->bytes = 512 * 1024; e->sum = ui_tex_hash(base, e->bytes); e->checked = g.rec_frame; e->stable = 0; e->dirty = 0;
        { static unsigned n; if (n++ < 8) UI_LOG("cube map fmt %02X %ux%u x6 (face stride %u) -> cube texture\n", fmt, w, h, face_bytes); }
        if (keep_bc) UI_LOG("[texture-bc] cube %ux%u: %u mips, %u bytes, face stride %u\n",w,h,levels,need,face_stride);
        sceGxmTextureSetMinFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
        return &e->tex;
    }
    int isdxt = (fmt == 0x0C || fmt == 0x0E || fmt == 0x0F);
    unsigned bs = (fmt == 0x0C) ? 8 : 16, bpp = ui_fmt_bpp(fmt);
    const uint8_t *src = X_G(0x80000000u | data);
    /* Skip down the mip chain (levels are stored back to back) until the level fits UI_TEX_MAXDIM:
     * a 1024x1024 DXT1 level texture becomes 32 KB instead of 4 MB of decoded RGBA. */
    static unsigned maxdim; if (!maxdim) { const char *e = getenv("XV_TEX_MAXDIM"); maxdim = e ? (unsigned)atoi(e) : UI_TEX_MAXDIM; if (maxdim < 16) maxdim = UI_TEX_MAXDIM; }
    if (!sizeword) {
        while ((w > maxdim || h > maxdim) && mips > 1 && w > 4 && h > 4) {
            src += isdxt ? ((w + 3) / 4) * ((h + 3) / 4) * bs : w * h * bpp;
            w >>= 1; h >>= 1; mips--;
        }
    }

    ui_tex_entry *e = re ? re : &g.texcache[g.texcount];
    /* A command may already rely on this upload being opaque. Never overwrite
     * its pixels after granting that proof, even within the recording frame.
     * The normal pool purge drains published draws before reclaiming storage. */
    int allocate = 1; /* All recorded uploads are immutable until pool retirement. */
    int opaque = 0; unsigned rgba_layout = 0;
    int err, as_bc = 0; unsigned source_consumed=0;
    /* Rectangular BC and full cube chains are opt-in pending hardware comparison.
     * Both reorder blocks using the same tested Y-even/X-odd GXM layout. */
    if (isdxt && (w == h || ui_extended_bc()) && ui_is_pow2(w) && ui_is_pow2(h) && w >= 4 && h >= 4) {
        /* Upload DXT as-is: NV2A stores 4x4 blocks row-major, GXM's swizzled layout wants the blocks
         * twiddled (PowerVR order, see gxm_unswz), so reorder 8/16-byte blocks - no decompression. */
        /* whole mip chain down to 4x4 (Xbox stores the levels back to back; GXM swizzled BC levels are
         * concatenated too): without mips the hull fly-by at grazing angles aliased into streaks */
        /* XV_BC_MIPS=0 (default until verified on hardware): upload level 0 only - the chained-level layout
         * may be what faulted the GPU at the main menu on 2026-09-02 21:50 */
        static int bcmips = -1; if (bcmips < 0) { const char *e = getenv("XV_BC_MIPS"); bcmips = e ? atoi(e) : 0; }
        unsigned levels = 0; uint32_t need = 0;
        int rectangular=w!=h;
        { unsigned lw=w,lh=h,requested=rectangular ? mips : bcmips ? mips : 1;
          while (levels<requested && (rectangular || (lw>=4 && lh>=4))) {
              need+=xv_bc_level_bytes(lw,lh,bs); ++levels;
              if (lw==1 || lh==1) break;
              lw>>=1;lh>>=1;
          } }
        source_consumed=need;
        need = ALIGN_UP(need, 64);
        if (allocate && g.dec_off + need > g.dec_cap) { g.tex_purge = 1; return NULL; }
        uint8_t *dst = !allocate ? (uint8_t *)sceGxmTextureGetData(&e->tex) : g.dec_base + g.dec_off, *o = dst; const uint8_t *lsrc = src;
        for (unsigned l = 0, lw = w, lh = h; l < levels; ++l, lw >>= 1, lh >>= 1) {
            unsigned bytes=xv_bc_level_bytes(lw,lh,bs);
            if (ui_reorder_bc(lsrc,o,fmt,lw,lh)) UI_TEX_FAIL();
            o+=bytes;lsrc+=bytes;
        }
        opaque = xv_alpha_bc_opaque(dst, source_consumed, fmt);
        xv_gpu_flush(dst, need);
        err = sceGxmTextureInitSwizzled(&e->tex, dst, gf, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitSwizzled(%02X %ux%u, %u mips) failed 0x%08X\n", fmt, w, h, levels, err); UI_TEX_FAIL(); }
        if (levels > 1) sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        if (allocate) g.dec_off += need;
        as_bc = levels > 1 ? 2 : 1;
        if (rectangular) { static unsigned n; if (n++<16) UI_LOG("[texture-bc] rectangle %ux%u: %u mips, %u bytes\n",w,h,levels,need); }
    } else {
        /* decoded RGBA + a box-filtered mip chain (levels packed after level 0, as GXM lays out linear
         * mips): without mips the 1024x512 hull plating aliased into streaks at grazing angles, and mips
         * cut texture bandwidth on the real GPU */
        /* Only build a chain when the Xbox texture has one: lightmap atlases (and other single-level bitmaps)
         * pack tiny per-face patches with black gutters, and a box-filtered mip blends the patches into the
         * gutters - whole floor faces went black at a distance in the a10 cryo bay, and Blood Gulch terrain
         * showed dark blocky patches. */
        unsigned levels = 1; if (mips > 1) { unsigned lw = w, lh = h; while (lw > 1 && lh > 1 && levels < 12) { lw >>= 1; lh >>= 1; levels++; } }
        /* GXM linear textures: every level's rows are laid out with the width padded to 8 texels (Vita3K's
         * texture cache: align_width = 8 for SCE_GXM_TEXTURE_LINEAR).  Packing 4x4 stage dummies (and the
         * 4/2/1-wide tails of every chain) unpadded made the GPU read rows 2..4 and the small mips from the
         * next cache entry: Halo's "neutral" 4x4 detail dummies sampled as garbage - black tiles on the a10
         * cryo-bay floor, since the base pass multiplies by them. */
#define LIN_PAD(x) (((x) + 7u) & ~7u)
        uint32_t need = 0; { unsigned lw = w, lh = h; for (unsigned l = 0; l < levels; ++l) { need += LIN_PAD(lw) * lh * 4u; lw = lw > 1 ? lw >> 1 : 1; lh = lh > 1 ? lh >> 1 : 1; } }
        need = ALIGN_UP(need, 64);
        uint32_t swizzled_bytes=xv_rgba_swizzled_bytes(w,h,levels);
        rgba_layout=swizzled_bytes ? (ui_rgba_swizzled() ? 2 : 1) : 0;
        /* A private cached buffer lives through worker completion. No shared
         * scratch, and no allocation on an unchanged texture's cache-hit path. */
        uint32_t *scratch=rgba_layout==2 ? malloc(need) : NULL;
        int swizzled=scratch!=NULL;
        if (rgba_layout==2 && !swizzled) {
            static unsigned failures;
            if (failures++<4) UI_LOG("[rgba-layout] scratch allocation failed; retaining linear layout for this version\n");
        }
        if (swizzled) need=ALIGN_UP(swizzled_bytes,64);
        if (g.dec_off + need > g.dec_cap) { free(scratch); g.tex_purge = 1; return NULL; }
        uint32_t *gpu=(uint32_t *)(g.dec_base+g.dec_off);
        uint32_t *dst=swizzled ? scratch : gpu;
        uint64_t dec_t0 = xk_os_monotonic_us();
        if (ui_decode(src, fmt, w, h, pitch ? pitch : w * bpp, linear, dst) != 0) {
            UI_LOG("unhandled tex fmt %02X (%ux%u)\n", fmt, w, h); free(scratch); UI_TEX_FAIL();
        }
        {   /* XV_TEXDUMP=<min width>: write decoded textures as PPM to ux0:data/xita/texdump/ */
            static int dump_min = -1; if (dump_min < 0) { const char *e = getenv("XV_TEXDUMP"); dump_min = e ? atoi(e) : 0; if (e && dump_min <= 0) dump_min = 1; }
            if (dump_min > 0 && (int)w >= dump_min) {
                char path[128]; sceIoMkdir("ux0:data/xita/texdump", 0777);
                sceClibSnprintf(path, sizeof path, "ux0:data/xita/texdump/%08X_%02X_%ux%u_m%u.ppm", data, fmt, w, h, mips);
                SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
                if (getenv("XV_TEXDUMP_RAW")) {          /* the source bytes too (offline layout experiments) */
                    char rp[128]; sceClibSnprintf(rp, sizeof rp, "ux0:data/xita/texdump/%08X_%02X_%ux%u_m%u_f%08X_s%08X.raw", data, fmt, w, h, mips, fmtword, sizeword);
                    SceUID rf = sceIoOpen(rp, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
                    if (rf >= 0) { sceIoWrite(rf, src, isdxt ? ((w + 3) / 4) * ((h + 3) / 4) * bs : (pitch ? pitch : w * bpp) * h); sceIoClose(rf); }
                }
                if (fd >= 0) { char hdr[32]; int n = sceClibSnprintf(hdr, sizeof hdr, "P6\n%u %u\n255\n", w, h); sceIoWrite(fd, hdr, n);
                    static uint8_t row[4096 * 3]; for (unsigned y = 0; y < h; ++y) { for (unsigned x = 0; x < w && x < 4096; ++x) { uint32_t p = dst[y * w + x]; row[x * 3] = p & 0xFF; row[x * 3 + 1] = (p >> 8) & 0xFF; row[x * 3 + 2] = (p >> 16) & 0xFF; } sceIoWrite(fd, row, (w < 4096 ? w : 4096) * 3); }
                    sceIoClose(fd); }
            }
        }
        if (coverage && lum_only) {                       /* L8 for the UI path: coverage -> alpha, RGB white */
            for (unsigned i = 0, N = w * h; i < N; ++i) dst[i] = ((dst[i] & 0xFFu) << 24) | 0x00FFFFFFu;
        } else if (alpha_coverage) {
            /* Font/text atlases with glyph coverage in ALPHA and black RGB: texmod (tex.rgb*color) would show
             * nothing, so set RGB=white and let tex.a drive the blend.  Never for luminance formats (their
             * synthesized alpha is 255 everywhere, which used to turn the small-font cache into a white square). */
            unsigned rgbnz = 0, anz = 0, N = w * h;
            for (unsigned i = 0; i < N; ++i) { if (dst[i] & 0x00FFFFFFu) rgbnz++; if (dst[i] >> 24) anz++; }
            if (anz > 16 && rgbnz * 20u < anz) for (unsigned i = 0; i < N; ++i) dst[i] |= 0x00FFFFFFu;
        }
        if (w < 8) {                                      /* level 0 was decoded with stride w: spread the rows to the 8-texel stride, last row first */
            for (unsigned y = h; y-- > 1;) memmove(dst + y * 8u, dst + y * w, w * 4u);
        }
        {   /* mip chain: 2x2 box filter per channel, every level at its padded row stride */
            const uint32_t *prev = dst; unsigned pw = w, ph = h; uint32_t *lvl = dst + LIN_PAD(w) * h;
            for (unsigned l = 1; l < levels; ++l) {
                unsigned nw = pw >> 1, nh = ph >> 1, ps = LIN_PAD(pw), ns = LIN_PAD(nw);
                for (unsigned y = 0; y < nh; ++y) for (unsigned x = 0; x < nw; ++x) {
                    uint32_t a = prev[(2 * y) * ps + 2 * x], b = prev[(2 * y) * ps + 2 * x + 1], c2 = prev[(2 * y + 1) * ps + 2 * x], d2 = prev[(2 * y + 1) * ps + 2 * x + 1];
                    uint32_t o = 0;
                    for (unsigned sh = 0; sh < 32; sh += 8) o |= ((((a >> sh) & 0xFF) + ((b >> sh) & 0xFF) + ((c2 >> sh) & 0xFF) + ((d2 >> sh) & 0xFF) + 2) >> 2) << sh;
                    lvl[y * ns + x] = o;
                }
                prev = lvl; lvl += ns * nh; pw = nw; ph = nh;
            }
        }
        opaque = xv_alpha_rgba_opaque(dst, w, h, levels);
        if (swizzled) xv_rgba_swizzle(dst,gpu,w,h,levels);
        free(scratch);
        xv_gpu_flush(gpu, need);
        err = swizzled ? sceGxmTextureInitSwizzled(&e->tex, gpu, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels)
                      : sceGxmTextureInitLinear(&e->tex, gpu, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitRGBA(%ux%u, %u mips, swizzled %d) failed 0x%08X\n", w, h, levels, swizzled, err); UI_TEX_FAIL(); }
        sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        if (allocate) g.dec_off += need;
        g_dec_us += xk_os_monotonic_us() - dec_t0; g_dec_n++;
    }
    e->data = data; e->fmtword = fmtword; e->palsum = g_cur_palsum; e->valid = 1;
    e->opaque = opaque; e->pinned = 0; e->rgba_layout = rgba_layout;
    if (!re) { g.texcount++; e->next = g.texhash[bucket]; g.texhash[bucket] = (int16_t)(e - g.texcache); }
    e->bytes = 0; e->sum = 0; e->checked = g.rec_frame; e->stable = 0;
    {                                                                 /* size of the source level we consumed */
        /* Keep validation and file-read invalidation on the same mip that was
         * decoded. Comparing its hash with level 0 falsely marked unchanged
         * large textures dirty on every re-check. */
        e->source_data = data + (uint32_t)(src - (const uint8_t *)X_G(0x80000000u | data));
        e->bytes = source_consumed ? source_consumed : isdxt ? ((w + 3) / 4) * ((h + 3) / 4) * bs : (pitch ? pitch : w * bpp) * h;
        if (e->bytes > 512 * 1024) e->bytes = 512 * 1024;
        e->sum = ui_tex_hash(src, e->bytes);
    }
    { static unsigned n; if (n++ < 24) UI_LOG("tex fmt %02X %ux%u -> %s %s (%u KB used)\n", fmt, w, h, as_bc ? "BC" : "RGBA", sceGxmTextureGetType(&e->tex)==SCE_GXM_TEXTURE_SWIZZLED ? "swizzled" : "linear", g.dec_off >> 10); }
    sceGxmTextureSetMinFilter(&e->tex, as_bc == 1 ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR);
    sceGxmTextureSetMagFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
    {   /* diagnostic: XV_TEX_POINT=<xbox fmt hex> point-samples every texture of that format (lightmap atlas checks) */
        static int pf = -2; if (pf == -2) { const char *e2 = getenv("XV_TEX_POINT"); pf = e2 ? (int)strtoul(e2, NULL, 16) : -1; }
        if ((int)fmt == pf) { sceGxmTextureSetMinFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_POINT); sceGxmTextureSetMagFilter(&e->tex, SCE_GXM_TEXTURE_FILTER_POINT); }
    }
    sceGxmTextureSetUAddrMode(&e->tex, SCE_GXM_TEXTURE_ADDR_REPEAT);
    sceGxmTextureSetVAddrMode(&e->tex, SCE_GXM_TEXTURE_ADDR_REPEAT);
    return &e->tex;
}

/* The pool is a bump allocator: when a frame could not fit its textures, wait for the GPU and start over
 * (the working set is rebuilt over the next frame).  Rate-limited so a set that never fits does not
 * re-decode every frame. */
/* A new map's tag data lands at the same guest addresses as the old one, so the GXM texture cache
 * (keyed by guest address + format word) would keep serving the previous level's pixels for entries that
 * already passed their re-validation quota (Blood Gulch grass on a10's cryo-bay walls).  The kernel calls
 * this when a map's tag region is read; the wipe happens after the current frame. */
void xv_ui_gxm_request_texture_purge(void) { g.tex_purge = 1; }
/* A file read landed in guest memory: any cached texture whose source bytes overlap it must be re-hashed at
 * its next bind.  Halo streams bitmap pixels into a fixed heap region and reuses the slots, so after a long
 * cinematic the "stable" entries (no longer re-checked) served the previous occupant: Cortana's green
 * hologram scan-lines on every cryo-bay wall after the a10 intro. */
void xv_ui_gxm_invalidate_range(uint32_t va, uint32_t len)
{
    uint32_t lo = va & 0x7FFFFFFFu, hi = lo + len; unsigned n = 0;
    for (int i = 0; i < g.texcount; ++i) {
        ui_tex_entry *e = &g.texcache[i];
        if (!e->valid || !e->bytes) continue;
        uint32_t a = e->source_data & 0x7FFFFFFFu, b = a + e->bytes;
        if (a < hi && lo < b) { e->dirty = 1; n++; }
    }
    if (n) { static unsigned m; if (m++ < 8) UI_LOG("file read %08X+%u overlaps %u cached texture(s): re-check on next bind\n", va, len, n); }
}
static void ui_tex_purge_if_needed(unsigned frame)
{
    if (!g.tex_purge) return;
    g.tex_purge = 0;
    if (frame - g.tex_purge_frame < 8 && g.tex_purges) return;
    { extern void xv_present_drain(void) __attribute__((weak)); if (xv_present_drain) xv_present_drain(); }   /* the pump may still be sampling this pool */
    sceGxmDisplayQueueFinish();
    UI_LOG("texture purge #%u at frame %u (%u textures, %u KB, %u bad)\n", ++g.tex_purges, frame, g.texcount, g.dec_off >> 10, g.nbad);
    g.texcount = 0; g.dec_off = 0; g.nbad = 0; g.tex_purge_frame = frame;
    memset(g.texhash, 0xFF, sizeof g.texhash);
}

const SceGxmTexture *xv_ui_gxm_texture(uint32_t hdr) { return g.ready ? ui_texture_for(hdr, 0) : NULL; }
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t hdr, uint32_t pal_guest) { return g.ready ? ui_texture_for_pal(hdr, 0, pal_guest) : NULL; }

/* Called only on the recording thread, immediately after live resolution.
 * Accept the cache owner's descriptor itself, never a copied/forged control
 * word or a render-target alias. The draw stores a value, not this mutable entry. */
int xv_ui_gxm_texture_opaque(const SceGxmTexture *texture)
{
    uintptr_t base = (uintptr_t)&g.texcache[0].tex, ptr = (uintptr_t)texture;
    if (!texture || ptr < base || (ptr - base) % sizeof(ui_tex_entry)) return 0;
    uintptr_t i = (ptr - base) / sizeof(ui_tex_entry);
    if (i >= g.texcount) return 0;
    ui_tex_entry *e = &g.texcache[i];
    if (!e->valid || !e->opaque) return 0;
    e->pinned = 1;
    return 1;
}

static int g_texture_filter = -1, g_mip_smooth = -1;
void xv_ui_gxm_set_texture_options(int filter, int mip)
{
    /* Initialize the unchanged half from startup config, on the recording
     * thread. Texture controls are copied for each draw before publication. */
    if(g_texture_filter<0) {
        const char *f=getenv("XV_TEX_FILTER"), *m=getenv("XV_MIP_SMOOTH");
        g_texture_filter=f?atoi(f):0;g_mip_smooth=m?!!atoi(m):1;
        if(g_texture_filter<0 || g_texture_filter>2)g_texture_filter=0;
    }
    if(filter>=0 && filter<=2)g_texture_filter=filter;
    if(mip>=0)g_mip_smooth=!!mip;
}
void xv_ui_gxm_apply_texture_options(SceGxmTexture *texture)
{
    if (g_texture_filter < 0) {
        const char *f = getenv("XV_TEX_FILTER"), *m = getenv("XV_MIP_SMOOTH");
        g_texture_filter = f ? atoi(f) : 0;
        if (g_texture_filter < 0 || g_texture_filter > 2) g_texture_filter = 0;
        g_mip_smooth = m ? atoi(m) != 0 : 1;
        UI_LOG("texture options: filter %d (0 game, 1 point, 2 linear), mip smoothing %s\n",
               g_texture_filter, g_mip_smooth ? "auto" : "off");
    }
    if (g_texture_filter) {
        SceGxmTextureFilter min = sceGxmTextureGetMinFilter(texture);
        int mip = min == SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR || min == SCE_GXM_TEXTURE_FILTER_MIPMAP_POINT;
        sceGxmTextureSetMinFilter(texture, g_texture_filter == 1 ?
            (mip ? SCE_GXM_TEXTURE_FILTER_MIPMAP_POINT : SCE_GXM_TEXTURE_FILTER_POINT) :
            (mip ? SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR : SCE_GXM_TEXTURE_FILTER_LINEAR));
        sceGxmTextureSetMagFilter(texture, g_texture_filter == 1 ? SCE_GXM_TEXTURE_FILTER_POINT : SCE_GXM_TEXTURE_FILTER_LINEAR);
    }
    /* LINEAR_STRIDED uses these bits for stride, and does not support mips. */
    if (!g_mip_smooth && sceGxmTextureGetType(texture) != SCE_GXM_TEXTURE_LINEAR_STRIDED)
        sceGxmTextureSetMipFilter(texture, SCE_GXM_TEXTURE_MIP_FILTER_DISABLED);
}

/* ---- init ------------------------------------------------------------------------------------- */
/* How many texcoord sets each known UI microcode writes (fragment programs are linked per set):
 *   1DAF0284 halo_vs_03 widget (oT0/oT1 smoke scroll, oT2 bitmap), 4469E1F8 halo_vs_04 text (oT0),
 *   BB2F446B halo_vs_38 full-screen (oT0..oT3). */
static int ui_ntex_for(uint32_t fnv)
{
    switch (fnv) { case 0x1DAF0284u: return 3; case 0x4469E1F8u: return 1; case 0xBB2F446Bu: return 4; default: return 1; }
}

static const SceGxmBlendInfo ui_blend = {
    SCE_GXM_COLOR_MASK_ALL,
    SCE_GXM_BLEND_FUNC_ADD, SCE_GXM_BLEND_FUNC_ADD,
    SCE_GXM_BLEND_FACTOR_SRC_ALPHA, SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    SCE_GXM_BLEND_FACTOR_ONE,       SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
};

int xv_ui_gxm_init(void)
{
    memset(&g, 0, sizeof g);
    g.pub = -1;
    memset(g.texhash, 0xFF, sizeof g.texhash);

    /* every recompiled vertex program that consumes the UI declaration */
    for (int i = 0; i < XV_HALO_VS_COUNT && g.nprog < UI_MAX_PROGS; ++i) {
        const xv_vs_desc_t *d = xv_halo_vs[i];
        if (d->decl_va != UI_DECL_VA) continue;
        ui_prog *p = &g.prog[g.nprog];
        if (xv_vshader_load(&p->vs, d) != 0) { UI_LOG("UI vshader %s load failed\n", d->gxp); continue; }
        p->desc = d; p->ntex = ui_ntex_for(d->func_hash);
        for (int st = 0; st < p->ntex && st < (int)UI_MAX_STAGES; ++st) {
            char path[64]; sceClibSnprintf(path, sizeof path, "app0:shaders/xv_ui_t%d.frag.gxp", st);
            p->fs_ok[st] = xv_fshader_load(&p->fs[st], path, &p->vs, &ui_blend) == 0;
        }
        /* the texmod program (texture * color0 on TEXCOORD0) is the stage-0 fallback */
        if (!p->fs_ok[0]) p->fs_ok[0] = xv_fshader_load(&p->fs[0], "app0:shaders/xv_texmod.frag.gxp", &p->vs, &ui_blend) == 0;
        if (!p->fs_ok[0]) { UI_LOG("no fragment program links against %s\n", d->gxp); xv_vshader_unload(&p->vs); continue; }
        UI_LOG("UI program %u: %s fnv %08X c[%d..%d] stages ok %d%d%d%d\n", g.nprog, d->gxp, d->func_hash, d->c_base, d->c_base + d->c_count,
               p->fs_ok[0], p->fs_ok[1], p->fs_ok[2], p->fs_ok[3]);
        g.nprog++;
    }
    if (!g.nprog) { UI_LOG("no UI vertex program (decl %08X) loaded\n", UI_DECL_VA); return -1; }

    /* clear program: full-screen coloured quad, opaque (reuses the runtime's clear/colour shaders) */
    if (xv_vshader_load(&g.clear_vs, &xv_vs_clear) != 0 ||
        xv_fshader_load(&g.clear_fs, "app0:shaders/xv_color.frag.gxp", &g.clear_vs, NULL) != 0) {
        UI_LOG("clear program load failed\n"); return -1;
    }
    const SceGxmBlendInfo panel_blend={
        .colorMask=SCE_GXM_COLOR_MASK_ALL,
        .colorFunc=SCE_GXM_BLEND_FUNC_ADD,.alphaFunc=SCE_GXM_BLEND_FUNC_ADD,
        .colorSrc=SCE_GXM_BLEND_FACTOR_SRC_ALPHA,.colorDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaSrc=SCE_GXM_BLEND_FACTOR_ONE,.alphaDst=SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
    };
    if(xv_fshader_load(&g.settings_fs,"app0:shaders/xv_color.frag.gxp",&g.clear_vs,&panel_blend)!=0)return -1;

    g.vbuf = ui_gpu_alloc(UI_FRAMES * UI_MAX_VERTS * sizeof(ui_vtx), &g.vbuf_uid);
    g.ibuf = ui_gpu_alloc(UI_MAX_QUADS * 6 * sizeof(uint16_t), &g.ibuf_uid);
    g.clrbuf = ui_gpu_alloc(UI_FRAMES * (4 + OVL_MAX_QUADS * 4) * sizeof(clr_vtx), &g.clr_uid);
    g.settingsbuf = ui_gpu_alloc(UI_FRAMES * UI_MAX_QUADS * 4 * sizeof(clr_vtx), &g.settings_uid);
    g.dec_cap = 32 * 1024 * 1024;                                  /* texture pool (BC as-is, others decoded RGBA) */
    g.dec_base = ui_gpu_alloc(g.dec_cap, &g.dec_uid);
    if (!g.vbuf || !g.ibuf || !g.clrbuf || !g.dec_base || !g.settingsbuf) return -1;

    /* static quad index buffer: quad q -> (0,1,2, 0,2,3)+q*4, relative to the batch's stream base */
    for (uint32_t q = 0; q < UI_MAX_QUADS; ++q) {
        uint16_t *ix = &g.ibuf[q * 6]; uint16_t b = (uint16_t)(q * 4);
        ix[0] = b; ix[1] = b + 1; ix[2] = b + 2; ix[3] = b; ix[4] = b + 2; ix[5] = b + 3;
    }

    for (uint32_t f = 0; f < UI_FRAMES; ++f)
        g.frame[f].verts = &g.vbuf[f * UI_MAX_VERTS];

    g.ready = 1;
    UI_LOG("ready: %u UI vertex programs, %u KB vertex ring, %u-quad index buffer\n",
           g.nprog, (UI_FRAMES * UI_MAX_VERTS * (unsigned)sizeof(ui_vtx)) >> 10, UI_MAX_QUADS);
    return 0;
}

void xv_ui_gxm_shutdown(void)
{
    xv_texture_worker_shutdown();
    xv_geometry_worker_shutdown();
    if (!g.ready) return;
    if (g.vbuf) { sceGxmUnmapMemory(g.vbuf); sceKernelFreeMemBlock(g.vbuf_uid); }
    if (g.ibuf) { sceGxmUnmapMemory(g.ibuf); sceKernelFreeMemBlock(g.ibuf_uid); }
    if (g.clrbuf) { sceGxmUnmapMemory(g.clrbuf); sceKernelFreeMemBlock(g.clr_uid); }
    if (g.settingsbuf) { sceGxmUnmapMemory(g.settingsbuf); sceKernelFreeMemBlock(g.settings_uid); }
    if (g.dec_base) { sceGxmUnmapMemory(g.dec_base); sceKernelFreeMemBlock(g.dec_uid); }
    for (uint32_t i = 0; i < g.nprog; ++i) {
        for (int st = 0; st < (int)UI_MAX_STAGES; ++st) if (g.prog[i].fs_ok[st]) xv_fshader_unload(&g.prog[i].fs[st]);
        xv_vshader_unload(&g.prog[i].vs);
    }
    xv_fshader_unload(&g.settings_fs);
    xv_fshader_unload(&g.clear_fs); xv_vshader_unload(&g.clear_vs);
    g.ready = 0;
}

int xv_ui_gxm_ready(void) { return g.ready; }

#ifdef XV_DEPTH_STORE
int xv_ui_gxm_depth_tail_readonly(unsigned frame)
{
    /* replay_overlay and replay_settings both disable stencil and depth writes.
     * Verify their actual linked fragment programs cannot export depth either.
     * No UI batches, clears or callbacks are covered by this contract. */
    return g.ready && frame < UI_FRAMES &&
        g.clear_fs.fprog && !g.clear_fs.replaces_depth &&
        g.settings_fs.fprog && !g.settings_fs.replaces_depth;
}
#endif

/* ---- recording (game fiber) ------------------------------------------------------------------- */
static inline uint32_t pack_argb(const float c[4])
{
    /* D3DCOLOR byte order B,G,R,A in memory; the UI VS reads it .zyxw -> RGBA */
    int32_t r = (int32_t)(c[0] * 255.0f + 0.5f), gg = (int32_t)(c[1] * 255.0f + 0.5f);
    int32_t b = (int32_t)(c[2] * 255.0f + 0.5f), a = (int32_t)(c[3] * 255.0f + 0.5f);
    r = r < 0 ? 0 : r > 255 ? 255 : r; gg = gg < 0 ? 0 : gg > 255 ? 255 : gg;
    b = b < 0 ? 0 : b > 255 ? 255 : b; a = a < 0 ? 0 : a > 255 ? 255 : a;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
}

void xv_ui_gxm_clear(uint32_t argb)
{
    if (!g.ready) return;
    ui_frame *fr = &g.frame[g.rec];
    fr->clear_argb = argb; fr->has_clear = 1;
}

void xv_ui_gxm_quads(const xd3d_im_vtx *v, unsigned n, uint32_t tex_hdr, const float tint[4], unsigned prog, unsigned stage)
{
    if (!g.ready || n < 4 || prog >= g.nprog) return;
    const ui_prog *p = &g.prog[prog];
    if (stage >= UI_MAX_STAGES || !p->fs_ok[stage]) stage = 0;
    { extern int xd3d_hist_active(void) __attribute__((weak));
      if (xd3d_hist_active && xd3d_hist_active()) {
          uint32_t fw = tex_hdr ? guest_u32(tex_hdr + 12) : 0, sz = tex_hdr ? guest_u32(tex_hdr + 16) : 0;
          unsigned w = sz ? (sz & 0xFFF) + 1 : 1u << ((fw >> 20) & 0xF), h = sz ? ((sz >> 12) & 0xFFF) + 1 : 1u << ((fw >> 24) & 0xF);
          if (tex_hdr && p->desc->func_hash == 0x4469E1F8u) { static unsigned once2; if (once2++ < 3) UI_LOG("[hist] stages %08X %08X %08X %08X fmtwords %08X %08X %08X %08X\n",
              xd3d_state.texture[0], xd3d_state.texture[1], xd3d_state.texture[2], xd3d_state.texture[3],
              xd3d_state.texture[0] ? guest_u32(xd3d_state.texture[0] + 12) : 0, xd3d_state.texture[1] ? guest_u32(xd3d_state.texture[1] + 12) : 0,
              xd3d_state.texture[2] ? guest_u32(xd3d_state.texture[2] + 12) : 0, xd3d_state.texture[3] ? guest_u32(xd3d_state.texture[3] + 12) : 0); }
          if (tex_hdr && p->desc->func_hash == 0x4469E1F8u) { static unsigned once; if (once++ < 3) UI_LOG("[hist] tex hdr %08X words %08X %08X %08X %08X %08X | c[-64] %.5f %.5f %.5f %.5f | data[0..15] %08X %08X %08X %08X\n", tex_hdr,
              guest_u32(tex_hdr), guest_u32(tex_hdr + 4), guest_u32(tex_hdr + 8), guest_u32(tex_hdr + 12), guest_u32(tex_hdr + 16),
              xd3d_state.vsc[32][0], xd3d_state.vsc[32][1], xd3d_state.vsc[32][2], xd3d_state.vsc[32][3],
              guest_u32(0x80000000u | guest_u32(tex_hdr + 4)), guest_u32((0x80000000u | guest_u32(tex_hdr + 4)) + 4), guest_u32((0x80000000u | guest_u32(tex_hdr + 4)) + 8), guest_u32((0x80000000u | guest_u32(tex_hdr + 4)) + 12)); }
          UI_LOG("[hist] ui quads %u prog %s stage %u ps %08X tex %08X fmt %02X %ux%u | v0 pos %.1f %.1f uv %.3f %.3f col %.2f %.2f %.2f %.2f | v2 pos %.1f %.1f uv %.3f %.3f\n",
                 n / 4, p->desc->gxp, stage, xd3d_state.ps_hash, tex_hdr, (fw >> 8) & 0xFF, w, h,
                 v[0].a[0][0], v[0].a[0][1], v[0].a[4][0], v[0].a[4][1], v[0].a[9][0], v[0].a[9][1], v[0].a[9][2], v[0].a[9][3],
                 v[2].a[0][0], v[2].a[0][1], v[2].a[4][0], v[2].a[4][1]); } }
    ui_frame *fr = &g.frame[g.rec];
    unsigned quads = n / 4u;
    if (fr->vcount + quads * 4u > UI_MAX_VERTS || fr->bcount >= UI_MAX_BATCHES || quads > UI_MAX_QUADS) {
        fr->overflow = 1; return;
    }

    if (!xv_d3d_record_ui(g.rec, fr->bcount)) return;
    ui_batch *b = &fr->batches[fr->bcount++];
    b->first_vertex = fr->vcount;
    b->nquads = quads;
    b->prog = (uint8_t)prog; b->stage = (uint8_t)stage;
    const SceGxmTexture *t = ui_texture_for(tex_hdr, 1);
    b->has_tex = t != NULL;
    if (t) { b->tex = *t; xv_ui_gxm_apply_texture_options(&b->tex); }
    if (tint) { b->tint[0] = tint[0]; b->tint[1] = tint[1]; b->tint[2] = tint[2]; b->tint[3] = tint[3]; }
    else       b->tint[0] = b->tint[1] = b->tint[2] = b->tint[3] = 1.0f;

    /* snapshot the c[] window this program uses (vsc index = d3d_reg + 96) */
    { int cnt = p->desc->c_count > XV_MAX_ATTRS ? XV_MAX_ATTRS : p->desc->c_count;
      const float *vsc = &xd3d_state.vsc[p->desc->c_base + 96][0];
      memcpy(b->cwin, vsc, (size_t)cnt * 4 * sizeof(float)); }

    /* zero-copy pack: stream straight into GPU memory, no scratch copy */
    ui_vtx *dst = &fr->verts[fr->vcount];
    for (unsigned i = 0; i < quads * 4u; ++i) {
        dst[i].x = v[i].a[0][0]; dst[i].y = v[i].a[0][1];   /* v0 position */
        dst[i].u = v[i].a[4][0]; dst[i].v = v[i].a[4][1];   /* v4 (uv input to the VS) */
        dst[i].color = pack_argb(v[i].a[9]);                /* v9 diffuse */
    }
    fr->vcount += quads * 4u;
}

void xv_ui_gxm_frame_flip(void)
{
    if (!g.ready) return;
    g.rec_frame++;
    ui_frame *fr = &g.frame[g.rec];
    if (fr->overflow)
        UI_LOG("frame overflow (%u verts, %u batches) - some UI dropped\n", fr->vcount, fr->bcount);
    { extern void xv_settings_snapshot(xv_dash_graphics_view *) __attribute__((weak));
      if(xv_settings_snapshot)xv_settings_snapshot(&fr->settings); }
    /* flush the vertex/clear data we just wrote so the GPU sees it */
    xv_gpu_flush_ui(fr->verts, fr->vcount * sizeof(ui_vtx));
    __atomic_store_n(&g.pub, (int32_t)g.rec, __ATOMIC_RELEASE);    /* hand this frame to the pump */
    g.rec = (g.rec + 1u) % UI_FRAMES;                             /* start recording the next */
}

void xv_ui_gxm_frame_begin(void)
{
    /* xv_present acquired this retired slot after publishing the last frame. */
    ui_frame *nx = &g.frame[g.rec];
    nx->vcount = 0; nx->bcount = 0; nx->has_clear = 0; nx->overflow = 0;
}

/* ---- replay (render pump, inside sceGxmBeginScene/EndScene) ------------------------------------- */

/* ---- debug overlay: fps + frame-time split as 7-segment digits (no font needed) -------------------
 * Toggle: hold SELECT+START ~1 s (xk_os_vita pad poll) or XV_FPS=1 in xita.cfg. */
int g_xv_overlay_on = -1;                                   /* -1 = read config once */
float g_xv_ovl_game_ms, g_xv_ovl_render_ms, g_xv_ovl_fps;
static const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };   /* abcdefg */
static unsigned panel_rect(clr_vtx *v, unsigned n, unsigned limit, float x, float y, float w, float h, uint32_t col)
{
    if (n + 4 > limit * 4) return n;
    /* pixel space (960x544) -> clip; y down */
    float x0 = x / 480.0f - 1.0f, x1 = (x + w) / 480.0f - 1.0f, y0 = 1.0f - y / 272.0f, y1 = 1.0f - (y + h) / 272.0f;
    v[n++] = (clr_vtx){ x0, y0, 0.5f, col }; v[n++] = (clr_vtx){ x1, y0, 0.5f, col };
    v[n++] = (clr_vtx){ x1, y1, 0.5f, col }; v[n++] = (clr_vtx){ x0, y1, 0.5f, col };
    return n;
}
static unsigned ovl_rect(clr_vtx *v, unsigned n, float x, float y, float w, float h, uint32_t col)
{ return panel_rect(v,n,OVL_MAX_QUADS,x,y,w,h,col); }
static unsigned panel_text_limit(clr_vtx *v,unsigned n,unsigned limit,const char *text,float x,float y,float scale,uint32_t color)
{
    for(;*text;text++,x+=6*scale) {
        unsigned ch=(unsigned char)*text;if(ch<32 || ch>127)ch='?';
        for(unsigned row=0;row<8;row++)for(unsigned col=0;col<5;) {
            if(!(font[ch-32][row]&(1u<<(6-col)))) {col++;continue;}
            unsigned start=col++;
            while(col<5 && (font[ch-32][row]&(1u<<(6-col))))col++;
            n=panel_rect(v,n,limit,x+start*scale,y+row*scale,(col-start)*scale,scale,color);
        }
    }
    return n;
}
static unsigned panel_text(clr_vtx *v,unsigned n,const char *text,float x,float y,float scale,uint32_t color)
{ return panel_text_limit(v,n,UI_MAX_QUADS,text,x,y,scale,color); }
void xv_ui_gxm_replay_settings(SceGxmContext *ctx,unsigned frame)
{
    if(!g.ready || frame>=UI_FRAMES || !g.frame[frame].settings.active)return;
    const xv_dash_graphics_view *s=&g.frame[frame].settings;
    clr_vtx *v=&g.settingsbuf[frame*UI_MAX_QUADS*4];unsigned n=0;
    const uint32_t green=0xff8cee60u,white=0xffd0ffc0u,dim=0xff90b590u;
    n=panel_rect(v,n,UI_MAX_QUADS,116,62,728,426,0xef081409u);
    n=panel_text(v,n,"XITA / IN-GAME GRAPHICS",140,84,2,green);
    char range[32];
    int end=s->first+5;if(end>s->count)end=s->count;
    snprintf(range,sizeof range,"%d-%d / %d",s->first+1,end,s->count);
    n=panel_text(v,n,range,810-strlen(range)*6,88,1,dim);
    n=panel_text(v,n,"Game continues unless paused in Halo.",140,115,1,dim);
    for(int i=0;i<5 && s->first+i<s->count;i++) {
        float y=153+i*44;int selected=s->first+i==s->selected;
        if(selected)n=panel_rect(v,n,UI_MAX_QUADS,132,y-8,696,34,0xff244028u);
        n=panel_text(v,n,s->names[i],145,y,2,selected?white:dim);
        n=panel_text(v,n,s->values[i],810-strlen(s->values[i])*12,y,2,green);
    }
    n=panel_text(v,n,s->live?"APPLIES DURING PLAY":"RELAUNCH XITA TO APPLY",145,380,1,green);
    n=panel_text(v,n,s->help,145,402,1,dim);
    n=panel_text(v,n,s->status,145,423,1,white);
    n=panel_text(v,n,"UP / DOWN  SELECT    LEFT / RIGHT  CHANGE    CIRCLE  CLOSE",145,461,1,green);
    xv_gpu_flush_pump(v,n*sizeof(*v));
    sceGxmSetViewport(ctx,480,480,272,-272,0.5f,0.5f);
    sceGxmSetCullMode(ctx,SCE_GXM_CULL_NONE);xv_stencil_bind(ctx,NULL);
    sceGxmSetFrontDepthFunc(ctx,SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx,SCE_GXM_DEPTH_WRITE_DISABLED);
    xv_shader_bind(ctx,&g.clear_vs,&g.settings_fs);
    const void *streams[]={v};xv_vshader_set_streams(ctx,&g.clear_vs,streams);
    XV_RENDER_CALL(XV_RENDER_DRAW,sceGxmDraw(ctx,SCE_GXM_PRIMITIVE_TRIANGLES,SCE_GXM_INDEX_FORMAT_U16,g.ibuf,n/4*6));
}
static unsigned ovl_digit(clr_vtx *v, unsigned n, int d, float x, float y, float s, uint32_t col)
{
    if (d < 0 || d > 9) return n;
    uint8_t m = SEG7[d]; float t = s * 0.18f, w = s, h = s * 1.8f;
    if (m & 0x01) n = ovl_rect(v, n, x, y, w, t, col);                          /* a top */
    if (m & 0x02) n = ovl_rect(v, n, x + w - t, y, t, h / 2, col);              /* b top-right */
    if (m & 0x04) n = ovl_rect(v, n, x + w - t, y + h / 2, t, h / 2, col);      /* c bottom-right */
    if (m & 0x08) n = ovl_rect(v, n, x, y + h - t, w, t, col);                  /* d bottom */
    if (m & 0x10) n = ovl_rect(v, n, x, y + h / 2, t, h / 2, col);              /* e bottom-left */
    if (m & 0x20) n = ovl_rect(v, n, x, y, t, h / 2, col);                      /* f top-left */
    if (m & 0x40) n = ovl_rect(v, n, x, y + h / 2 - t / 2, w, t, col);          /* g middle */
    return n;
}
/* draw an integer right-aligned ending at x_end */
static unsigned ovl_number(clr_vtx *v, unsigned n, unsigned val, float x_end, float y, float s, uint32_t col, int min_digits)
{
    float adv = s * 1.35f; int digits = 0; unsigned t = val;
    do { digits++; t /= 10; } while (t);
    if (digits < min_digits) digits = min_digits;
    float x = x_end - digits * adv;
    for (int i = digits - 1; i >= 0; --i) { n = ovl_digit(v, n, (int)(val % 10), x + i * adv, y, s, col); val /= 10; }
    return n;
}

/* C0/C1/C2, a bar and an explicit percentage. Dashes distinguish unavailable
 * kernel counters from an idle core. This stays inside the existing quad budget. */
static unsigned ovl_cpu_row(clr_vtx *v, unsigned n, unsigned core, unsigned usage, float y)
{
    const uint32_t white = 0xFFE0E0E0u;
    uint32_t col = usage > 90 && usage <= 100 ? 0xFFFF8040u : 0xFF70D8FFu;
    float x = 778, s = 7, t = 1.4f;
    /* C */
    n = ovl_rect(v, n, x, y, s, t, white);
    n = ovl_rect(v, n, x, y, t, 12.6f, white);
    n = ovl_rect(v, n, x, y + 11.2f, s, t, white);
    n = ovl_digit(v, n, (int)core, x + 10, y, s, white);
    n = ovl_rect(v, n, 809, y + 3, 74, 7, 0xFF303030u);
    if (usage <= 100) {
        if (usage) n = ovl_rect(v, n, 809, y + 3, 74.0f * usage / 100.0f, 7, col);
        n = ovl_number(v, n, usage, 938, y, s, col, 1);
        /* %: two dots and a descending diagonal. */
        n = ovl_rect(v, n, 940, y + 1, 2, 2, white);
        n = ovl_rect(v, n, 947, y + 10, 2, 2, white);
        for (unsigned i = 0; i < 5; ++i) n = ovl_rect(v, n, 946 - i * 1.25f, y + 2 + i * 2, 1.5f, 2, white);
    } else {
        n = ovl_rect(v, n, 917, y + 6, 7, t, white);
        n = ovl_rect(v, n, 928, y + 6, 7, t, white);
    }
    return n;
}

static void xv_ui_gxm_overlay(SceGxmContext *ctx, int32_t idx)
{
    extern uint32_t xv_benchmark_status(void) __attribute__((weak));
    uint32_t test=xv_benchmark_status?xv_benchmark_status():0;
    if (g_xv_overlay_on < 0) { const char *e = getenv("XV_FPS"); g_xv_overlay_on = e ? atoi(e) != 0 : 0; }
    if (!g_xv_overlay_on && !test) return;
    clr_vtx *v = &g.clrbuf[idx * (4 + OVL_MAX_QUADS * 4) + 4]; unsigned n = 0;
    const uint32_t bg = 0xA0000000u, fps_col = 0xFF40FF40u, game_col = 0xFFFFD040u, rend_col = 0xFF40C0FFu;
    /* panel top-right: fps | game ms | render ms  (colours: green / amber / blue) */
    n = ovl_rect(v, n, 960 - 190, 6, 184, 108, bg);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_fps + 0.5f), 960 - 136, 11, 10, fps_col, 1);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_game_ms + 0.5f), 960 - 74, 11, 10, game_col, 1);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_render_ms + 0.5f), 960 - 12, 11, 10, rend_col, 1);
    uint32_t cpu = xv_cpu_usage();
    for (unsigned i = 0; i < 3; ++i) n = ovl_cpu_row(v, n, i, (cpu >> (8 * i)) & 255u, 42 + 19 * i);
    n=panel_text_limit(v,n,OVL_MAX_QUADS,XV_BUILD_LABEL,778,101,1,0xFFE0E0E0u);
    if(test) {
        n=ovl_rect(v,n,770,119,184,35,bg);
        /* Tiny 3x5 TEST label, followed by resolution and phase progress. */
        static const unsigned glyphs[]={072222u,074747u,074717u,072222u};
        static const unsigned material_glyphs[]={074557u,075744u,055557u,0};
        const unsigned *label=(test&(1u<<19))?material_glyphs:glyphs;
        for(unsigned c=0;c<4;c++)for(unsigned y=0;y<5;y++)for(unsigned x=0;x<3;x++)
            if(label[c]&(1u<<((4-y)*3+2-x)))n=ovl_rect(v,n,779+c*9+x*2,125+y*2,2,2,0xFFFFFFFFu);
        n=ovl_number(v,n,test&1023u,882,122,8,game_col,3);
        n=ovl_number(v,n,(test>>17)&3u,941,122,8,fps_col,1);
        n=ovl_rect(v,n,779,144,164,4,0xFF303030u);
        n=ovl_rect(v,n,779,144,164*((test>>10)&127u)/100.0f,4,fps_col);
    }
    if (!n) return;
    xv_gpu_flush_pump(v, n * sizeof(clr_vtx));
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    xv_shader_bind(ctx, &g.clear_vs, &g.clear_fs);
    const void *streams[1] = { v };
    xv_vshader_set_streams(ctx, &g.clear_vs, streams);
    XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, (n / 4) * 6));
}

unsigned xv_ui_gxm_record_frame(void) { return g.rec; }
void xv_ui_gxm_replay_overlay(SceGxmContext *ctx, unsigned frame)
{
    if (!g.ready || frame >= UI_FRAMES) return;
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    xv_stencil_bind(ctx, NULL);
    xv_ui_gxm_overlay(ctx, frame);
}
/* Ordered RTT replay retains the scene's viewport and pins the UI ring index. */
void xv_ui_gxm_replay_batch(SceGxmContext *ctx, unsigned frame, unsigned batch, const void *target)
{
    if (!g.ready || frame >= UI_FRAMES || batch >= g.frame[frame].bcount) return;
    ui_frame *fr = &g.frame[frame];
    ui_batch *b = &fr->batches[batch];
    if (target && b->has_tex && sceGxmTextureGetData(&b->tex) == target) {
        static int warned;
        if (!warned++) UI_LOG("RT feedback UI batch skipped\n");
        return;
    }
    xv_stencil_bind(ctx, NULL);
    ui_prog *p = &g.prog[b->prog];
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);
    xv_shader_bind(ctx, &p->vs, &p->fs[b->stage]);
    const void *streams[1] = { &fr->verts[b->first_vertex] };
    xv_vshader_set_streams(ctx, &p->vs, streams);
    void *ub = NULL;
    if (xv_vshader_begin_constants(ctx, &p->vs, &ub) == 0) {
        int cnt = p->desc->c_count > XV_MAX_ATTRS ? XV_MAX_ATTRS : p->desc->c_count;
        xv_vshader_set_constants(ub, &p->vs, p->desc->c_base, cnt, b->cwin);
    }
    if (b->has_tex) sceGxmSetFragmentTexture(ctx, 0, &b->tex);
    XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, b->nquads * 6));
}

unsigned xv_ui_gxm_published_frame(void)
{ return (unsigned)__atomic_load_n(&g.pub, __ATOMIC_ACQUIRE); }
void xv_ui_gxm_replay(SceGxmContext *ctx, unsigned width, unsigned height)
{ xv_ui_gxm_replay_frame(ctx, width, height, xv_ui_gxm_published_frame()); }
void xv_ui_gxm_replay_frame(SceGxmContext *ctx, unsigned width, unsigned height, unsigned idx)
{
    if (!g.ready || idx >= UI_FRAMES) return;
    ui_frame *fr = &g.frame[idx];

    /* Halo's own VS emits D3D clip space (+y up); GXM's default viewport (yScale = -height/2) already
     * puts +y at the top of the 960x544 target, so keep it explicit here (xv_d3d may have changed it). */
    sceGxmSetViewport(ctx, width * 0.5f, width * 0.5f, height * 0.5f, -(float)height * 0.5f, 0.5f, 0.5f);
    xv_stencil_bind(ctx, NULL);
    /* 2D: depth off, cull off */
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetCullMode(ctx, SCE_GXM_CULL_NONE);

    /* clear: opaque full-screen quad in clip space, colour = clear_argb */
    if (fr->has_clear) {
        clr_vtx *cv = &g.clrbuf[idx * (4 + OVL_MAX_QUADS * 4)];
        uint32_t col = fr->clear_argb;
        const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int i = 0; i < 4; ++i) { cv[i].x = corners[i][0]; cv[i].y = corners[i][1]; cv[i].z = 0.5f; cv[i].color = col; }
        xv_gpu_flush_pump(cv, 4 * sizeof(clr_vtx));
        xv_shader_bind(ctx, &g.clear_vs, &g.clear_fs);
        const void *streams[1] = { cv };
        xv_vshader_set_streams(ctx, &g.clear_vs, streams);
        XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, 6));
    }

    /* UI batches */
    const void *last_tex_ptr = (const void *)-1;
    for (uint32_t bi = 0; bi < fr->bcount; ++bi) {
        ui_batch *b = &fr->batches[bi];
        ui_prog *p = &g.prog[b->prog];
        xv_shader_bind(ctx, &p->vs, &p->fs[b->stage]);

        const void *streams[1] = { &fr->verts[b->first_vertex] };
        xv_vshader_set_streams(ctx, &p->vs, streams);

        void *ub = NULL;
        if (xv_vshader_begin_constants(ctx, &p->vs, &ub) == 0) {
            int cnt = p->desc->c_count > XV_MAX_ATTRS ? XV_MAX_ATTRS : p->desc->c_count;
            xv_vshader_set_constants(ub, &p->vs, p->desc->c_base, cnt, b->cwin);
        }

        if (b->has_tex) {                                /* only re-bind when the texture actually changes */
            const void *tdata = sceGxmTextureGetData(&b->tex);
            if (tdata != last_tex_ptr) { sceGxmSetFragmentTexture(ctx, 0, &b->tex); last_tex_ptr = tdata; }
        }

        XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, b->nquads * 6));
    }
    xv_ui_gxm_overlay(ctx, idx);
}

/* ================================================================================================
 *  xd3d_r_* hook overrides.  The recompiled engine (recomp/kernel/xd3d.c) declares these weak; on
 *  the Vita this file provides the strong definitions, so the same game code drives GXM.
 * ============================================================================================== */
#include "xv_d3d.h"
#include "xv_passthrough.h"
#include "xv_hud.h"
static int g_mesh_path = 1;                              /* world geometry through xv_d3d (set 0 to fall back to UI-only clears) */
/* Color target state for the legacy immediate UI path. */
static int g_offscreen;
void xd3d_r_state(const char *what, uint32_t a, uint32_t b, uint32_t v)
{
    (void)b;
    if (!strcmp(what, "ReleaseRenderTarget")) { xv_d3d_ReleaseRenderTarget(a); return; }
    if (!strcmp(what, "SetRenderTarget")) {
        if (!a) return;                                                 /* NULL: keep the current colour target */
        g_offscreen = (v && a != v);
        xv_d3d_SetRenderTarget(a, !g_offscreen);                        /* opens / closes a render-to-texture pass */
        static unsigned n; if (n++ < 8) UI_LOG("SetRenderTarget %08X (backbuffer %08X) -> %s\n", a, v, g_offscreen ? "offscreen pass" : "backbuffer");
    }
}
void xd3d_r_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil)
{
    if (g_mesh_path) { xv_d3d_Clear(flags, color, z, stencil); return; }   /* colour + depth, rendered before the world */
    (void)z; (void)stencil;
    if (flags & 1) xv_ui_gxm_clear(color);
}

/* World geometry: DrawVertices / DrawIndexedVertices from the recompiled renderer.  The kernel's D3D
 * object model holds the bound state; push it into the Stage-3 GXM bridge (xv_d3d.c) which records the
 * draw with pointers straight into guest memory (the arena is GXM-mapped) and replays it before the UI. */
static void sync_draw_state(void)
{
    uint64_t profile = xv_draw_profile_begin();
    uint32_t texture_state[4][5];
    xd3d_ps_sync();
    xd3d_texture_states(texture_state);
    xv_d3d_SyncDrawState(&xd3d_state,xd3d_current_attributes(),texture_state);
    xv_draw_profile_step(XV_DRAW_STATE, &profile);
}

void xd3d_r_draw(xctx *c, int indexed, uint32_t prim, uint32_t count, uint32_t data)
{
    { extern void xd3d_hist_tex_check(void); xd3d_hist_tex_check(); }
    (void)c;
    if (!g_mesh_path) return;
    uint32_t vs = xd3d_state.vs_program;
    if (!(vs & 1)) return;                                             /* FVF draws: not yet */
    uint32_t fnv = guest_u32((vs & ~1u) + 12);
    uint64_t profile = xv_draw_profile_begin();
    uint32_t h = xv_d3d_handle_for_hash(fnv);
    if (!h) { static unsigned n; if (n++ < 8) UI_LOG("draw: no program for VS fnv %08X\n", fnv); return; }
    xv_d3d_SetVertexShader(h);
    xv_d3d_SetTrackedConstants(xd3d_state.vsc, &xd3d_state.vsc_dirty_lo, &xd3d_state.vsc_dirty_hi);
    xv_draw_profile_step(XV_DRAW_SETUP, &profile);
    sync_draw_state();
    if (indexed) {
        uint32_t ib = xd3d_state.indices;                              /* X_D3DIndexBuffer header; Data = guest address of the WORDs */
        uint32_t idata = data ? data : (ib ? guest_u32(ib + 4) : 0);   /* TEST: bare IB Data again */
        { extern int xd3d_hist_active(void); if (xd3d_hist_active()) {
            static unsigned once; UI_LOG("[hist] ib obj %08X data %08X pidx %08X count %u\n", ib, ib ? guest_u32(ib + 4) : 0, data, count);
            if (!once++) { uint32_t tbl = guest_u32(0x803A6000u + 0x1C), cnt = guest_u32(0x803A6000u + 0x18);
                for (unsigned i = 0; i < 6 && i < cnt; ++i) UI_LOG("[hist] tri obj[%u] %08X %08X %08X\n", i, guest_u32(tbl + i * 12), guest_u32(tbl + i * 12 + 4), guest_u32(tbl + i * 12 + 8)); }
        } }
        if (!idata) return;
        xv_d3d_DrawIndexedVerticesBase(prim, count, idata, xd3d_state.index_base);
    } else {
        xv_d3d_DrawVertices(prim, data, count);                        /* data = start vertex */
    }
}

/* Which texture stage carries the image for the bound combiner program: the highest tN read by any
 * active combiner stage (D3DPIXELSHADERDEF: AlphaInputs @0, RGBInputs @0x88, CombinerCount @0xD4;
 * input byte low nibble = register, 8..B = t0..t3). */
static unsigned ps_image_stage(uint32_t psdef)
{
    if (!psdef) return 0;
    /* the EFFECTIVE combiner state: Halo patches stage registers after SetPixelShaderProgram, so the def in
     * guest memory is stale for the dialog/pause-menu text (picked a solid stage -> glyphs drew as blocks) */
    xd3d_ps_sync();
    const uint8_t *p = (const uint8_t *)xd3d_state.ps_shadow;
    unsigned count = p[0xD4] & 0xF; if (count > 8) count = 8;
    /* Backward liveness over the combiner stages: which registers reach the output.  Registers: 0 zero, 1 c0,
     * 2 c1, 3 fog, 4 v0, 5 v1, 8..B t0..t3, C r0, D r1, E v1r0_sum, F ef_prod.  The text program computes
     * t1/t2 too but only t0*v0 lands in r0 - the old "highest stage read anywhere" rule picked t2. */
    unsigned live = 0;
    uint32_t fin_abcd = *(const uint32_t *)(p + 0x20), fin_efg = *(const uint32_t *)(p + 0x24);
    if (fin_abcd || fin_efg) { for (int k = 0; k < 4; ++k) { live |= 1u << ((fin_abcd >> (8 * k)) & 0xF); live |= 1u << ((fin_efg >> (8 * k)) & 0xF); } }
    else live = 1u << 0xC;                                                       /* default final: r0 */
    if (live & (1u << 0xE)) live |= (1u << 0xC) | (1u << 5);                    /* v1r0_sum */
    for (int st = (int)count - 1; st >= 0; --st) {
        /* ICW: A = bits 24-31, B = 16-23, C = 8-15, D = 0-7 (register in the low nibble of each byte);
         * OCW: CD dst = bits 0-3, AB dst = 4-7, SUM dst = 8-11.  AB and CD are separate products. */
        uint32_t rgb_in = *(const uint32_t *)(p + 0x88 + st * 4), a_in = *(const uint32_t *)(p + 0x00 + st * 4);
        uint32_t rgb_out = *(const uint32_t *)(p + 0xB4 + st * 4), a_out = *(const uint32_t *)(p + 0x68 + st * 4);
        #define LIVE_IN(word, shift) (live |= 1u << (((word) >> (shift)) & 0xF))
        #define DST(word, shift) ((((word) >> (shift)) & 0xF) ? (1u << (((word) >> (shift)) & 0xF)) : 0u)
        unsigned rgb_ab = DST(rgb_out, 4), rgb_cd = DST(rgb_out, 0), rgb_sum = DST(rgb_out, 8);
        unsigned a_ab = DST(a_out, 4), a_cd = DST(a_out, 0), a_sum = DST(a_out, 8);
        if ((rgb_ab | rgb_sum) & live) { LIVE_IN(rgb_in, 24); LIVE_IN(rgb_in, 16); }
        if ((rgb_cd | rgb_sum) & live) { LIVE_IN(rgb_in, 8);  LIVE_IN(rgb_in, 0); }
        if ((a_ab | a_sum) & live)     { LIVE_IN(a_in, 24);   LIVE_IN(a_in, 16); }
        if ((a_cd | a_sum) & live)     { LIVE_IN(a_in, 8);    LIVE_IN(a_in, 0); }
        #undef LIVE_IN
        #undef DST
    }
    unsigned best = 0, any = 0;
    for (unsigned t = 0; t < 4; ++t) if (live & (1u << (8 + t))) { best = t; any = 1; }
    if (any) return best;
    /* nothing texture-fed reaches the output: fall back to the highest stage read anywhere */
    for (unsigned st = 0; st < count; ++st)
        for (int k = 0; k < 4; ++k) {
            unsigned ra = p[st * 4 + k] & 0xF, rr = p[0x88 + st * 4 + k] & 0xF;
            if (ra >= 8 && ra <= 0xB && ra - 8 > best) best = ra - 8;
            if (rr >= 8 && rr <= 0xB && rr - 8 > best) best = rr - 8;
        }
    return best;
}

/* Immediate lens-flare vertices use the particle declaration, plus v10 set
 * directly by Halo. Keep that attribute per vertex: the streamed declaration
 * normally supplies it from one persistent value for the whole draw. */
static unsigned g_flare_seen, g_flare_culled;
static void draw_immediate_flare(const xd3d_im_vtx *v, unsigned n)
{
    typedef struct {
        float x, y, z, u, v;
        uint32_t color;
        float secondary[4];
    } flare_vertex;
    _Static_assert(sizeof(flare_vertex) == 40, "immediate flare vertex layout");
    static uint32_t handle;
    static xv_vs_desc_t desc;
    static xv_attr_desc_t attrs[4];
    if (!g_mesh_path) return;
    static int cull=-1;
    if (cull<0) {
        const char *e=getenv("XV_FLARE_CULL"), *wc=getenv("XV_WCLAMP");
        cull=(!e || atoi(e)) && (!wc || !atoi(wc));
    }
    g_flare_seen+=n/4;
    if (cull && n==4 && xv_flare_quad_outside(v,sizeof *v,&xd3d_state.vsc[28])) {
        g_flare_culled++; return;
    }
    if (!handle) {
        /* Keep normal hash lookup bound to the streamed layout. */
        if (!xv_d3d_handle_for_hash(xv_vs_halo_vs_56.func_hash)) return;
        desc = xv_vs_halo_vs_56;
        memcpy(attrs, desc.attrs, sizeof attrs);
        attrs[3].stream = 0;
        attrs[3].offset = 24;
        desc.attrs = attrs;
        desc.stride[0] = sizeof(flare_vertex);
        handle = xv_d3d_RegisterVertexShader(&desc);
        if (!handle) return;
    }
    xv_d3d_SetVertexShader(handle);
    xv_d3d_SetTrackedConstants(xd3d_state.vsc, &xd3d_state.vsc_dirty_lo, &xd3d_state.vsc_dirty_hi);
    sync_draw_state();
    for (unsigned first = 0; first + 4 <= n;) {
        flare_vertex packed[64];
        unsigned count=0;
        while (first+4<=n && count+4<=64) {
            if (cull && n!=4 && xv_flare_quad_outside(v+first,sizeof *v,&xd3d_state.vsc[28])) {
                g_flare_culled++; first+=4; continue;
            }
            for (unsigned i=0;i<4;i++) {
                const xd3d_im_vtx *src=&v[first+i];
                packed[count+i]=(flare_vertex){src->a[0][0],src->a[0][1],src->a[0][2],
                    src->a[4][0],src->a[4][1],pack_argb(src->a[9]),{0}};
                memcpy(packed[count+i].secondary,src->a[10],sizeof packed[count+i].secondary);
            }
            first+=4; count+=4;
        }
        if (count) xv_d3d_DrawImmediateStrided(X_D3DPT_QUADLIST,packed,count,sizeof(flare_vertex));
    }
}

void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n)
{
    if (xd3d_im_passthrough()) {
        if (!g_mesh_path || prim != 7 || n < 4) return;
        static uint32_t handle;
        if (!handle) handle = xv_d3d_RegisterVertexShader(&xv_vs_passthrough);
        if (!handle) return;
        xv_d3d_SetVertexShader(handle);
        sync_draw_state();
        float scale[4][4];
        for (unsigned t = 0; t < 4; ++t) {
            scale[t][0] = scale[t][1] = scale[t][2] = scale[t][3] = 1.0f;
            uint32_t hdr = xd3d_state.texture[t];
            uint32_t size = hdr ? guest_u32(hdr + 16) : 0;
            if (size) {
                scale[t][0] = 1.0f / ((size & 0xFFF) + 1);
                scale[t][1] = 1.0f / (((size >> 12) & 0xFFF) + 1);
            }
        }
        xv_d3d_SetVertexShaderConstant(0, &scale[0][0], 4);
        if (xd3d_hist_active()) {
            UI_LOG("[hist] passthrough rgba %.3f %.3f %.3f %.3f uv %.3f %.3f / %.3f %.3f\n",
                   v[0].a[3][0], v[0].a[3][1], v[0].a[3][2], v[0].a[3][3],
                   v[0].a[9][0], v[0].a[9][1], v[0].a[10][0], v[0].a[10][1]);
        }
        xv_d3d_DrawImmediate(X_D3DPT_QUADLIST, v, n);
        return;
    }
    if (prim != 7 || n < 4) return;       /* QUADLIST only (the UI path) */
    uint32_t vs = xd3d_state.vs_handle;
    uint32_t decl = (vs & 1) ? guest_u32(vs & ~1u) : 0;
    uint32_t program = xd3d_state.vs_program;
    uint32_t fnv = (program & 1) ? guest_u32((program & ~1u) + 12) : 0;
    if (decl == 0x001E13BCu && fnv == 0x405809D3u) {
        draw_immediate_flare(v, n);
        return;
    }
    if (decl != UI_DECL_VA) return;
    xd3d_ps_sync();
    /* Halo's HUD uses premultiplied/additive blending and a combiner threshold
     * for meter fills. The one-texture UI fallback loses those constants and
     * multiplies the reticle's zero alpha into otherwise visible RGB. */
    uint32_t hud_program = xv_hud_program_for_vs(fnv, xd3d_state.ps_shadow);
    if (!hud_program) hud_program = xv_composite_program_for_vs(fnv, xd3d_state.ps_shadow);
    /* Scene copies use this same declaration. Keep their combiner/blend state
     * and record them in the mesh list so backbuffer aliases resolve on replay. */
    int scene_copy = 0;
    for (unsigned t = 0; t < 4; t++) {
        uint32_t hdr = xd3d_state.texture[t];
        if (hdr && guest_u32(hdr + 4) && guest_u32(hdr + 4) == xd3d_backbuffer_data()) scene_copy = 1;
    }
    if (hud_program || scene_copy) {
        uint32_t h = xv_d3d_handle_for_hash(fnv);
        if (!h) return;
        xv_d3d_SetVertexShader(h);
        xv_d3d_SetTrackedConstants(xd3d_state.vsc, &xd3d_state.vsc_dirty_lo, &xd3d_state.vsc_dirty_hi);
        sync_draw_state();
        if (hud_program) xv_d3d_SetPixelShader(hud_program, 0, xd3d_state.psc);
        if (fnv == 0xBB2F446Bu) {
            float rows[8][4]; uint32_t sizes[4];
            for (unsigned t=0;t<4;t++) {
                uint32_t hdr=xd3d_state.texture[t];
                sizes[t]=hdr ? guest_u32(hdr+16) : 0;
            }
            xv_composite_texture_rows(rows,&xd3d_state.vsc[15],sizes);
            xv_d3d_SetVertexShaderConstant(-81,&rows[0][0],8);
        } else if (scene_copy) {
            /* Linear Xbox copy coordinates are pixels. The original VS applies
             * its own texture transform, then GXM needs normalized coordinates. */
            float scale[4]; memcpy(scale, xd3d_state.vsc[32], sizeof scale);
            uint32_t hdr = xd3d_state.texture[0];
            uint32_t size = hdr ? guest_u32(hdr + 16) : 0;
            if (size) { scale[0] /= (size & 0xFFF) + 1; scale[1] /= ((size >> 12) & 0xFFF) + 1; }
            xv_d3d_SetVertexShaderConstant(-64, scale, 1);
        }
        if (xd3d_hist_active()) {
            extern uint32_t xd3d_alpha_test(void);
            UI_LOG("[hist] hud ps %08X blendop %08X alpha %08X pos %.1f %.1f / %.1f %.1f\n",
                   xd3d_state.ps_hash, xd3d_state.blend_op, xd3d_alpha_test(),
                   v[0].a[0][0], v[0].a[0][1], v[2].a[0][0], v[2].a[0][1]);
            UI_LOG("[hist] hud sides %.1f %.1f / %.1f %.1f cull %X\n",
                   v[1].a[0][0], v[1].a[0][1], v[3].a[0][0], v[3].a[0][1], xd3d_state.cull);
            if (hud_program == 0x5D70F0B3u)
                for (unsigned i = 0; i < 4; i++)
                    UI_LOG("[hist] meter c%u %08X %08X\n", i,
                           xd3d_state.ps_shadow[0x28 / 4 + i], xd3d_state.ps_shadow[0x48 / 4 + i]);
        }
        for (unsigned first = 0; first + 4 <= n;) {
            ui_vtx packed[64];
            unsigned count = (n - first) & ~3u; if (count > 64) count = 64;
            for (unsigned i = 0; i < count; i++) {
                const xd3d_im_vtx *src = &v[first + i];
                packed[i] = (ui_vtx){src->a[0][0], src->a[0][1], src->a[4][0], src->a[4][1], pack_argb(src->a[9])};
            }
            xv_d3d_DrawImmediateStrided(X_D3DPT_QUADLIST, packed, count, sizeof(ui_vtx));
            first += count;
        }
        return;
    }
    unsigned prog = 0;
    for (uint32_t i = 0; i < g.nprog; ++i) if (g.prog[i].desc->func_hash == fnv) { prog = i; break; }
    unsigned stage = ps_image_stage(xd3d_state.ps_def);
    xv_ui_gxm_quads(v, n, xd3d_state.texture[stage & 3], NULL, prog, stage);
}

static volatile uint32_t g_mesh_frame = 0xFFFFFFFFu;    /* xv_d3d list to replay this frame */
uint32_t xv_ui_gxm_mesh_frame(void) { return g_mesh_frame; }
/* Frame-time split, logged every 60 frames: "game" = everything between two Presents that is not our
 * render (recompiled Halo + HLE + draw recording), "render" = xv_present (GXM submit + wait for the
 * previous frame + display flip).  The first hardware number that says where the time goes. */
static uint64_t g_t_last_present, g_t_game_acc, g_t_render_acc; static unsigned g_t_frames;
static inline uint64_t t_us(void) { extern uint64_t xk_os_monotonic_us(void); return xk_os_monotonic_us(); }
void xd3d_hist_small_check(unsigned frame, unsigned draws);
uint64_t xv_t_present_us;
static void xd3d_r_present_inner(unsigned frame, unsigned draws);
static unsigned g_report_pending;   /* frame + 1: a helper-side present's 60-frame report, run at the owner's join */
static void present_report(unsigned frame);
void xd3d_r_present(unsigned frame, unsigned draws)
{
    extern uint64_t xk_os_monotonic_us(void); uint64_t t0 = xk_os_monotonic_us(); xd3d_r_present_inner(frame, draws); xv_t_present_us += xk_os_monotonic_us() - t0;
}
static void xd3d_r_present_inner(unsigned frame, unsigned draws)
{
    /* Settings/benchmark transitions may touch upload policy or attachments.
     * Complete captured preparation before either transition, not just before
     * the eventual EndFrame publication. No full-GPU wait is added. */
    xv_vertex_capture_drain();
    xd3d_hist_small_check(frame, draws);
    (void)draws;
    extern void xv_present(void);
    uint64_t t0 = t_us();
    if (g_t_last_present) g_t_game_acc += t0 - g_t_last_present;
    { extern void xv_benchmark_present(void) __attribute__((weak));
      if(xv_benchmark_present)xv_benchmark_present(); }
    { extern void xv_settings_frame(void) __attribute__((weak));
      if(xv_settings_frame)xv_settings_frame(); }
    uint64_t ps_[8]; ps_[0] = t_us();
    if (g_mesh_path) g_mesh_frame = xv_d3d_EndFrame();
    ps_[1] = t_us();
    xv_ui_gxm_frame_flip();      /* seal; do not reset the next slot yet */
    ps_[2] = t_us();
    xv_gpu_flush_pending();     /* includes the current frame's late UI writes */
    ps_[3] = t_us();
    xv_present();               /* publish, then acquire only the next slots */
    ps_[4] = t_us();
    if (g_mesh_path) xv_d3d_BeginFrame();
    ps_[5] = t_us();
    xv_ui_gxm_frame_begin();
    ps_[6] = t_us();
    ui_tex_purge_if_needed(frame);
    uint64_t t1 = t_us(); ps_[7] = t1;
    g_t_render_acc += t1 - t0; g_t_last_present = t1;
    if (t1 - t0 > 300000u) {   /* perf133 (Sept 23): isolated ~6.6 s present-path stalls once every few windows; name the stage */
        static unsigned n_; if (n_++ < 20) UI_LOG("[present-stall] frame %u: %llu ms total; pre %llu EndFrame %llu flip %llu flush %llu present %llu BeginFrame %llu frame_begin %llu purge %llu ms\n", frame,
            (unsigned long long)((t1 - t0) / 1000), (unsigned long long)((ps_[0] - t0) / 1000), (unsigned long long)((ps_[1] - ps_[0]) / 1000), (unsigned long long)((ps_[2] - ps_[1]) / 1000),
            (unsigned long long)((ps_[3] - ps_[2]) / 1000), (unsigned long long)((ps_[4] - ps_[3]) / 1000), (unsigned long long)((ps_[5] - ps_[4]) / 1000), (unsigned long long)((ps_[6] - ps_[5]) / 1000), (unsigned long long)((ps_[7] - ps_[6]) / 1000));
    }
    if (++g_t_frames == 60) {
        /* A present on the scene helper (xd3d_present_flush_helper) leaves the reports to the owner's join: several of
         * them (object jobs, owner phases) read owner-side state. */
        { extern int xv_scene_thread_on_helper(void) __attribute__((weak));
          if (xv_scene_thread_on_helper && xv_scene_thread_on_helper()) { __atomic_store_n(&g_report_pending, frame + 1u, __ATOMIC_RELEASE); return; } }
        present_report(frame);
    }
}
void xd3d_r_present_owner_tail(void)
{
    unsigned f = __atomic_exchange_n(&g_report_pending, 0, __ATOMIC_ACQ_REL);
    if (f) present_report(f - 1u);
}
static void present_report(unsigned frame)
{
    {
        /* Measure the periodic report itself: its synchronous file writes
         * occur after t1 and are otherwise hidden in the next game interval. */
        uint64_t report_start=t_us();
        static int report_batch=-1;
        if (report_batch<0) {
            const char *e=getenv("XV_PROFILE_BATCH");
            report_batch=!e || atoi(e)!=0;
        }
        int grouped=report_batch && xv_log_report_begin_frame(frame);
#ifdef XV_OWNER_PHASE
        { extern void xv_owner_phase_report(unsigned); xv_owner_phase_report(g_t_frames); }
#endif
        { extern void xv_native_math_report(unsigned); extern void xd3d_prepare_report(unsigned);
          xv_native_math_report(g_t_frames); xd3d_prepare_report(g_t_frames); }
        { extern void xv_hle_dispatch_report(unsigned) __attribute__((weak));
          if(xv_hle_dispatch_report)xv_hle_dispatch_report(g_t_frames); }
#ifdef XV_NATIVE_OBJECT_SCAN
        { extern void xv_object_scan_report(unsigned); xv_object_scan_report(g_t_frames); }
#endif
#ifdef XV_NATIVE_OBJECT_COLLECT
        { extern void xv_object_collect_report(unsigned); xv_object_collect_report(g_t_frames); }
#endif
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
        { extern void xv_object_jobs_report(unsigned); xv_object_jobs_report(g_t_frames); }
#endif
        xv_draw_profile_report(g_t_frames);
        xv_vertex_upload_report(g_t_frames);
        xv_vertex_capture_report(g_t_frames);
        UI_LOG("[palette-cache] %u frames: %u reused / %u hashed (all 1024 bytes checked)\n",
            g_t_frames,g_palette_reused,g_palette_hashed);
        g_palette_reused=g_palette_hashed=0;
        if (g_flare_seen) UI_LOG("[flare-work] %u quads kept / %u outside screen in %u frames\n",
            g_flare_seen-g_flare_culled,g_flare_culled,g_t_frames);
        g_flare_seen=g_flare_culled=0;
        g_xv_ovl_game_ms = g_t_game_acc / 60000.0f; g_xv_ovl_render_ms = g_t_render_acc / 60000.0f;
        g_xv_ovl_fps = 60.0e6f / (float)(g_t_game_acc + g_t_render_acc + 1);
        { extern unsigned xv_d3d_draw_acc, xv_d3d_bsp_acc, xv_n_kicks, xv_n_fires; extern uint64_t xv_t_vbcb_us, xv_t_draw_us, xv_t_present_us; UI_LOG("frame time: game %.1f ms + wait %.1f ms = %.1f fps | pump %.1f ms | %u textures %u KB | decode %u tex %.1f ms | draws/frame %u bsp %u | frames %u | kicks %u fires %u vbcb %.1f ms draw-hle %.1f ms present %.1f ms (per frame)\n",
               g_t_game_acc / 60000.0, g_t_render_acc / 60000.0, 60.0e6 / (double)(g_t_game_acc + g_t_render_acc + 1), xv_pump_us_acc / 60000.0, g.texcount, g.dec_off >> 10, g_dec_n, g_dec_us / 1000.0, xv_d3d_draw_acc / (g_t_frames ? g_t_frames : 1), xv_d3d_bsp_acc / (g_t_frames ? g_t_frames : 1), g_t_frames, xv_n_kicks / (g_t_frames ? g_t_frames : 1), xv_n_fires / (g_t_frames ? g_t_frames : 1), xv_t_vbcb_us / 1000.0 / (g_t_frames ? g_t_frames : 1), xv_t_draw_us / 1000.0 / (g_t_frames ? g_t_frames : 1), xv_t_present_us / 1000.0 / (g_t_frames ? g_t_frames : 1)); xv_d3d_draw_acc = xv_d3d_bsp_acc = 0; xv_n_kicks = xv_n_fires = 0; xv_t_vbcb_us = xv_t_draw_us = xv_t_present_us = 0; } g_dec_n = 0; g_dec_us = 0; xv_texture_worker_report(); xv_geometry_worker_report();
        g_t_frames = 0; g_t_game_acc = g_t_render_acc = 0; xv_pump_us_acc = 0;
        if (grouped) xv_log_report_end();
        uint64_t report_us=t_us()-report_start;
        xv_log_status logs; xv_log_get_status(&logs);
        if(logs.enabled) {
            int cost_group=xv_log_report_begin_frame(frame);
            UI_LOG("[profile-cost] frame %u batch %d producer-us %llu; formatting/enqueue/backpressure, excludes worker output and this line\n",
                frame,grouped,(unsigned long long)report_us);
            UI_LOG("[log-worker] cumulative accepted/written/synced %llu/%llu/%llu queued %u high %u error %d; waits %llu %.3f ms console %.3f file-wait %.3f file %.3f sync %.3f ms\n",
                (unsigned long long)logs.accepted,(unsigned long long)logs.written,(unsigned long long)logs.synced,
                logs.queued,logs.high_water,logs.error,(unsigned long long)logs.backpressure_count,
                logs.backpressure_us/1000.0,logs.console_us/1000.0,logs.file_wait_us/1000.0,logs.file_us/1000.0,logs.sync_us/1000.0);
            if(cost_group) xv_log_report_end();
        } else UI_LOG("[profile-cost] frame %u batch %d report-us %llu; includes formatting and synchronous log writes, excludes this line\n",
            frame,grouped,(unsigned long long)report_us);
    }
}
