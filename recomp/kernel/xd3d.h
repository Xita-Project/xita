/* xd3d.h - the D3D state the object model (xd3d.c) tracks for the renderer hooks.
 * The host null renderer logs it; the Vita build overrides the hooks and feeds GXM (xv_d3d bridge). */
#pragma once
#include "../xv_x86rt.h"
#include "../../runtime/xv_stencil.h"

typedef struct xd3d_state {
    /* geometry */
    uint32_t vs_handle;                /* odd = shader object | 1 (guest struct: decl, func, size, fnv) ; even = FVF */
    uint32_t vs_program;               /* resident program selected independently of the input declaration */
    uint32_t stream_vb[4];             /* X_D3DVertexBuffer headers (guest) */
    uint32_t stream_stride[4];
    uint32_t indices;                  /* X_D3DIndexBuffer header (guest) or 0 */
    uint32_t index_base;
    /* textures */
    uint32_t texture[4];               /* X_D3DPixelContainer headers (guest) */
    uint32_t fog_color;                /* NV097_SET_FOG_COLOR (D3DRS_FOGCOLOR): fragment fog colour */
    uint32_t const_mode;               /* D3DDevice_SetShaderConstantMode (0x200 96, 0x201 192, 0x202 192+fixed offsets) */
    uint32_t palette[4];               /* SetPalette: guest address of the 256 D3DCOLOR entries (P8 textures), 0 = none */
    /* vertex shader constants (Xbox c[-96..96): stored at [index+96]) */
    float    vsc[192][4];
    uint32_t vsc_dirty_lo, vsc_dirty_hi; /* changed rows [lo, hi); consumed as 192,0 */
    /* pixel shader */
    uint32_t ps_def;                   /* X_D3DPIXELSHADERDEF pointer (runtime-built combiners) */
    uint32_t ps_hash;                  /* FNV of the def minus its constant colours: identifies the combiner program */
    uint32_t ps_key;                   /* canonical program identity, excluding inactive stages */
    uint32_t ps_shadow[0x3C];          /* effective X_D3DPIXELSHADERDEF: SetPixelShaderProgram + the NV2A combiner
                                          registers Halo pokes afterwards through SetRenderState_Simple */
    int      ps_dirty;
    float    psc[18][4];               /* combiner constants: c0[stage 0..7], c1[stage 0..7], final c0, c1 (from the def) */
    /* render states of interest */
    uint32_t cull;                     /* 1 none, 2 cw, 3 ccw */
    uint32_t z_enable, z_write, z_func;
    uint32_t alpha_blend, src_blend, dst_blend, blend_op;
    uint32_t color_mask;               /* NV097_SET_COLOR_MASK: 0x01000000 A, 0x00010000 R, 0x00000100 G, 0x00000001 B */
    uint32_t alpha_test, alpha_func, alpha_ref;
    uint32_t fill_mode;
    /* viewport */
    uint32_t vp_x, vp_y, vp_w, vp_h; float vp_minz, vp_maxz;
    xv_stencil stencil;
    /* frame counters */
    unsigned frame, draws_in_frame;
} xd3d_state_t;

extern xd3d_state_t xd3d_state;
void xd3d_ps_sync(void);              /* recompute ps_hash/psc from ps_shadow if a poke changed it (call before a draw) */
extern uint32_t g_xd3d_device;

/* renderer hooks (weak null versions in xd3d.c; the Vita/GXM bridge overrides them) */
void xd3d_r_present(unsigned frame, unsigned draws);
void xd3d_r_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil);
void xd3d_r_draw(xctx *c, int indexed, uint32_t prim, uint32_t count, uint32_t data);   /* data: index ptr (indexed) or start vertex */
void xd3d_r_state(const char *what, uint32_t a, uint32_t b, uint32_t v);
typedef struct { float a[16][4]; } xd3d_im_vtx;                       /* one immediate-mode vertex: all 16 attribute registers */
void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n); /* Begin/SetVertexData/End draw */
int xd3d_im_passthrough(void); /* current Begin/End uses SetVertexData4f(-1) */
const float (*xd3d_current_attributes(void))[4]; /* persistent NV2A vertex register values */
int xd3d_hist_active(void);
uint32_t xd3d_texture_state(unsigned stage, unsigned state); /* XDK 3925 indices */
void xd3d_texture_states(uint32_t out[4][5]); /* U/V, border ARGB, mag/min */
uint32_t xd3d_backbuffer_data(void);

unsigned xd3d_frame(void);
unsigned xd3d_pad_frame(void);   /* pad record/replay index, anchored at gameplay start */
