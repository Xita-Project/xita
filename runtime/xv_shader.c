/*
 * xv_shader.c - GXM shader patcher + recompiled-shader binding (see xv_shader.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/gxm.h>

#include "xv_shader.h"
#include "xv_render_profile.h"
#include "shaders/xv_hud_gxp.h"
#include "shaders/xv_ps_gxp.h"
#include "shaders/xv_vs_gxp.h"

#include "xv_log.h"
#define XV_LOG(...)  xv_logf("[xv/shader] " __VA_ARGS__)

/* Patcher memory: modest fixed pools; 67 vertex + a few fragment programs fit. */
#define PATCHER_BUFFER_SIZE        (512 * 1024)
#define PATCHER_VERTEX_USSE_SIZE   (256 * 1024)
#define PATCHER_FRAGMENT_USSE_SIZE (256 * 1024)
#define CONST_STREAM_SIZE          (4 * 1024)     /* default register file; draws supply frame-owned values */

typedef struct { SceUID uid; void *base; SceSize size; unsigned int usse_offset; } blk_t;

static SceGxmShaderPatcher *g_patcher;
static blk_t g_buffer, g_vusse, g_fusse, g_const;
static float *g_const_attr;                       /* immutable GPU defaults for all 16 registers */

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
    memset(g_const_attr, 0, 16 * 16);
    for (unsigned i = 0; i < 16; i++) g_const_attr[i * 4 + 3] = 1.0f;

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

unsigned xv_fshader_embedded_texture_mask(const char *path)
{
    static int override = -1;
    if (override < 0) { const char *e = getenv("XV_SHADER_OVERRIDE"); override = e && atoi(e) != 0; }
    if (override || !path) return 15;
    unsigned lo = 0, hi = sizeof xv_ps_embedded / sizeof xv_ps_embedded[0];
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        int cmp = strcmp(path, xv_ps_embedded[mid].path);
        if (cmp < 0) hi = mid;
        else if (cmp > 0) lo = mid + 1;
        else {
            const SceGxmProgram *prog = (const SceGxmProgram *)xv_ps_embedded[mid].data;
            if (sceGxmProgramCheck(prog) != SCE_OK) return 15;
            static const char *const names[4] = { "tex0", "tex1", "tex2", "tex3" };
            unsigned mask = 0;
            for (unsigned t = 0; t < 4; t++)
                if (sceGxmProgramFindParameterByName(prog, names[t])) mask |= 1u << t;
            return mask;
        }
    }
    return 15;
}

#ifdef XV_DEPTH_STORE
int xv_fshader_embedded_no_depth(const char *path)
{
    if(!path)return 0;
    const char *override=getenv("XV_SHADER_OVERRIDE");
    if(override && atoi(override))return 0;
    unsigned lo=0,hi=sizeof xv_ps_embedded/sizeof xv_ps_embedded[0];
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        int cmp=strcmp(path,xv_ps_embedded[mid].path);
        if(cmp<0)hi=mid;
        else if(cmp>0)lo=mid+1;
        else {
            const SceGxmProgram *p=(const SceGxmProgram *)xv_ps_embedded[mid].data;
            return sceGxmProgramCheck(p)==SCE_OK &&
                sceGxmProgramGetType(p)==SCE_GXM_FRAGMENT_PROGRAM &&
                !sceGxmProgramIsDepthReplaceUsed(p);
        }
    }
    return 0;
}
#endif

static SceGxmProgram *load_gxp(const char *path, int *source)
{
    /* Source: 0 unknown/failure, 1 embedded, 2 file. Caller-local so concurrent
     * first-use loads cannot overwrite another load's diagnostic provenance. */
    if (source) *source = 0;
    /* Optimization correctness relies on this exact constant program, even
     * when development overrides are enabled for ordinary game shaders. */
    int builtin_only = !strcmp(path, "builtin:xita-depth");
    if (builtin_only) path = "app0:shaders/ps_28CF808C_07_na.frag.gxp";
    /* Packaged shaders match this runtime's layouts. Old device-compiled files can outlive an
     * upgrade, so overriding the package is an explicit development option. */
    static int override = -1;
    if (override < 0) {
        const char *e = getenv("XV_SHADER_OVERRIDE"); override = e ? atoi(e) != 0 : 0;
        XV_LOG("shader source preference: %s\n", override ? "device override" : "packaged");
    }
    /* These new HUD programs travel inside the executable as well as the VPK.
     * An in-place USB executable update therefore has every required shader. */
    const void *builtin = NULL; unsigned builtin_size = 0;
    for (unsigned i = 0; i < sizeof xv_hud_gxp / sizeof xv_hud_gxp[0]; i++)
        if (!strcmp(path, xv_hud_gxp[i].path)) { builtin = xv_hud_gxp[i].data; builtin_size = xv_hud_gxp[i].size; break; }
    /* Vertex programs must travel with their matching fragments and layouts.
     * This also updates fog varyings during an executable-only USB install. */
    for (unsigned i = 0; i < sizeof xv_vs_embedded / sizeof xv_vs_embedded[0]; i++)
        if (!strcmp(path, xv_vs_embedded[i].path)) {
            builtin = xv_vs_embedded[i].data; builtin_size = xv_vs_embedded[i].size; break;
        }
    /* Every table-referenced fragment travels with this executable. Keep the
     * existing device-override option for shader development. */
    unsigned lo = 0, hi = sizeof xv_ps_embedded / sizeof xv_ps_embedded[0];
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        int cmp = strcmp(path, xv_ps_embedded[mid].path);
        if (cmp < 0) hi = mid;
        else if (cmp > 0) lo = mid + 1;
        else { builtin = xv_ps_embedded[mid].data; builtin_size = xv_ps_embedded[mid].size; break; }
    }
    if (builtin_only && !builtin) return NULL;
    int use_override = override && !builtin_only;
    SceUID fd = (use_override || builtin) ? -1 : sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0 && (!builtin || use_override))
    { const char *base = strrchr(path, '/'); base = base ? base + 1 : path; char alt[160];
      snprintf(alt, sizeof alt, "ux0:data/xita/shaders/%s", base); fd = sceIoOpen(alt, SCE_O_RDONLY, 0);
      if (fd >= 0) { static unsigned n; if (n++ < 4) XV_LOG("%s: using device-compiled %s\n", path, alt); } }
    if (fd < 0 && builtin) {
        SceGxmProgram *prog = malloc(builtin_size);
        if (!prog) return NULL;
        memcpy(prog, builtin, builtin_size);
        if (sceGxmProgramCheck(prog) != SCE_OK) { free(prog); return NULL; }
        if (source) *source = 1;
        return prog;
    }
    if (fd < 0 && use_override) fd = sceIoOpen(path, SCE_O_RDONLY, 0);
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
    if (source) *source = 2;
    return prog;
}

#if XV_PACKED_VERTEX_LAYOUT
/* Only these three declarations and their actual embedded linked programs.
 * Compare fields, not padded structs; bindings must be exactly the qualified
 * live inputs. File overrides differing by even one byte decline. */
static int packed_layout(const xv_vshader_t *vs,const SceGxmVertexAttribute *a,unsigned n)
{
    const xv_vs_desc_t *d=vs->desc;
    static const char *const names[]={"position","blendweight","normal","color0","color1","backcolor0","backcolor1"};
    static const unsigned formats[]={SCE_GXM_ATTRIBUTE_FORMAT_F32,SCE_GXM_ATTRIBUTE_FORMAT_U8,
        SCE_GXM_ATTRIBUTE_FORMAT_U8,SCE_GXM_ATTRIBUTE_FORMAT_U8,SCE_GXM_ATTRIBUTE_FORMAT_F32,
        SCE_GXM_ATTRIBUTE_FORMAT_U8,SCE_GXM_ATTRIBUTE_FORMAT_S16N};
    static const unsigned offsets[]={0,12,16,20,24,0,4},components[]={3,4,4,4,2,4,2};
    unsigned kind;
    if(d->func_hash==0x3068A44Bu && d->func_size==148 && d->c_count==11 &&
        !strcmp(d->gxp,"app0:shaders/halo_vs_06.gxp"))kind=0;
    else if(d->func_hash==0xF0F51170u && d->func_size==180 && d->c_count==20 &&
        !strcmp(d->gxp,"app0:shaders/halo_vs_29.gxp"))kind=1;
    else if(d->func_hash==0x53C00F6Cu && d->func_size==116 && d->c_count==4 &&
        !strcmp(d->gxp,"app0:shaders/halo_vs_58.gxp"))kind=2;
    else return 0;
    if(d->c_base!=-96 || d->nstreams!=(kind==2?2:1) || d->nattrs!=(kind==2?7:5) ||
       d->stride[0]!=32 || d->stride[1]!=(kind==2?8:0) || d->stride[2] || d->stride[3])return 0;
    for(unsigned i=0;i<d->nattrs;i++) {
        const xv_attr_desc_t *v=&d->attrs[i];
        if(strcmp(v->name,names[i]) || v->stream!=(i>=5) || v->offset!=offsets[i] ||
           v->format!=formats[i] || v->components!=components[i] || v->vreg!=(i<5?i:i+2))return 0;
    }
    if(n!=(kind?2:1))return 0;
    for(unsigned i=0;i<n;i++) {
        unsigned k=i?(kind==1?1:6):0;
        if(a[i].streamIndex!=(k>=5) || a[i].offset!=offsets[k] ||
           a[i].format!=formats[k] || a[i].componentCount!=components[k])return 0;
    }
    for(unsigned i=0;i<sizeof xv_vs_embedded/sizeof *xv_vs_embedded;i++)
        if(!strcmp(d->gxp,xv_vs_embedded[i].path)) {
            unsigned bytes=sceGxmProgramGetSize(vs->prog);
            /* GXP files may have 0..3 trailing alignment bytes outside the
             * program. Compare the complete validated program, not padding. */
            return bytes>=0x30 && bytes<=xv_vs_embedded[i].size &&
                bytes==sceGxmProgramGetSize((const SceGxmProgram *)xv_vs_embedded[i].data) &&
                !memcmp(vs->prog,xv_vs_embedded[i].data,bytes);
        }
    return 0;
}
#endif
int xv_vshader_load(xv_vshader_t *vs, const xv_vs_desc_t *desc)
{
    memset(vs, 0, sizeof(*vs));
    vs->desc = desc;
    vs->const_stream = 0xFF;
    vs->prog = load_gxp(desc->gxp, NULL);
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
        attrs[nattr].offset         = a->stream == XV_CONST_STREAM ? a->vreg * 16 : a->offset;
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
        streams[nstreams].stride      = 16 * 16;
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
#if XV_PACKED_VERTEX_LAYOUT
    if(packed_layout(vs,attrs,nattr)) {
        streams[0].stride=16;
        SceGxmVertexProgram *packed=NULL;
        int result=sceGxmShaderPatcherCreateVertexProgram(g_patcher,vs->id,attrs,nattr,streams,nstreams,&packed);
        if(result>=0)vs->packed_vprog=packed;
        XV_LOG("%s: packed-prefix16 %s (original retained)\n",desc->gxp,vs->packed_vprog?"ready":"unavailable");
    }
#endif
    vs->p_c = sceGxmProgramFindParameterByName(vs->prog, "c");
    XV_LOG("%s: %u/%u attrs bound, %u stream(s)%s, c[%d..%d] uniform %s\n", desc->gxp, nattr, desc->nattrs,
           desc->nstreams, need_const ? " + const" : "", desc->c_base, desc->c_base + desc->c_count - 1,
           vs->p_c ? "found" : "MISSING");
    return 0;
}

void xv_vshader_unload(xv_vshader_t *vs)
{
#if XV_PACKED_VERTEX_LAYOUT
    if(vs->packed_vprog)
        sceGxmShaderPatcherReleaseVertexProgram(g_patcher,vs->packed_vprog);
#endif
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
    /* Cold-path attribution only. No clocks on cache-hit draws. Reuse the
     * existing diagnostic switch to avoid consuming another launch-env slot.
     * Wall time includes preemption; this is not GPU service time. */
    const char *slow = getenv("XV_FRAME_SLOW_MS");
    int slow_ms = slow ? atoi(slow) : 0;
    int timed = slow_ms >= 50 && slow_ms <= 10000;
    uint64_t t0 = timed ? sceKernelGetProcessTimeWide() : 0;
    int source = 0;
    fs->prog = load_gxp(gxp_path, &source);
    uint64_t t1 = timed ? sceKernelGetProcessTimeWide() : 0;
    if (!fs->prog)
        return -1;
    int err = sceGxmShaderPatcherRegisterProgram(g_patcher, fs->prog, &fs->id);
    uint64_t t2 = timed ? sceKernelGetProcessTimeWide() : 0;
    if (err < 0) {
        XV_LOG("%s: register failed 0x%08X\n", gxp_path, err);
        free((void *)fs->prog); fs->prog = NULL;
        return err;
    }
    err = sceGxmShaderPatcherCreateFragmentProgram(g_patcher, fs->id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
                                                   SCE_GXM_MULTISAMPLE_NONE, blend, vs->prog, &fs->fprog);
    uint64_t t3 = timed ? sceKernelGetProcessTimeWide() : 0;
    if (err < 0) {
        XV_LOG("%s: create fragment program failed 0x%08X\n", gxp_path, err);
        sceGxmShaderPatcherUnregisterProgram(g_patcher, fs->id);
        free((void *)fs->prog); fs->prog = NULL;
        return err;
    }
    fs->p_psc      = sceGxmProgramFindParameterByName(fs->prog, "psc");
    fs->uses_discard = sceGxmProgramIsDiscardUsed(fs->prog) != 0;
    fs->replaces_depth = sceGxmProgramIsDepthReplaceUsed(fs->prog) != 0;
    fs->p_fogcolor = sceGxmProgramFindParameterByName(fs->prog, "xv_fogcolor");
    fs->p_atest    = sceGxmProgramFindParameterByName(fs->prog, "xv_atest");
    fs->p_texscale = sceGxmProgramFindParameterByName(fs->prog, "xv_texscale");
    fs->p_border1 = sceGxmProgramFindParameterByName(fs->prog, "xv_border1");
    /* Keep cold-load diagnostics in one record. Ordinary owner-thread logs can
     * take the immediate mutex/file path; logging here both stalls setup and
     * contaminates the metadata timer. Preserve error logs at their failures. */
    unsigned psc_array = 0, psc_components = 0;
    if (fs->p_psc && strstr(gxp_path, "_1D.frag.gxp")) {
        psc_array = sceGxmProgramParameterGetArraySize(fs->p_psc);
        psc_components = sceGxmProgramParameterGetComponentCount(fs->p_psc);
    }
    static const char *const texnames[4] = { "tex0", "tex1", "tex2", "tex3" };
    for (int i = 0; i < 4; ++i) {
        const SceGxmProgramParameter *p = sceGxmProgramFindParameterByName(fs->prog, texnames[i]);
        if (p)
            fs->tex_index[i] = (int)sceGxmProgramParameterGetResourceIndex(p);
    }
    if (timed) {
        uint64_t t4 = sceKernelGetProcessTimeWide();
        /* Keep normal helper-log suppression: this must never introduce a
         * synchronous helper log wait. Missing records do not prove no hitch.
         * This single final record is outside the measured setup interval. */
        XV_LOG("[shader-load-us] end %llu source %s total %llu load %llu register %llu link %llu metadata %llu: %s against %s; discard %u psc %u x %u\n",
               (unsigned long long)t4, source == 1 ? "embedded" : "file",
               (unsigned long long)(t4-t0), (unsigned long long)(t1-t0),
               (unsigned long long)(t2-t1), (unsigned long long)(t3-t2),
               (unsigned long long)(t4-t3), gxp_path,
               vs->desc ? vs->desc->gxp : "?", (unsigned)fs->uses_discard, psc_array, psc_components);
    } else {
        XV_LOG("%s: linked against %s; discard %u psc %u x %u\n", gxp_path,
               vs->desc ? vs->desc->gxp : "?", (unsigned)fs->uses_discard, psc_array, psc_components);
    }
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
    if (fs->p_atest) { void *fub; if (XV_RENDER_CALL(XV_RENDER_FRAGMENT_UNIFORM, sceGxmReserveFragmentDefaultUniformBuffer(ctx, &fub)) == 0) {
        static const float off[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; sceGxmSetUniformDataF(fub, fs->p_atest, 0, 4, off); } }
}

int xv_vshader_begin_constants(SceGxmContext *ctx, const xv_vshader_t *vs, void **ub)
{
    *ub = NULL;
    if (!vs->p_c)
        return 0;                                  /* program has no c[]: nothing to reserve */
    return XV_RENDER_CALL(XV_RENDER_VERTEX_UNIFORM, sceGxmReserveVertexDefaultUniformBuffer(ctx, ub));
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

void xv_vshader_set_streams(SceGxmContext *ctx, const xv_vshader_t *vs, const void *const *streams)
{
    for (unsigned s = 0; s < vs->desc->nstreams; ++s)
        if (streams[s])
            sceGxmSetVertexStream(ctx, s, streams[s]);
    if (vs->const_stream != 0xFF)
        sceGxmSetVertexStream(ctx, vs->const_stream, g_const_attr);
}
