/*
 * xv_shader.c - GXM shader patcher + recompiled-shader binding (see xv_shader.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/gxm.h>

#include "xv_shader.h"

#include "xv_log.h"
#define XV_LOG(...)  xv_logf("[xv/shader] " __VA_ARGS__)

/* Patcher memory: modest fixed pools; 67 vertex + a few fragment programs fit. */
#define PATCHER_BUFFER_SIZE        (512 * 1024)
#define PATCHER_VERTEX_USSE_SIZE   (256 * 1024)
#define PATCHER_FRAGMENT_USSE_SIZE (256 * 1024)
#define CONST_STREAM_SIZE          (4 * 1024)     /* one float4 lives here (256 KB CDRAM is overkill) */

typedef struct { SceUID uid; void *base; SceSize size; unsigned int usse_offset; } blk_t;

static SceGxmShaderPatcher *g_patcher;
static blk_t g_buffer, g_vusse, g_fusse, g_const;
static float *g_const_attr;                       /* GPU-visible float4 for constant-fed attributes */

/* ------------------------------------------------------------------------- */

static void *patcher_host_alloc(void *ud, unsigned int size) { (void)ud; return malloc(size); }
static void  patcher_host_free(void *ud, void *mem)          { (void)ud; free(mem); }

static int alloc_mapped(blk_t *b, SceSize size, unsigned attribs, const char *name)
{
    size = (size + 0xFFF) & ~0xFFFu;
    b->uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (b->uid < 0)
        return b->uid;
    sceKernelGetMemBlockBase(b->uid, &b->base);
    b->size = size;
    int err = sceGxmMapMemory(b->base, size, attribs);
    if (err < 0)
        return err;
    return 0;
}

static int alloc_usse(blk_t *b, SceSize size, int vertex, const char *name)
{
    size = (size + 0xFFF) & ~0xFFFu;
    b->uid = sceKernelAllocMemBlock(name, SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (b->uid < 0)
        return b->uid;
    sceKernelGetMemBlockBase(b->uid, &b->base);
    b->size = size;
    return vertex ? sceGxmMapVertexUsseMemory(b->base, size, &b->usse_offset)
                  : sceGxmMapFragmentUsseMemory(b->base, size, &b->usse_offset);
}

static void free_blk(blk_t *b, int kind /* 0 mapped, 1 vusse, 2 fusse */)
{
    if (!b->base)
        return;
    if (kind == 0) sceGxmUnmapMemory(b->base);
    else if (kind == 1) sceGxmUnmapVertexUsseMemory(b->base);
    else sceGxmUnmapFragmentUsseMemory(b->base);
    sceKernelFreeMemBlock(b->uid);
    b->base = NULL;
}

/* ------------------------------------------------------------------------- */

int xv_shader_init(void)
{
    int err;
    if ((err = alloc_mapped(&g_buffer, PATCHER_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ, "xv_patcher_buf")) < 0 ||
        (err = alloc_usse(&g_vusse, PATCHER_VERTEX_USSE_SIZE, 1, "xv_patcher_vusse")) < 0 ||
        (err = alloc_usse(&g_fusse, PATCHER_FRAGMENT_USSE_SIZE, 0, "xv_patcher_fusse")) < 0 ||
        (err = alloc_mapped(&g_const, CONST_STREAM_SIZE, SCE_GXM_MEMORY_ATTRIB_READ, "xv_const_stream")) < 0) {
        XV_LOG("patcher memory setup failed: 0x%08X\n", err);
        return err;
    }
    g_const_attr = (float *)g_const.base;
    g_const_attr[0] = g_const_attr[1] = g_const_attr[2] = 0.0f;
    g_const_attr[3] = 1.0f;

    SceGxmShaderPatcherParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.userData              = NULL;
    pp.hostAllocCallback     = patcher_host_alloc;
    pp.hostFreeCallback      = patcher_host_free;
    pp.bufferMem             = g_buffer.base;
    pp.bufferMemSize         = g_buffer.size;
    pp.vertexUsseMem         = g_vusse.base;
    pp.vertexUsseMemSize     = g_vusse.size;
    pp.vertexUsseOffset      = g_vusse.usse_offset;
    pp.fragmentUsseMem       = g_fusse.base;
    pp.fragmentUsseMemSize   = g_fusse.size;
    pp.fragmentUsseOffset    = g_fusse.usse_offset;
    err = sceGxmShaderPatcherCreate(&pp, &g_patcher);
    if (err < 0) {
        XV_LOG("sceGxmShaderPatcherCreate failed: 0x%08X\n", err);
        return err;
    }
    XV_LOG("patcher up (buf %u KB, vusse %u KB, fusse %u KB)\n",
           g_buffer.size >> 10, g_vusse.size >> 10, g_fusse.size >> 10);
    return 0;
}

void xv_shader_shutdown(void)
{
    if (g_patcher) {
        sceGxmShaderPatcherDestroy(g_patcher);
        g_patcher = NULL;
    }
    free_blk(&g_const, 0);
    free_blk(&g_fusse, 2);
    free_blk(&g_vusse, 1);
    free_blk(&g_buffer, 0);
}

/* ------------------------------------------------------------------------- */

static SceGxmProgram *load_gxp(const char *path)
{
    /* A shader compiled on the console (tools/shadercomp -> ux0:data/xita/shaders/<name>.gxp) overrides the
     * copy packed in the VPK, so regenerated .cg sources take effect without repacking/reinstalling. */
    SceUID fd = -1;
    { const char *base = strrchr(path, '/'); base = base ? base + 1 : path; char alt[160];
      snprintf(alt, sizeof alt, "ux0:data/xita/shaders/%s", base); fd = sceIoOpen(alt, SCE_O_RDONLY, 0);
      if (fd >= 0) { static unsigned n; if (n++ < 4) XV_LOG("%s: using device-compiled %s\n", path, alt); } }
    if (fd < 0) fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        XV_LOG("cannot open %s (0x%08X)\n", path, fd);
        return NULL;
    }
    SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
    sceIoLseek(fd, 0, SCE_SEEK_SET);
    if (size <= 0 || size > (1 << 20)) {
        sceIoClose(fd);
        return NULL;
    }
    SceGxmProgram *prog = (SceGxmProgram *)malloc((size_t)size);
    if (!prog) {
        sceIoClose(fd);
        return NULL;
    }
    int got = sceIoRead(fd, prog, (SceSize)size);
    sceIoClose(fd);
    if (got != (int)size || sceGxmProgramCheck(prog) != SCE_OK) {
        XV_LOG("%s: bad GXP (read %d of %d, check failed)\n", path, got, (int)size);
        free(prog);
        return NULL;
    }
    return prog;
}

int xv_vshader_load(xv_vshader_t *vs, const xv_vs_desc_t *desc)
{
    memset(vs, 0, sizeof(*vs));
    vs->desc = desc;
    vs->const_stream = 0xFF;
    vs->prog = load_gxp(desc->gxp);
    if (!vs->prog)
        return -1;
    int err = sceGxmShaderPatcherRegisterProgram(g_patcher, vs->prog, &vs->id);
    if (err < 0) {
        XV_LOG("%s: register failed 0x%08X\n", desc->gxp, err);
        return err;
    }

    /* Attribute list: each declared v-register whose AppIn member survived
     * compilation gets a GXM attribute pointing at its stream/offset. */
    SceGxmVertexAttribute attrs[XV_MAX_ATTRS];
    SceGxmVertexStream    streams[XV_MAX_STREAMS + 1];
    unsigned nattr = 0;
    int need_const = 0;
    for (unsigned i = 0; i < desc->nattrs && nattr < XV_MAX_ATTRS; ++i) {
        const xv_attr_desc_t *a = &desc->attrs[i];
        /* Stage 3 declares inputs as members of `struct AppIn IN`; SceShaccCg
         * records those as "IN.<member>".  Try that first, then the bare name. */
        char qualified[64];
        snprintf(qualified, sizeof(qualified), "IN.%s", a->name);
        const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName(vs->prog, qualified);
        if (!p)
            p = sceGxmProgramFindParameterByName(vs->prog, a->name);
        if (!p) {
            XV_LOG("  %s: attribute %s (v%u) not found in program (unused or renamed)\n", desc->gxp, a->name, a->vreg);
            continue;                       /* unused by the program: compiler dropped it */
        }
        attrs[nattr].streamIndex    = (a->stream == XV_CONST_STREAM) ? desc->nstreams : a->stream;
        attrs[nattr].offset         = a->offset;
        attrs[nattr].format         = a->format;
        attrs[nattr].componentCount = a->components;
        attrs[nattr].regIndex       = (uint16_t)sceGxmProgramParameterGetResourceIndex(p);
        if (a->stream == XV_CONST_STREAM)
            need_const = 1;
        nattr++;
    }
    unsigned nstreams = desc->nstreams;
    for (unsigned s = 0; s < nstreams; ++s) {
        streams[s].stride      = desc->stride[s];
        streams[s].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    }
    if (need_const) {
        streams[nstreams].stride      = 16;
        streams[nstreams].indexSource = SCE_GXM_INDEX_SOURCE_INSTANCE_16BIT;   /* instance 0 -> same float4 for all */
        vs->const_stream = (uint8_t)nstreams;
        nstreams++;
    }
    err = sceGxmShaderPatcherCreateVertexProgram(g_patcher, vs->id, attrs, nattr, streams, nstreams, &vs->vprog);
    if (err < 0) {
        XV_LOG("%s: create vertex program failed 0x%08X (%u attrs, %u streams)\n", desc->gxp, err, nattr, nstreams);
        return err;
    }
    vs->nbound = (uint8_t)nattr;
    vs->p_c = sceGxmProgramFindParameterByName(vs->prog, "c");
    XV_LOG("%s: %u/%u attrs bound, %u stream(s)%s, c[%d..%d] uniform %s\n", desc->gxp, nattr, desc->nattrs,
           desc->nstreams, need_const ? " + const" : "", desc->c_base, desc->c_base + desc->c_count - 1,
           vs->p_c ? "found" : "MISSING");
    return 0;
}

void xv_vshader_unload(xv_vshader_t *vs)
{
    if (vs->vprog)
        sceGxmShaderPatcherReleaseVertexProgram(g_patcher, vs->vprog);
    if (vs->prog) {
        sceGxmShaderPatcherUnregisterProgram(g_patcher, vs->id);
        free((void *)vs->prog);
    }
    memset(vs, 0, sizeof(*vs));
}

int xv_fshader_load(xv_fshader_t *fs, const char *gxp_path, const xv_vshader_t *vs, const SceGxmBlendInfo *blend)
{
    memset(fs, 0, sizeof(*fs));
    for (int i = 0; i < 4; ++i)
        fs->tex_index[i] = -1;
    fs->prog = load_gxp(gxp_path);
    if (!fs->prog)
        return -1;
    int err = sceGxmShaderPatcherRegisterProgram(g_patcher, fs->prog, &fs->id);
    if (err < 0) {
        XV_LOG("%s: register failed 0x%08X\n", gxp_path, err);
        return err;
    }
    err = sceGxmShaderPatcherCreateFragmentProgram(g_patcher, fs->id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, blend, vs->prog, &fs->fprog);
    if (err < 0) {
        XV_LOG("%s: create fragment program failed 0x%08X\n", gxp_path, err);
        return err;
    }
    fs->p_psc      = sceGxmProgramFindParameterByName(fs->prog, "psc");
    fs->p_fogcolor = sceGxmProgramFindParameterByName(fs->prog, "xv_fogcolor");
    fs->p_atest    = sceGxmProgramFindParameterByName(fs->prog, "xv_atest");
    static const char *const texnames[4] = { "tex0", "tex1", "tex2", "tex3" };
    for (int i = 0; i < 4; ++i) {
        const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName(fs->prog, texnames[i]);
        if (p)
            fs->tex_index[i] = (int)sceGxmProgramParameterGetResourceIndex(p);
    }
    XV_LOG("%s: linked against %s\n", gxp_path, vs->desc ? vs->desc->gxp : "?");
    return 0;
}

void xv_fshader_unload(xv_fshader_t *fs)
{
    if (fs->fprog)
        sceGxmShaderPatcherReleaseFragmentProgram(g_patcher, fs->fprog);
    if (fs->prog) {
        sceGxmShaderPatcherUnregisterProgram(g_patcher, fs->id);
        free((void *)fs->prog);
    }
    memset(fs, 0, sizeof(*fs));
}

/* ------------------------------------------------------------------------- */

void xv_shader_bind(SceGxmContext *ctx, const xv_vshader_t *vs, const xv_fshader_t *fs)
{
    sceGxmSetVertexProgram(ctx, vs->vprog);
    sceGxmSetFragmentProgram(ctx, fs->fprog);
    /* Shared shaders (xv_texmod/tex0/lm) carry the alpha-test uniform for the mesh path; the UI path binds
     * them here and must disable it, or the discard reads an uninitialised uniform and drops UI pixels. */
    if (fs->p_atest) { void *fub; if (sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fub) == 0) {
        static const float off[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; sceGxmSetUniformDataF(fub, fs->p_atest, 0, 4, off); } }
}

int xv_vshader_begin_constants(SceGxmContext *ctx, const xv_vshader_t *vs, void **ub)
{
    *ub = NULL;
    if (!vs->p_c)
        return 0;                                  /* program has no c[]: nothing to reserve */
    return sceGxmReserveVertexDefaultUniformBuffer(ctx, ub);
}

int xv_vshader_set_constants(void *ub, const xv_vshader_t *vs, int d3d_reg, int count, const float *v)
{
    if (!ub || !vs->p_c)
        return 0;
    int first = d3d_reg - vs->desc->c_base;
    if (first < 0 || first + count > vs->desc->c_count)
        return -1;                                 /* outside this shader's window: ignore */
    return sceGxmSetUniformDataF(ub, vs->p_c, (unsigned)first * 4, (unsigned)count * 4, v);
}

void xv_shader_set_const_attr(const float v[4])
{
    if (g_const_attr)
        memcpy(g_const_attr, v, 16);
}

void xv_vshader_set_streams(SceGxmContext *ctx, const xv_vshader_t *vs, const void *const *streams)
{
    for (unsigned s = 0; s < vs->desc->nstreams; ++s)
        if (streams[s])
            sceGxmSetVertexStream(ctx, s, streams[s]);
    if (vs->const_stream != 0xFF)
        sceGxmSetVertexStream(ctx, vs->const_stream, g_const_attr);
}
