/*
 * xv_shader.h - Xita runtime: GXM shader patcher + recompiled-shader binding
 *
 * A recompiled Xbox vertex shader arrives as a .gxp in app0:shaders/ together with
 * a layout descriptor (xv_vs_desc_t, generated into shaders/xv_layouts.h by
 * gen_layouts.py from the Stage 2b/3 output).  This module registers programs
 * with the shader patcher, builds the SceGxmVertexProgram whose attribute list
 * mirrors the Xbox vertex declaration, and exposes the c[] uniform window so the
 * D3D HLE can implement SetVertexShaderConstant() as a single uniform write.
 */
#pragma once

#include <stdint.h>
#include <psp2/gxm.h>

/* Stream index used in xv_attr_desc_t for attributes no vertex stream supplies.
 * The runtime binds them to a 16-byte, instance-indexed stream, so every vertex
 * reads the same float4 - the Xbox persistent-attribute (SetVertexData4f) model. */
#define XV_CONST_STREAM   255
#define XV_MAX_STREAMS    4
#define XV_MAX_ATTRS      16

typedef struct {
    const char *name;        /* AppIn member name == GXM parameter name          */
    uint8_t     stream;      /* declaration stream, or XV_CONST_STREAM           */
    uint16_t    offset;      /* byte offset inside the stream                    */
    uint8_t     format;      /* SceGxmAttributeFormat                            */
    uint8_t     components;
    uint8_t     vreg;        /* Xbox v-register, for diagnostics                 */
} xv_attr_desc_t;

typedef struct {
    const char           *gxp;                    /* "app0:shaders/halo_vs_00.gxp" */
    uint8_t               nstreams;
    uint16_t              stride[XV_MAX_STREAMS];
    uint8_t               nattrs;
    const xv_attr_desc_t *attrs;
    int16_t               c_base;                 /* D3D index of c[0] (may be negative) */
    uint16_t              c_count;
    uint32_t              func_va;                /* provenance (0 for synthetic) */
    uint32_t              decl_va;
    uint32_t              func_hash;              /* FNV-1a of the Xbox function blob (0 = synthetic) */
    uint32_t              func_size;              /* blob size in bytes (4 + 16 * instructions)      */
} xv_vs_desc_t;

typedef struct {
    const xv_vs_desc_t          *desc;
    const SceGxmProgram         *prog;            /* file image, host memory      */
    SceGxmShaderPatcherId        id;
    SceGxmVertexProgram         *vprog;
    const SceGxmProgramParameter *p_c;            /* uniform float4 c[]           */
    uint8_t                      const_stream;    /* index of the const stream or 0xFF */
    uint8_t                      nbound;          /* attributes actually linked   */
} xv_vshader_t;

typedef struct {
    const SceGxmProgram          *prog;
    SceGxmShaderPatcherId         id;
    SceGxmFragmentProgram        *fprog;
    const SceGxmProgramParameter *p_psc;          /* uniform float4 psc[16]       */
    const SceGxmProgramParameter *p_fogcolor;     /* uniform float4 xv_fogcolor   */
    const SceGxmProgramParameter *p_atest;        /* uniform float4 xv_atest (alpha test ref,func,enable) */
    int                           tex_index[4];   /* resource index of tex0..3 or -1 */
} xv_fshader_t;

/* Lifecycle (sceGxmInitialize must already have run). */
int  xv_shader_init(void);
void xv_shader_shutdown(void);

/* Load + register a vertex shader and build its vertex program from `desc`. */
int  xv_vshader_load(xv_vshader_t *vs, const xv_vs_desc_t *desc);
void xv_vshader_unload(xv_vshader_t *vs);

/* Load + register a fragment shader.  `vs` supplies the vertex program it links
 * against; `blend` may be NULL for opaque. */
int  xv_fshader_load(xv_fshader_t *fs, const char *gxp_path, const xv_vshader_t *vs,
                     const SceGxmBlendInfo *blend);
void xv_fshader_unload(xv_fshader_t *fs);

/* Per-draw helpers. */
void xv_shader_bind(SceGxmContext *ctx, const xv_vshader_t *vs, const xv_fshader_t *fs);
/* Uniform writes for one draw: reserve the vertex default uniform buffer ONCE
 * after xv_shader_bind (GXM keeps exactly one reservation per draw), then write
 * any number of SetVertexShaderConstant(d3d_reg, count float4s) ranges into it. */
int  xv_vshader_begin_constants(SceGxmContext *ctx, const xv_vshader_t *vs, void **ub);
int  xv_vshader_set_constants(void *ub, const xv_vshader_t *vs, int d3d_reg, int count, const float *v);
/* Value seen by every constant-fed attribute (SetVertexData4f). */
void xv_shader_set_const_attr(const float v[4]);
/* Binds vertex streams: `streams[i]` = GPU-visible pointer for declaration stream i;
 * the constant stream (if any) is bound automatically. */
void xv_vshader_set_streams(SceGxmContext *ctx, const xv_vshader_t *vs, const void *const *streams);
