/*
 * xv_d3d.h - Xita runtime: Direct3D 8 (Xbox flavour) HLE state machine
 *
 * The recompiled game calls these entry points on the guest fiber.  They never
 * touch GXM directly: they RECORD into a per-frame command list (streams, index
 * pointer, vertex-shader handle, a snapshot of the c[] window it reads, texture
 * control words, render state).  The pump thread (core 1) replays the list with
 * libgxm inside its scene.  Blueprint sections 1.4, 1.5 and 5.
 *
 * Everything the GPU consumes stays in guest RAM: vertex/index pointers are the
 * game's own (D3DResource.Data), textures are 16-byte SceGxmTexture control words
 * pointing at the game's texels (NV2A swizzle == PowerVR twiddle).
 */
#pragma once
#ifdef XV_DEPTH_STORE
/* Mode changes only at the drained recording-owner/pump boundary; default OFF.
 * Reporting is pump-owned. First/offscreen stores retain their policy. */
int xv_depth_store_available(void);
int xv_depth_store_enabled(void);
void xv_depth_store_override(int enabled);
void xv_d3d_depth_store_report(void);
#endif

#include <stdint.h>
#include <psp2/gxm.h>
#include "xv_shader.h"
#include "xv_stencil.h"

/* Call once after configuration handoff, before recorder/pump threads start. */
void xv_d3d_configure_render_preparation(void);

/* Internal comparison control: -1 restores the configured default. */
void xv_depth_prepare_override(int enabled);
int xv_depth_prepare_available(void);

/* --- Xbox D3D structures / enums as the game sees them --------------------------- */

typedef struct { uint32_t Common, Data, Lock; } X_D3DResource;
typedef struct { uint32_t Common, Data, Lock, Format, Size; } X_D3DPixelContainer;   /* textures, surfaces */

enum { X_D3DPT_POINTLIST = 1, X_D3DPT_LINELIST = 2, X_D3DPT_LINELOOP = 3, X_D3DPT_LINESTRIP = 4,
       X_D3DPT_TRIANGLELIST = 5, X_D3DPT_TRIANGLESTRIP = 6, X_D3DPT_TRIANGLEFAN = 7,
       X_D3DPT_QUADLIST = 8, X_D3DPT_QUADSTRIP = 9, X_D3DPT_POLYGON = 10 };

enum { X_D3DCULL_NONE = 1, X_D3DCULL_CW = 2, X_D3DCULL_CCW = 3 };
enum { X_D3DCMP_NEVER = 1, X_D3DCMP_LESS, X_D3DCMP_EQUAL, X_D3DCMP_LESSEQUAL, X_D3DCMP_GREATER,
       X_D3DCMP_NOTEQUAL, X_D3DCMP_GREATEREQUAL, X_D3DCMP_ALWAYS };
enum { X_D3DBLEND_ZERO = 1, X_D3DBLEND_ONE, X_D3DBLEND_SRCCOLOR, X_D3DBLEND_INVSRCCOLOR, X_D3DBLEND_SRCALPHA,
       X_D3DBLEND_INVSRCALPHA, X_D3DBLEND_DESTALPHA, X_D3DBLEND_INVDESTALPHA, X_D3DBLEND_DESTCOLOR,
       X_D3DBLEND_INVDESTCOLOR, X_D3DBLEND_SRCALPHASAT };

/* Texture stage states (XDK 4xxx numbering) and their values. */
enum { X_D3DTSS_ADDRESSU = 0, X_D3DTSS_ADDRESSV = 1, X_D3DTSS_ADDRESSW = 2, X_D3DTSS_MAGFILTER = 3,
       X_D3DTSS_MINFILTER = 4, X_D3DTSS_MIPFILTER = 5, X_D3DTSS_BORDERCOLOR = 6 };
enum { X_D3DTEXF_NONE = 0, X_D3DTEXF_POINT = 1, X_D3DTEXF_LINEAR = 2, X_D3DTEXF_ANISOTROPIC = 3 };
enum { X_D3DTADDRESS_WRAP = 1, X_D3DTADDRESS_MIRROR = 2, X_D3DTADDRESS_CLAMP = 3, X_D3DTADDRESS_BORDER = 4,
       X_D3DTADDRESS_CLAMPTOEDGE = 5 };

#define X_D3DCLEAR_ZBUFFER   0x00000001u
#define X_D3DCLEAR_STENCIL   0x00000002u
#define X_D3DCLEAR_TARGET    0x000000F0u        /* R|G|B|A write masks */

/* X_D3DPixelContainer.Format field */
#define X_FMT_FORMAT(f)   (((f) >> 8)  & 0xFF)
#define X_FMT_MIPS(f)     (((f) >> 16) & 0x0F)
#define X_FMT_USIZE(f)    (((f) >> 20) & 0x0F)    /* log2 width  (swizzled) */
#define X_FMT_VSIZE(f)    (((f) >> 24) & 0x0F)    /* log2 height (swizzled) */
#define X_FMT_CUBEMAP     0x00000004u

enum { X_D3DFMT_L8 = 0x00, X_D3DFMT_A1R5G5B5 = 0x02, X_D3DFMT_A4R4G4B4 = 0x04, X_D3DFMT_R5G6B5 = 0x05,
       X_D3DFMT_A8R8G8B8 = 0x06, X_D3DFMT_X8R8G8B8 = 0x07, X_D3DFMT_P8 = 0x0B, X_D3DFMT_DXT1 = 0x0C,
       X_D3DFMT_DXT3 = 0x0E, X_D3DFMT_DXT5 = 0x0F, X_D3DFMT_LIN_R5G6B5 = 0x11, X_D3DFMT_LIN_A8R8G8B8 = 0x12,
       X_D3DFMT_A8L8 = 0x1A, X_D3DFMT_LIN_X8R8G8B8 = 0x1E };

/* --- host services the HLE relies on (provided by main.c) ------------------------- */
void *xv_guest_ptr(uint32_t guest_addr);                 /* guest -> host/GPU pointer  */
void  xv_gpu_flush(const void *ptr, uint32_t len);       /* register uncached GPU writes for publication */
void  xv_present(void);                                  /* D3DDevice_Swap fence         */

/* --- lifecycle ----------------------------------------------------------------- */
int  xv_d3d_init(const xv_vs_desc_t *const *table, unsigned count);    /* after xv_shader_init */
void xv_d3d_shutdown(void);

/* --- HLE entry points (guest fiber) --------------------------------------------- */
uint32_t xv_d3d_CreateVertexShader(uint32_t decl_guest, uint32_t func_guest);  /* 0 on failure */
uint32_t xv_d3d_RegisterVertexShader(const xv_vs_desc_t *desc);                /* runtime-owned programs */
void xv_d3d_SetVertexShader(uint32_t handle);
void xv_d3d_SetVertexShaderConstant(int reg, const float *data, unsigned count);  /* reg: D3D numbering, -96..95 */
void xv_d3d_SetVertexData4f(unsigned vreg, float x, float y, float z, float w);   /* persistent attribute */
void xv_d3d_SetAllAttributes(const float (*attributes)[4]);
void xv_d3d_SetStreamSource(unsigned stream, uint32_t vb_guest, unsigned stride); /* vb_guest -> X_D3DResource */
void xv_d3d_SetTexture(unsigned stage, uint32_t tex_guest);                      /* 0 unbinds */
void xv_d3d_SetTexturePalette(unsigned stage, uint32_t pal_guest);   /* guest address of 256 D3DCOLOR entries, 0 = none */
void xv_d3d_SetTextureStageState(unsigned stage, unsigned type, uint32_t value);
void xv_d3d_SetStencil(const xv_stencil *state);
void xv_d3d_SetRenderState_ZEnable(uint32_t v);
void xv_d3d_SetRenderState_ZWriteEnable(uint32_t v);
void xv_d3d_SetRenderState_ZFunc(uint32_t v);
void xv_d3d_SetRenderState_CullMode(uint32_t v);
void xv_d3d_SetRenderState_AlphaBlendEnable(uint32_t v);
void xv_d3d_SetRenderState_ColorWriteEnable(uint32_t rgba_bits);   /* D3DRS_COLORWRITEENABLE: R 1, G 2, B 4, A 8 */
void xv_d3d_SetRenderState_SrcBlend(uint32_t v);
void xv_d3d_SetRenderState_DestBlend(uint32_t v);
void xv_d3d_SetRenderState_BlendOp(uint32_t nv2a_op);
void xv_d3d_Clear(uint32_t flags, uint32_t color_argb, float z, uint32_t stencil);
void xv_d3d_NoteOffscreenTarget(uint32_t data);
void xv_d3d_SetPixelShader(uint32_t hash, uint32_t key, const float (*psc)[4]); /* raw/canonical ids + 18 constants; key 0 selects a semantic HUD id */
void xv_d3d_DrawVertices(uint32_t prim, uint32_t start_vertex, uint32_t vertex_count);
/* Copy complete immediate attribute registers into a bounded GPU frame ring. */
void xv_d3d_DrawImmediate(uint32_t prim, const void *vertices, uint32_t count);
/* Packed immediate vertices using the active shader's single-stream stride. */
void xv_d3d_DrawImmediateStrided(uint32_t prim, const void *vertices, uint32_t count, uint32_t stride);
void xv_d3d_DrawIndexedVertices(uint32_t prim, uint32_t vertex_count, uint32_t indices_guest);
void xv_d3d_Swap(void);
/* recomp-build entry points (state pushed per draw by the kernel's D3D object model) */
void     xv_d3d_DrawIndexedVerticesBase(uint32_t prim, uint32_t index_count, uint32_t indices_guest, uint32_t base_vertex);
uint32_t xv_d3d_handle_for_hash(uint32_t fnv);            /* registers the program on first use; 0 = unknown */
void     xv_d3d_SetAllConstants(const float (*vsc)[4]);   /* c[-96..95] */
/* Persistent producer with all writes recorded in [dirty_lo, dirty_hi).
 * Consumes that range; UI/generic writes automatically force a full resync. */
void     xv_d3d_SetTrackedConstants(const float (*vsc)[4], uint32_t *dirty_lo, uint32_t *dirty_hi);
/* Exact CPU scan comparison; -1 restores the configured opt-in default. */
void     xv_d3d_draw_scan_override(int enabled);
/* Set only at a drained frame boundary; -1 restores the configured default. */
void     xv_d3d_texture_state_override(int enabled);
void xv_d3d_index_reuse_override(int enabled);
uint32_t xv_d3d_EndFrame(void);                           /* close the recorded list; returns its frame number */

/* Handle (from xv_d3d_RegisterVertexShader) of the clear-quad program xv_clear.gxp. */
void xv_d3d_set_clear_shader(uint32_t handle);

/* --- render targets: record on guest thread, replay on pump -------------------- */
int xv_d3d_record_ui(unsigned frame, unsigned batch);
const SceGxmTexture *xv_d3d_render_target_texture(uint32_t hdr);
void xv_d3d_ReleaseRenderTarget(uint32_t data);
void xv_d3d_SetRenderTarget(uint32_t surface_hdr, int is_backbuffer);
int xv_d3d_uses_previous_frame(uint32_t frame);
void xv_d3d_SetPreviousFrameTexture(const SceGxmTexture *texture);
/* Only sampled from an offscreen pass, after the backbuffer scene has finished. */
void xv_d3d_SetSceneBackbufferTexture(const SceGxmTexture *texture);
void xv_d3d_check_geometry(uint32_t frame); /* trace-only lifetime diagnostics, after GPU completion */
void xv_d3d_visibility_prepare(SceGxmContext *ctx, uint32_t frame, unsigned w, unsigned h); /* before the first scene */
int xv_d3d_has_visibility(uint32_t frame);
#ifdef XV_QUERY_BOUNDARY
/* Pump-only; sealed packet, no storage release or GPU finish. */
int xv_d3d_query_boundary_prepare(uint32_t frame,int enabled);
void xv_d3d_query_boundary_arm(uint32_t frame,const SceGxmNotification *fence);
void xv_d3d_query_boundary_report(void);
#endif
void xv_d3d_frag_census_complete(uint32_t frame); /* XV_FRAG_CENSUS: after final completion */
void xv_d3d_visibility_complete(uint32_t frame); /* after query fragments complete; storage stays owned until final completion */

/* Legacy replay runs inside the caller's scene. RTT replay starts outside a
 * scene and leaves the final backbuffer scene open for the caller to end/flip. */
void xv_d3d_render(SceGxmContext *ctx, uint32_t frame);
int xv_d3d_has_render_targets(uint32_t frame);
int xv_d3d_render_targets(SceGxmContext *ctx, uint32_t frame,
    SceGxmRenderTarget *back, SceGxmSyncObject *sync,
    const SceGxmColorSurface *color, const SceGxmDepthStencilSurface *depth,
    unsigned back_width, unsigned back_height, int depth_tail_readonly);

unsigned xv_d3d_record_slot(void);
void xv_d3d_BeginFrame(void);

/* Recording-owner state import; copies inputs immediately, no retained pointers.
 * Texture columns: address U/V, border ARGB, mag filter, min filter. */
struct xd3d_state;
void xv_d3d_SyncDrawState(const struct xd3d_state *state,
                        const float (*attributes)[4],const uint32_t texture_state[4][5]);
