/*
 * xv_d3d.h - XboxVita runtime: Direct3D 8 (Xbox flavour) HLE state machine
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

#include <stdint.h>
#include <psp2/gxm.h>
#include "xv_shader.h"

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
       X_D3DTSS_MINFILTER = 4, X_D3DTSS_MIPFILTER = 5 };
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
void  xv_gpu_flush(const void *ptr, uint32_t len);       /* dcache clean before GPU read */
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
void xv_d3d_SetStreamSource(unsigned stream, uint32_t vb_guest, unsigned stride); /* vb_guest -> X_D3DResource */
void xv_d3d_SetTexture(unsigned stage, uint32_t tex_guest);                      /* 0 unbinds */
void xv_d3d_SetTextureStageState(unsigned stage, unsigned type, uint32_t value);
void xv_d3d_SetRenderState_ZEnable(uint32_t v);
void xv_d3d_SetRenderState_ZWriteEnable(uint32_t v);
void xv_d3d_SetRenderState_ZFunc(uint32_t v);
void xv_d3d_SetRenderState_CullMode(uint32_t v);
void xv_d3d_SetRenderState_AlphaBlendEnable(uint32_t v);
void xv_d3d_SetRenderState_SrcBlend(uint32_t v);
void xv_d3d_SetRenderState_DestBlend(uint32_t v);
void xv_d3d_Clear(uint32_t flags, uint32_t color_argb, float z, uint32_t stencil);
void xv_d3d_NoteOffscreenTarget(uint32_t data);
void xv_d3d_SetPixelShader(uint32_t hash, const float (*psc)[4]);   /* combiner program id + its 16 constants */           /* guest Data of a render-target surface: draws sampling it are skipped */
void xv_d3d_DrawVertices(uint32_t prim, uint32_t start_vertex, uint32_t vertex_count);
void xv_d3d_DrawIndexedVertices(uint32_t prim, uint32_t vertex_count, uint32_t indices_guest);
void xv_d3d_Swap(void);
/* recomp-build entry points (state pushed per draw by the kernel's D3D object model) */
void     xv_d3d_DrawIndexedVerticesBase(uint32_t prim, uint32_t index_count, uint32_t indices_guest, uint32_t base_vertex);
uint32_t xv_d3d_handle_for_hash(uint32_t fnv);            /* registers the program on first use; 0 = unknown */
void     xv_d3d_SetAllConstants(const float (*vsc)[4]);   /* c[-96..95] */
uint32_t xv_d3d_EndFrame(void);                           /* close the recorded list; returns its frame number */

/* Handle (from xv_d3d_RegisterVertexShader) of the clear-quad program xv_clear.gxp. */
void xv_d3d_set_clear_shader(uint32_t handle);

/* --- pump side (inside sceGxmBeginScene/EndScene) -------------------------------- */
void xv_d3d_render(SceGxmContext *ctx, uint32_t frame);
void xv_d3d_render_offscreen(SceGxmContext *ctx, uint32_t frame);   /* render-to-texture passes: call BEFORE the main BeginScene */
void xv_d3d_SetRenderTarget(uint32_t surface_hdr, int is_backbuffer);
