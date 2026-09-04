/*
 * xv_ui_gxm.c - hardware (GXM) renderer for the recompiled engine's immediate-mode UI path.
 * See xv_ui_gxm.h for the design; see recomp/kernel/xd3d.c for the caller (xd3d_r_* hooks).
 *
 * Flat C, no per-frame heap churn: rings are allocated once, batches live in a fixed pool, and the
 * only work on the game fiber is a tight pack loop that streams vertices straight into GPU memory.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>

#include "xv_shader.h"
extern void xv_gpu_flush(const void *ptr, uint32_t len);   /* dcache clean before GPU read (main.c) */
#include "xv_ui_gxm.h"
#include "shaders/xv_layouts.h"           /* xv_vs_halo_vs_03, xv_vs_clear, xv_halo_vs[] */

#include "xv_log.h"
#define UI_LOG(...)  xv_logf("[xv/ui] " __VA_ARGS__)

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((uint32_t)(a) - 1))
#define UI_DECL_VA       0x001E13ECu       /* Halo's screen-space UI vertex declaration */
#define UI_MAX_QUADS     4096u
#define UI_MAX_VERTS     (UI_MAX_QUADS * 4u)
#define UI_MAX_BATCHES   1024u
#define UI_FRAMES        2u                /* record double-buffer (frame parity) */
#define UI_TEX_CACHE     768u
#define UI_TEX_MAXDIM    256u              /* decode no mip level larger than this (level textures are 512-1024) */
#define UI_TEX_BAD       1024u
#define UI_MAX_PROGS     4u                /* vertex programs sharing the UI declaration (vs_03/04/38) */
#define UI_MAX_STAGES    4u                /* NV2A texture stages */
#define OVL_MAX_QUADS    320u              /* debug overlay: 7-segment digits drawn with the clear program */

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
    uint32_t      first_vertex;            /* into the current frame's vertex half */
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
} ui_frame;

/* built texture control words, cached by (guest data pointer, format word) */
typedef struct { uint32_t data, fmtword, palsum; SceGxmTexture tex; int valid; uint32_t sum, bytes; unsigned checked; uint16_t stable; uint8_t dirty; int16_t next; } ui_tex_entry;   /* stable: consecutive unchanged re-checks (map textures stop being re-hashed once settled) */   /* palsum: hash of the P8 palette used (0 = none) */   /* sum: content hash of dynamic textures; next: hash chain */

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
    xv_fshader_t     clear_fs;

    /* GPU-mapped memory */
    SceUID           vbuf_uid;  ui_vtx  *vbuf;         /* UI_FRAMES * UI_MAX_VERTS */
    SceUID           ibuf_uid;  uint16_t *ibuf;        /* UI_MAX_QUADS * 6, built once */
    SceUID           clr_uid;   clr_vtx *clrbuf;       /* UI_FRAMES * (4 fullscreen verts + OVL_MAX_QUADS*4 overlay) */
    SceUID           dec_uid;   uint8_t *dec_base;     /* CPU-decoded linear RGBA8 texture pool */
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
    *uid = sceKernelAllocMemBlock("xv_ui", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, size, NULL);
    if (*uid < 0) { UI_LOG("alloc %u KB failed: 0x%08X\n", size >> 10, *uid); return NULL; }
    sceKernelGetMemBlockBase(*uid, &base);
    int err = sceGxmMapMemory(base, size, SCE_GXM_MEMORY_ATTRIB_READ);
    if (err != SCE_OK) { UI_LOG("map failed: 0x%08X\n", err); sceKernelFreeMemBlock(*uid); return NULL; }
    return base;
}


/* ---- CPU texture decode to linear RGBA8 (bytes R,G,B,A == GXM U8U8U8U8_ABGR) --------------------
 * The GPU's hardware twiddle does not match the NV2A layout for Xbox's swizzled/DXT UI atlas, so we
 * de-swizzle + decompress once on the CPU (textures are cached).  Same logic proved correct on host. */
static uint32_t ui_morton1(uint32_t x){ x&=0x55555555; x=(x|(x>>1))&0x33333333; x=(x|(x>>2))&0x0F0F0F0F; x=(x|(x>>4))&0x00FF00FF; x=(x|(x>>8))&0x0000FFFF; return x; }
static void ui_unswz(unsigned w, unsigned h, unsigned idx, unsigned *ox, unsigned *oy){
    unsigned bx=ui_morton1(idx), by=ui_morton1(idx>>1), lw=0, lh=0, t;
    for(t=w;t>1;t>>=1)lw++; for(t=h;t>1;t>>=1)lh++;
    unsigned c=lw<lh?lw:lh, m=(1u<<c)-1;
    if(lw>=lh){*ox=(bx&m)|((idx>>(2*c))<<c);*oy=by&m;} else {*oy=(by&m)|((idx>>(2*c))<<c);*ox=bx&m;}
    { static int tr = -1; if (tr < 0) tr = getenv("XV_SWZ_T") != NULL; if (tr && w == h) { unsigned t2 = *ox; *ox = *oy; *oy = t2; } }   /* diagnostic: transposed Morton */
}
/* GXM (PowerVR) twiddle: same min-square/long-axis scheme, but Y occupies the even (low) bits and X the
 * odd ones - the transpose of NV2A's order.  Used when handing block-compressed data to the GPU as-is. */
static void gxm_unswz(unsigned w, unsigned h, unsigned idx, unsigned *ox, unsigned *oy){
    unsigned by=ui_morton1(idx), bx=ui_morton1(idx>>1), lw=0, lh=0, t;
    for(t=w;t>1;t>>=1)lw++; for(t=h;t>1;t>>=1)lh++;
    unsigned c=lw<lh?lw:lh, m=(1u<<c)-1;
    if(lw>=lh){*ox=(bx&m)|((idx>>(2*c))<<c);*oy=by&m;} else {*oy=(by&m)|((idx>>(2*c))<<c);*ox=bx&m;}
}
static uint32_t ui_c565(uint16_t v){ unsigned r=(v>>11)&31,g=(v>>5)&63,b=v&31; return 0xFF000000u|((b*255/31)<<16)|((g*255/63)<<8)|(r*255/31); }
static uint32_t ui_c4444(uint16_t v){ unsigned a=(v>>12)&15,r=(v>>8)&15,g=(v>>4)&15,b=v&15; return ((a*17u)<<24)|((b*17u)<<16)|((g*17u)<<8)|(r*17u); }
static uint32_t ui_c1555(uint16_t v){ unsigned a=v>>15?255:0,r=(v>>10)&31,g=(v>>5)&31,b=v&31; return (a<<24)|((b*255/31)<<16)|((g*255/31)<<8)|(r*255/31); }
static void ui_dxt(const uint8_t *s, int fmt, uint32_t out[16]){
    const uint8_t *cb = fmt==0x0C ? s : s+8;
    uint16_t c0=cb[0]|(cb[1]<<8), c1=cb[2]|(cb[3]<<8);
    uint32_t p[4]={ui_c565(c0),ui_c565(c1),0,0};
    if(c0>c1||fmt!=0x0C){ for(int i=0;i<3;i++){unsigned a=(p[0]>>(i*8))&0xFF,b=(p[1]>>(i*8))&0xFF; p[2]|=(((2*a+b)/3)<<(i*8)); p[3]|=(((a+2*b)/3)<<(i*8));} p[2]|=0xFF000000u; p[3]|=0xFF000000u; }
    else { for(int i=0;i<3;i++){unsigned a=(p[0]>>(i*8))&0xFF,b=(p[1]>>(i*8))&0xFF; p[2]|=(((a+b)/2)<<(i*8));} p[2]|=0xFF000000u; }
    uint32_t bits=cb[4]|(cb[5]<<8)|(cb[6]<<16)|((uint32_t)cb[7]<<24);
    for(int i=0;i<16;i++) out[i]=p[(bits>>(i*2))&3];
    if(fmt==0x0E) for(int i=0;i<16;i++){ unsigned a4=(s[i/2]>>((i&1)*4))&0xF; out[i]=(out[i]&0x00FFFFFFu)|((a4*17u)<<24); }
    else if(fmt==0x0F){ unsigned a0=s[0],a1=s[1]; uint64_t ab=0; for(int i=0;i<6;i++) ab|=(uint64_t)s[2+i]<<(i*8);
        for(int i=0;i<16;i++){ unsigned code=(ab>>(i*3))&7,a; if(code==0)a=a0; else if(code==1)a=a1; else if(a0>a1)a=((8-code)*a0+(code-1)*a1)/7; else if(code==6)a=0; else if(code==7)a=255; else a=((6-code)*a0+(code-1)*a1)/5; out[i]=(out[i]&0x00FFFFFFu)|(a<<24);} }
}
/* decode guest texture -> dst (w*h RGBA8, bytes R,G,B,A). Returns 0 on success. */
static const uint32_t *g_cur_pal;       /* palette (guest memory, D3DCOLOR) for the P8 texture being decoded */
static uint32_t g_cur_palsum;
static int ui_decode(const uint8_t *src, unsigned fmt, unsigned w, unsigned h, unsigned pitch, int linear, uint32_t *dst)
{
    if (fmt==0x0C || fmt==0x0E || fmt==0x0F) {                         /* DXT: linear 4x4 block order */
        unsigned bw=(w+3)/4, bh=(h+3)/4, bs=(fmt==0x0C?8:16);
        for (unsigned by=0; by<bh; ++by) for (unsigned bx=0; bx<bw; ++bx) {
            uint32_t blk[16]; ui_dxt(src + (by*bw+bx)*bs, fmt, blk);
            for (unsigned i=0;i<16;i++){ unsigned x=bx*4+(i&3), y=by*4+(i>>2); if(x<w&&y<h) dst[y*w+x]=blk[i]; }
        }
    } else if (fmt==0x12 || fmt==0x1E || fmt==0x3F || fmt==0x40 || fmt==0x41) {   /* linear 32-bit */
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++){ const uint8_t *p=src+y*pitch+x*4; uint32_t r,g,b,a;
            if(fmt==0x3F){a=p[3];b=p[2];g=p[1];r=p[0];} else if(fmt==0x41){r=p[3];g=p[2];b=p[1];a=p[0];}
            else if(fmt==0x40){b=p[3];g=p[2];r=p[1];a=p[0];} else {b=p[0];g=p[1];r=p[2];a=fmt==0x1E?255:p[3];}
            dst[y*w+x]=(a<<24)|(b<<16)|(g<<8)|r; }
    } else if (fmt==0x06 || fmt==0x07) {                              /* swizzled A8R8G8B8 */
        for (unsigned i=0;i<w*h;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y); const uint8_t *p=src+i*4;
            if(x<w&&y<h) dst[y*w+x]=((fmt==0x07?255u:p[3])<<24)|(p[0]<<16)|(p[1]<<8)|p[2]; }
    } else if (fmt==0x02 || fmt==0x03 || fmt==0x04 || fmt==0x05) {    /* swizzled 16-bit */
        for (unsigned i=0;i<w*h;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y); uint16_t v=src[i*2]|(src[i*2+1]<<8);
            if(x<w&&y<h) dst[y*w+x]= fmt==0x05?ui_c565(v): fmt==0x04?ui_c4444(v): ui_c1555(v); }
    } else if (fmt==0x10 || fmt==0x11 || fmt==0x1C || fmt==0x1D) {    /* linear 16-bit */
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++){ uint16_t v=src[y*pitch+x*2]|(src[y*pitch+x*2+1]<<8);
            dst[y*w+x]= fmt==0x11?ui_c565(v): fmt==0x1D?ui_c4444(v): ui_c1555(v); }
    } else if (fmt==0x0B && g_cur_pal) {                              /* P8 swizzled through the bound palette (D3DCOLOR ARGB) */
        static int flat = -1; if (flat < 0) flat = getenv("XV_P8_FLAT") != NULL;   /* diagnostic: every P8 texture becomes a flat tangent-space normal */
        for (unsigned i=0;i<w*h;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y); uint32_t p=g_cur_pal[src[i]];
            if(x<w&&y<h) dst[y*w+x]=flat ? 0xFFFF8080u : (p&0xFF000000u)|((p&0xFF)<<16)|(p&0xFF00)|((p>>16)&0xFF); }
    } else if (fmt==0x0B || fmt==0x00 || fmt==0x19 || fmt==0x01) {   /* 8-bit swizzled: L8/P8, A8, AL8 */
        for (unsigned i=0;i<w*h;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y); uint8_t l=src[i];
            if(x<w&&y<h) dst[y*w+x]= fmt==0x19 ? ((uint32_t)l<<24)|0x00FFFFFF : fmt==0x01 ? ((uint32_t)l<<24)|(l<<16)|(l<<8)|l : 0xFF000000u|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x13 || fmt==0x1F || fmt==0x1B) {                  /* 8-bit linear: L8, A8, AL8 */
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++){ uint8_t l=src[y*pitch+x];
            dst[y*w+x]= fmt==0x1F ? ((uint32_t)l<<24)|0x00FFFFFF : fmt==0x1B ? ((uint32_t)l<<24)|(l<<16)|(l<<8)|l : 0xFF000000u|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x1A) {                                             /* A8L8 swizzled */
        for (unsigned i=0;i<w*h;i++){ unsigned x,y; ui_unswz(w,h,i,&x,&y); uint8_t l=src[i*2], a=src[i*2+1];
            if(x<w&&y<h) dst[y*w+x]=((uint32_t)a<<24)|(l<<16)|(l<<8)|l; }
    } else if (fmt==0x20) {                                             /* LIN_A8L8 */
        for (unsigned y=0;y<h;y++) for (unsigned x=0;x<w;x++){ uint8_t l=src[y*pitch+x*2], a=src[y*pitch+x*2+1]; dst[y*w+x]=((uint32_t)a<<24)|(l<<16)|(l<<8)|l; }
    } else { (void)linear; return -1; }
    return 0;
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
    uint32_t data = guest_u32(hdr + 4);              /* X_D3DPixelContainer.Data (guest phys) */
    uint32_t fmtword = guest_u32(hdr + 12);
    uint32_t sizeword = guest_u32(hdr + 16);
    if (!data) return NULL;

    unsigned fmt0 = (fmtword >> 8) & 0xFF;
    /* P8 with a palette bound: decode through it (bump maps hold normals in the palette); its identity is
     * part of the cache key.  Without a palette P8 falls back to luminance. */
    g_cur_pal = NULL; g_cur_palsum = 0;
    if (fmt0 == 0x0B && pal_guest) { g_cur_pal = (const uint32_t *)X_G(pal_guest); g_cur_palsum = ui_tex_hash((const uint8_t *)g_cur_pal, 1024) | 1u; }
    int lum_only = (fmt0 == 0x00 || fmt0 == 0x13 || (fmt0 == 0x0B && !g_cur_pal));        /* L8 swz, LIN_L8, P8-as-L8 */
    if (!lum_only) coverage = 0;
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
            /* tiny textures (Halo's 4x4 stage dummies, 1D ramps) are rewritten by the CPU, never by a file read,
             * so the read-based invalidation cannot catch them: re-hash them every frame (64 bytes, free) */
            int due = e->bytes && e->checked != g.rec_frame &&
                      (dynamic || e->dirty || e->bytes <= 1024 || (e->stable < 24 && ((g.rec_frame + (unsigned)i) & 15u) == 0));
            if (due) {
                e->checked = g.rec_frame;
                uint32_t sum = ui_tex_hash(X_G(0x80000000u | data), e->bytes);
                if (sum != e->sum) { re = e; static unsigned n; if (n++ < 12) UI_LOG("tex %08X fmt %02X changed (check %u%s): re-decoding in place\n", data, (fmtword >> 8) & 0xFF, e->stable, e->dirty ? ", file read" : ""); e->dirty = 0; break; }   /* stale: re-decode below */
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
     * bytes.  Decode level 0 of every face to RGBA, twiddle into GXM's cube layout, one mip. */
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
        uint32_t face_stride = 0; { unsigned lw = w; for (unsigned l = 0; l < levels; ++l) { face_stride += lw * lw * 4u; lw >>= 1; } }
        if (w >= 16) face_stride = ALIGN_UP(face_stride, 2048);
        uint32_t need = ALIGN_UP(6u * face_stride, 64);
        if (g.dec_off + need > g.dec_cap) { g.tex_purge = 1; return NULL; }
        if (re) re->valid = 0;                                    /* source changed: drop the stale entry, build a fresh one (its pool space is reclaimed at the next purge) */
        uint32_t *dst = (uint32_t *)(g.dec_base + g.dec_off);
        static uint32_t tmp[256 * 256], tmp2[128 * 128];
        const uint8_t *base = X_G(0x80000000u | data);
        memset(dst, 0, need);
        for (unsigned f = 0; f < 6; ++f) {
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
        int err = sceGxmTextureInitCube(&e->tex, dst, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitCube(%ux%u fmt %02X, %u mips) failed 0x%08X\n", w, h, fmt, levels, err); UI_TEX_FAIL(); }
        sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        g.dec_off += need;
        e->data = data; e->fmtword = fmtword; e->palsum = g_cur_palsum; e->valid = 1; g.texcount++;
        e->next = g.texhash[bucket]; g.texhash[bucket] = (int16_t)(e - g.texcache);
        e->bytes = 6u * face_bytes; if (e->bytes > 512 * 1024) e->bytes = 512 * 1024; e->sum = ui_tex_hash(base, e->bytes); e->checked = g.rec_frame; e->stable = 0; e->dirty = 0;
        { static unsigned n; if (n++ < 8) UI_LOG("cube map fmt %02X %ux%u x6 (face stride %u) -> cube texture\n", fmt, w, h, face_bytes); }
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
    int err, as_bc = 0;
    /* square only: GXM's twiddle for non-square textures is not the min-square/long-axis layout we
     * assumed (ring/planet textures streaked) - those take the decode path at the capped mip instead */
    if (isdxt && w == h && ui_is_pow2(w) && w >= 4) {
        /* Upload DXT as-is: NV2A stores 4x4 blocks row-major, GXM's swizzled layout wants the blocks
         * twiddled (PowerVR order, see gxm_unswz), so reorder 8/16-byte blocks - no decompression. */
        /* whole mip chain down to 4x4 (Xbox stores the levels back to back; GXM swizzled BC levels are
         * concatenated too): without mips the hull fly-by at grazing angles aliased into streaks */
        /* XV_BC_MIPS=0 (default until verified on hardware): upload level 0 only - the chained-level layout
         * may be what faulted the GPU at the main menu on 2026-09-02 21:50 */
        static int bcmips = -1; if (bcmips < 0) { const char *e = getenv("XV_BC_MIPS"); bcmips = e ? atoi(e) : 0; }
        unsigned levels = 0; uint32_t need = 0;
        { unsigned lw = w; while (lw >= 4 && levels < (bcmips ? mips : 1)) { need += (lw / 4) * (lw / 4) * bs; lw >>= 1; levels++; } }
        need = ALIGN_UP(need, 64);
        if (!re && g.dec_off + need > g.dec_cap) { g.tex_purge = 1; return NULL; }
        uint8_t *dst = re ? (uint8_t *)sceGxmTextureGetData(&e->tex) : g.dec_base + g.dec_off, *o = dst; const uint8_t *lsrc = src;
        for (unsigned l = 0, lw = w; l < levels; ++l, lw >>= 1) {
            unsigned bw = lw / 4, nb = bw * bw;
            for (unsigned i = 0; i < nb; ++i) { unsigned x, y; gxm_unswz(bw, bw, i, &x, &y); memcpy(o + i * bs, lsrc + (y * bw + x) * bs, bs); }
            o += nb * bs; lsrc += nb * bs;
        }
        xv_gpu_flush(dst, need);
        err = sceGxmTextureInitSwizzled(&e->tex, dst, gf, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitSwizzled(%02X %ux%u, %u mips) failed 0x%08X\n", fmt, w, h, levels, err); UI_TEX_FAIL(); }
        if (levels > 1) sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        if (!re) g.dec_off += need; as_bc = levels > 1 ? 2 : 1;
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
        if (!re && g.dec_off + need > g.dec_cap) { g.tex_purge = 1; return NULL; }
        uint32_t *dst = re ? (uint32_t *)sceGxmTextureGetData(&e->tex) : (uint32_t *)(g.dec_base + g.dec_off);
        uint64_t dec_t0 = xk_os_monotonic_us();
        if (ui_decode(src, fmt, w, h, pitch ? pitch : w * bpp, linear, dst) != 0) {
            UI_LOG("unhandled tex fmt %02X (%ux%u)\n", fmt, w, h); UI_TEX_FAIL();
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
        if (coverage) {                                   /* L8 for the UI path: coverage -> alpha, RGB white */
            for (unsigned i = 0, N = w * h; i < N; ++i) dst[i] = ((dst[i] & 0xFFu) << 24) | 0x00FFFFFFu;
        } else if (!lum_only) {
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
        xv_gpu_flush(dst, need);
        err = sceGxmTextureInitLinear(&e->tex, dst, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, levels);
        if (err != SCE_OK) { UI_LOG("textureInitLinear(%ux%u, %u mips) failed 0x%08X\n", w, h, levels, err); UI_TEX_FAIL(); }
        sceGxmTextureSetMipFilter(&e->tex, SCE_GXM_TEXTURE_MIP_FILTER_ENABLED);
        if (!re) g.dec_off += need;
        g_dec_us += xk_os_monotonic_us() - dec_t0; g_dec_n++;
    }
    e->data = data; e->fmtword = fmtword; e->palsum = g_cur_palsum; e->valid = 1;
    if (!re) { g.texcount++; e->next = g.texhash[bucket]; g.texhash[bucket] = (int16_t)(e - g.texcache); }
    e->bytes = 0; e->sum = 0; e->checked = g.rec_frame; e->stable = 0;
    {                                                                 /* size of the source level we consumed */
        e->bytes = isdxt ? ((w + 3) / 4) * ((h + 3) / 4) * bs : (pitch ? pitch : w * bpp) * h;
        if (e->bytes > 512 * 1024) e->bytes = 512 * 1024;
        e->sum = ui_tex_hash(src, e->bytes);
    }
    { static unsigned n; if (n++ < 24) UI_LOG("tex fmt %02X %ux%u -> %s (%u KB used)\n", fmt, w, h, as_bc ? "BC" : "RGBA", g.dec_off >> 10); }
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
        uint32_t a = e->data & 0x7FFFFFFFu, b = a + e->bytes;
        if (a < hi && lo < b) { e->dirty = 1; n++; }
    }
    if (n) { static unsigned m; if (m++ < 8) UI_LOG("file read %08X+%u overlaps %u cached texture(s): re-check on next bind\n", va, len, n); }
}
static void ui_tex_purge_if_needed(unsigned frame)
{
    if (!g.tex_purge) return;
    g.tex_purge = 0;
    if (frame - g.tex_purge_frame < 8 && g.tex_purges) return;
    sceGxmDisplayQueueFinish();
    UI_LOG("texture purge #%u at frame %u (%u textures, %u KB, %u bad)\n", ++g.tex_purges, frame, g.texcount, g.dec_off >> 10, g.nbad);
    g.texcount = 0; g.dec_off = 0; g.nbad = 0; g.tex_purge_frame = frame;
    memset(g.texhash, 0xFF, sizeof g.texhash);
}

const SceGxmTexture *xv_ui_gxm_texture(uint32_t hdr) { return g.ready ? ui_texture_for(hdr, 0) : NULL; }
const SceGxmTexture *xv_ui_gxm_texture_pal(uint32_t hdr, uint32_t pal_guest) { return g.ready ? ui_texture_for_pal(hdr, 0, pal_guest) : NULL; }

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

    g.vbuf = ui_gpu_alloc(UI_FRAMES * UI_MAX_VERTS * sizeof(ui_vtx), &g.vbuf_uid);
    g.ibuf = ui_gpu_alloc(UI_MAX_QUADS * 6 * sizeof(uint16_t), &g.ibuf_uid);
    g.clrbuf = ui_gpu_alloc(UI_FRAMES * (4 + OVL_MAX_QUADS * 4) * sizeof(clr_vtx), &g.clr_uid);
    g.dec_cap = 32 * 1024 * 1024;                                  /* texture pool (BC as-is, others decoded RGBA) */
    g.dec_base = ui_gpu_alloc(g.dec_cap, &g.dec_uid);
    if (!g.vbuf || !g.ibuf || !g.clrbuf || !g.dec_base) return -1;

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
    if (!g.ready) return;
    if (g.vbuf) { sceGxmUnmapMemory(g.vbuf); sceKernelFreeMemBlock(g.vbuf_uid); }
    if (g.ibuf) { sceGxmUnmapMemory(g.ibuf); sceKernelFreeMemBlock(g.ibuf_uid); }
    if (g.clrbuf) { sceGxmUnmapMemory(g.clrbuf); sceKernelFreeMemBlock(g.clr_uid); }
    if (g.dec_base) { sceGxmUnmapMemory(g.dec_base); sceKernelFreeMemBlock(g.dec_uid); }
    for (uint32_t i = 0; i < g.nprog; ++i) {
        for (int st = 0; st < (int)UI_MAX_STAGES; ++st) if (g.prog[i].fs_ok[st]) xv_fshader_unload(&g.prog[i].fs[st]);
        xv_vshader_unload(&g.prog[i].vs);
    }
    xv_fshader_unload(&g.clear_fs); xv_vshader_unload(&g.clear_vs);
    g.ready = 0;
}

int xv_ui_gxm_ready(void) { return g.ready; }

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

    ui_batch *b = &fr->batches[fr->bcount++];
    b->first_vertex = fr->vcount;
    b->nquads = quads;
    b->prog = (uint8_t)prog; b->stage = (uint8_t)stage;
    const SceGxmTexture *t = ui_texture_for(tex_hdr, 1);
    b->has_tex = t != NULL;
    if (t) b->tex = *t;
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
    /* flush the vertex/clear data we just wrote so the GPU sees it */
    xv_gpu_flush(fr->verts, fr->vcount * sizeof(ui_vtx));
    __atomic_store_n(&g.pub, (int32_t)g.rec, __ATOMIC_RELEASE);    /* hand this frame to the pump */
    g.rec = (g.rec + 1u) % UI_FRAMES;                             /* start recording the next */
    ui_frame *nx = &g.frame[g.rec];
    nx->vcount = 0; nx->bcount = 0; nx->has_clear = 0; nx->overflow = 0;
}

/* ---- replay (render pump, inside sceGxmBeginScene/EndScene) ------------------------------------- */

/* ---- debug overlay: fps + frame-time split as 7-segment digits (no font needed) -------------------
 * Toggle: hold SELECT+START ~1 s (xk_os_vita pad poll) or XV_FPS=1 in xita.cfg. */
int g_xv_overlay_on = -1;                                   /* -1 = read config once */
float g_xv_ovl_game_ms, g_xv_ovl_render_ms, g_xv_ovl_fps;
static const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };   /* abcdefg */
static unsigned ovl_rect(clr_vtx *v, unsigned n, float x, float y, float w, float h, uint32_t col)
{
    if (n + 4 > OVL_MAX_QUADS * 4) return n;
    /* pixel space (960x544) -> clip; y down */
    float x0 = x / 480.0f - 1.0f, x1 = (x + w) / 480.0f - 1.0f, y0 = 1.0f - y / 272.0f, y1 = 1.0f - (y + h) / 272.0f;
    v[n++] = (clr_vtx){ x0, y0, 0.5f, col }; v[n++] = (clr_vtx){ x1, y0, 0.5f, col };
    v[n++] = (clr_vtx){ x1, y1, 0.5f, col }; v[n++] = (clr_vtx){ x0, y1, 0.5f, col };
    return n;
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

static void xv_ui_gxm_overlay(SceGxmContext *ctx, int32_t idx)
{
    if (g_xv_overlay_on < 0) { const char *e = getenv("XV_FPS"); g_xv_overlay_on = e ? atoi(e) != 0 : 0; }
    if (!g_xv_overlay_on) return;
    clr_vtx *v = &g.clrbuf[idx * (4 + OVL_MAX_QUADS * 4) + 4]; unsigned n = 0;
    const uint32_t bg = 0xA0000000u, fps_col = 0xFF40FF40u, game_col = 0xFFFFD040u, rend_col = 0xFF40C0FFu;
    /* panel top-right: fps | game ms | render ms  (colours: green / amber / blue) */
    n = ovl_rect(v, n, 960 - 190, 6, 184, 30, bg);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_fps + 0.5f), 960 - 136, 11, 10, fps_col, 1);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_game_ms + 0.5f), 960 - 74, 11, 10, game_col, 1);
    n = ovl_number(v, n, (unsigned)(g_xv_ovl_render_ms + 0.5f), 960 - 12, 11, 10, rend_col, 1);
    if (!n) return;
    xv_gpu_flush(v, n * sizeof(clr_vtx));
    sceGxmSetFrontDepthFunc(ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    xv_shader_bind(ctx, &g.clear_vs, &g.clear_fs);
    const void *streams[1] = { v };
    xv_vshader_set_streams(ctx, &g.clear_vs, streams);
    sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, (n / 4) * 6);
}

void xv_ui_gxm_replay(SceGxmContext *ctx)
{
    if (!g.ready) return;
    int32_t idx = __atomic_load_n(&g.pub, __ATOMIC_ACQUIRE);
    if (idx < 0) return;
    ui_frame *fr = &g.frame[idx];

    /* Halo's own VS emits D3D clip space (+y up); GXM's default viewport (yScale = -height/2) already
     * puts +y at the top of the 960x544 target, so keep it explicit here (xv_d3d may have changed it). */
    sceGxmSetViewport(ctx, 480.0f, 480.0f, 272.0f, -272.0f, 0.5f, 0.5f);
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
        xv_gpu_flush(cv, 4 * sizeof(clr_vtx));
        xv_shader_bind(ctx, &g.clear_vs, &g.clear_fs);
        const void *streams[1] = { cv };
        xv_vshader_set_streams(ctx, &g.clear_vs, streams);
        sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, 6);
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

        sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g.ibuf, b->nquads * 6);
    }
    xv_ui_gxm_overlay(ctx, idx);
}

/* ================================================================================================
 *  xd3d_r_* hook overrides.  The recompiled engine (recomp/kernel/xd3d.c) declares these weak; on
 *  the Vita this file provides the strong definitions, so the same game code drives GXM.
 * ============================================================================================== */
#include "xv_d3d.h"
static int g_mesh_path = 1;                              /* world geometry through xv_d3d (set 0 to fall back to UI-only clears) */
/* Render-to-texture passes (Halo: dynamic object shadows, some fog/reflection effects) are dropped
 * for now: while an offscreen surface is the target nothing is recorded, and draws that sample one
 * of those surfaces are skipped by xv_d3d.  Without this, the offscreen clears wiped the frame. */
static int g_offscreen;
void xd3d_r_state(const char *what, uint32_t a, uint32_t b, uint32_t v)
{
    (void)b;
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
static uint32_t gl_blend_to_d3d(uint32_t gl)
{
    switch (gl) { case 0: return 1; case 1: return 2; case 0x300: return 3; case 0x301: return 4; case 0x302: return 5; case 0x303: return 6;
                  case 0x304: return 7; case 0x305: return 8; case 0x306: return 9; case 0x307: return 10; case 0x308: return 11; default: return 2; }
}
void xd3d_r_draw(xctx *c, int indexed, uint32_t prim, uint32_t count, uint32_t data)
{
    { extern void xd3d_hist_tex_check(void); xd3d_hist_tex_check(); }
    (void)c;
    if (!g_mesh_path) return;
    uint32_t vs = xd3d_state.vs_handle;
    if (!(vs & 1)) return;                                             /* FVF draws: not yet */
    uint32_t fnv = guest_u32((vs & ~1u) + 12);
    uint32_t h = xv_d3d_handle_for_hash(fnv);
    if (!h) { static unsigned n; if (n++ < 8) UI_LOG("draw: no program for VS fnv %08X\n", fnv); return; }
    xv_d3d_SetVertexShader(h);
    xv_d3d_SetAllConstants(xd3d_state.vsc);
    xd3d_ps_sync();
    xv_d3d_SetPixelShader(xd3d_state.ps_hash, xd3d_state.psc);
    for (unsigned i = 0; i < 4; ++i) {
        xv_d3d_SetStreamSource(i, xd3d_state.stream_vb[i], xd3d_state.stream_stride[i]);
        xv_d3d_SetTexture(i, xd3d_state.texture[i]);
        xv_d3d_SetTexturePalette(i, xd3d_state.palette[i]);
    }
    /* the kernel model keeps the NV2A/GL tokens the game wrote; xv_d3d speaks D3D enums */
    xv_d3d_SetRenderState_ZEnable(xd3d_state.z_enable);
    xv_d3d_SetRenderState_ZWriteEnable(xd3d_state.z_write);
    xv_d3d_SetRenderState_ZFunc(xd3d_state.z_func >= 0x200 && xd3d_state.z_func <= 0x207 ? xd3d_state.z_func - 0x200 + 1 : 4);
    xv_d3d_SetRenderState_CullMode(xd3d_state.cull == 0x900 ? 2 : xd3d_state.cull == 0x901 ? 3 : 1);   /* GL_CW / GL_CCW / none */
    xv_d3d_SetRenderState_AlphaBlendEnable(xd3d_state.alpha_blend);
    { uint32_t m = xd3d_state.color_mask; xv_d3d_SetRenderState_ColorWriteEnable(((m >> 16) & 1) | ((m >> 7) & 2) | ((m & 1) << 2) | ((m >> 21) & 8)); }   /* -> D3D bits R1 G2 B4 A8 */
    xv_d3d_SetRenderState_SrcBlend(gl_blend_to_d3d(xd3d_state.src_blend));
    xv_d3d_SetRenderState_DestBlend(gl_blend_to_d3d(xd3d_state.dst_blend));
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

void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n)
{
    if (prim != 7 || n < 4 || g_offscreen) return;       /* QUADLIST only (the UI path) */
    uint32_t vs = xd3d_state.vs_handle;
    uint32_t decl = (vs & 1) ? guest_u32(vs & ~1u) : 0;
    if (decl != UI_DECL_VA) return;                      /* other decls handled elsewhere */
    uint32_t fnv = (vs & 1) ? guest_u32((vs & ~1u) + 12) : 0;
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
void xd3d_r_present(unsigned frame, unsigned draws)
{
    extern uint64_t xk_os_monotonic_us(void); uint64_t t0 = xk_os_monotonic_us(); xd3d_r_present_inner(frame, draws); xv_t_present_us += xk_os_monotonic_us() - t0;
}
static void xd3d_r_present_inner(unsigned frame, unsigned draws)
{
    { extern void xv_gpu_flush_pending(void); xv_gpu_flush_pending(); }   /* merged dcache cleans for this frame's vertex/index/texture data */
    xd3d_hist_small_check(frame, draws);
    (void)draws;
    extern void xv_present(void);
    uint64_t t0 = t_us();
    if (g_t_last_present) g_t_game_acc += t0 - g_t_last_present;
    if (g_mesh_path) g_mesh_frame = xv_d3d_EndFrame();
    xv_ui_gxm_frame_flip();      /* publish the recorded frame */
    xv_present();                /* fence: block this fiber until the pump has drawn it */
    ui_tex_purge_if_needed(frame);
    uint64_t t1 = t_us();
    g_t_render_acc += t1 - t0; g_t_last_present = t1;
    if (++g_t_frames == 60) {
        g_xv_ovl_game_ms = g_t_game_acc / 60000.0f; g_xv_ovl_render_ms = g_t_render_acc / 60000.0f;
        g_xv_ovl_fps = 60.0e6f / (float)(g_t_game_acc + g_t_render_acc + 1);
        { extern unsigned xv_d3d_draw_acc, xv_d3d_bsp_acc, xv_n_kicks, xv_n_fires; extern uint64_t xv_t_vbcb_us, xv_t_draw_us, xv_t_present_us; UI_LOG("frame time: game %.1f ms + render %.1f ms = %.1f fps | %u textures %u KB | decode %u tex %.1f ms | draws/frame %u bsp %u | frames %u | kicks %u fires %u vbcb %.1f ms draw-hle %.1f ms present %.1f ms (per frame)\n",
               g_t_game_acc / 60000.0, g_t_render_acc / 60000.0, 60.0e6 / (double)(g_t_game_acc + g_t_render_acc + 1), g.texcount, g.dec_off >> 10, g_dec_n, g_dec_us / 1000.0, xv_d3d_draw_acc / (g_t_frames ? g_t_frames : 1), xv_d3d_bsp_acc / (g_t_frames ? g_t_frames : 1), g_t_frames, xv_n_kicks / (g_t_frames ? g_t_frames : 1), xv_n_fires / (g_t_frames ? g_t_frames : 1), xv_t_vbcb_us / 1000.0 / (g_t_frames ? g_t_frames : 1), xv_t_draw_us / 1000.0 / (g_t_frames ? g_t_frames : 1), xv_t_present_us / 1000.0 / (g_t_frames ? g_t_frames : 1)); xv_d3d_draw_acc = xv_d3d_bsp_acc = 0; xv_n_kicks = xv_n_fires = 0; xv_t_vbcb_us = xv_t_draw_us = xv_t_present_us = 0; } g_dec_n = 0; g_dec_us = 0;
        g_t_frames = 0; g_t_game_acc = g_t_render_acc = 0;
    }
}
