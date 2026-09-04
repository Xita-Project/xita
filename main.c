/*
 * main.c — Xita runtime skeleton for the PlayStation Vita (vitasdk, bare libgxm)
 *
 * Implements the four foundation layers of the architecture blueprint
 * (xita-architecture.txt):
 *
 *   §1.2  Guest RAM   : one 64 MB SCE_KERNEL_MEMBLOCK_TYPE_USER_RW block = Xbox
 *                       physical memory 0x00000000..0x03FFFFFF.  guest -> host is
 *                       `g_xram + (addr & XV_GUEST_MASK)`; the 0x8xxxxxxx / 0xFxxxxxxx
 *                       aliases collapse under the mask.
 *   §1.2  Zero copy   : that same block is handed to sceGxmMapMemory() with READ|WRITE,
 *                       so vertex / index / texture data and even render-to-texture
 *                       surfaces are consumed by the SGX in place — no shadow copies.
 *   §5    GXM context : the minimal libgxm bring-up — initialise, VDM / vertex /
 *                       fragment / fragment-USSE ring buffers, context, render target,
 *                       two CDRAM display buffers + sync objects, one depth buffer,
 *                       display queue callback.  No vitaGL, no runtime shader compiler.
 *   §3.4  Scheduler   : xk_sched — every emulated Xbox thread is a fiber inside ONE
 *                       Vita thread pinned to core 0 ("guest"); a second Vita thread
 *                       pinned to core 1 is the render/present pump.  Context switch
 *                       is ~40 cycles of hand-written Thumb-2 (§3.3).
 *
 * Build (vitasdk):
 *   arm-vita-eabi-gcc -Wl,-q -O2 -mthumb -o xita.elf main.c \
 *       -lSceGxm_stub -lSceDisplay_stub -lSceKernelThreadMgr_stub \
 *       -lSceSysmem_stub -lSceProcessmgr_stub -lSceLibKernel_stub
 *   vita-elf-create xita.elf xita.velf
 *   vita-make-fself -s xita.velf eboot.bin
 *   vita-mksfoex -s TITLE_ID=XITA00001 "Xita" param.sfo
 *   vita-pack-vpk -s param.sfo -b eboot.bin xita.vpk
 *
 * Everything the recompiled game and the D3D HLE layer need to plug into is exposed
 * at the bottom of this file (xv_gpu_ptr, xv_gpu_ensure_visible, xk_* fiber API,
 * xv_present).  What is intentionally NOT here yet: shader programs (Stage 3 output
 * compiled by psp2cgc), the D3D HLE state machine, and the kernel API translator.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/power.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include "xv_shader.h"
#include "xv_d3d.h"
#include "xv_scene.h"
#include "xv_layouts.h"        /* generated: recompiled-shader layouts (gen_layouts.py) */

/* ======================================================================================
 *  Configuration
 * ==================================================================================== */

#include "xv_log.h"
#define XV_LOG(...)                 xv_logf("[xv] " __VA_ARGS__)

#define XV_XRAM_SIZE                (64u * 1024u * 1024u)     /* Xbox physical RAM     */
#define XV_GUEST_MASK               0x03FFFFFFu               /* 26 bits = 64 MB       */

#define XV_DISPLAY_WIDTH            960
#define XV_DISPLAY_HEIGHT           544
#define XV_DISPLAY_STRIDE           1024                      /* pixels                */
#define XV_DISPLAY_BUFFER_COUNT     2
#define XV_DISPLAY_PIXEL_FORMAT     SCE_DISPLAY_PIXELFORMAT_A8B8G8R8
#define XV_COLOR_FORMAT             SCE_GXM_COLOR_FORMAT_A8B8G8R8
#define XV_DISPLAY_MAX_PENDING      2                         /* frames in flight      */

/* The guest thread's stack also hosts every fiber's native stack (see xks_init):
 * the kernel refuses syscalls whose SP lies outside the calling thread's own
 * registered stack, so fiber stacks are carved out of it instead of allocated. */
#define XV_GUEST_THREAD_STACK       (2 * 1024 * 1024)
#define XV_PUMP_THREAD_STACK        (256 * 1024)
#define XV_THREAD_PRIORITY          0x10000100                /* SCE default user prio */

#define XK_MAX_FIBERS               16
#define XK_NATIVE_STACK_SIZE        (32 * 1024)               /* wrapper C frames only */
#define XK_PRIORITY_LEVELS          32
#define XK_DEFAULT_PRIORITY         8                         /* Xbox THREAD_PRIORITY_NORMAL */
#define XK_QUANTUM_US               2000                      /* ~Xbox default quantum */

#define ALIGN_UP(x, a)              (((x) + ((a) - 1)) & ~((a) - 1))
#define ARRAY_COUNT(a)              (sizeof(a) / sizeof((a)[0]))

/* ======================================================================================
 *  §1.2  Guest RAM — the single copy of Xbox memory, visible to CPU and GPU
 * ==================================================================================== */

#ifdef XV_RUN_RECOMP
extern uint8_t *g_xram;                  /* defined in xv_boot.c (page-table arena) */
#else
static SceUID   g_xram_uid = -1;
static uint8_t *g_xram     = NULL;      /* host base of guest physical 0x00000000     */
#endif

/* Translate any guest pointer (user, 0x8xxxxxxx kernel alias, 0xFxxxxxxx WC alias)
 * to the host/GPU address.  Both are identical because the block is GXM-mapped. */
#ifdef XV_RUN_RECOMP
extern uint32_t *g_xpt;                  /* recomp page table: guest page -> arena offset */
static inline void *xv_gpu_ptr(uint32_t guest_addr)
{
    /* The recompiled kernel places virtual allocations anywhere in the arena: go through its page
     * table, exactly like X_G(), so heap objects (texture headers, D3D resources) resolve correctly. */
    return g_xram + g_xpt[guest_addr >> 12] + (guest_addr & 0xFFFu);
}
#else
static inline void *xv_gpu_ptr(uint32_t guest_addr)
{
    return g_xram + (guest_addr & XV_GUEST_MASK);
}
#endif

/*
 * Cache coherency (§1.3, option A).  USER_RW memory is write-back cached on the A9;
 * the SGX reads DRAM.  Before the GPU consumes a range the CPU wrote, the L1+L2 lines
 * must be cleaned.  User mode cannot issue cache maintenance, so a tiny taiHEN kernel
 * module exports xv_kmod_dcache_clean() (-> ksceKernelCpuDcacheAndL2WritebackRange).
 * It is declared weak: when the module is not present the call resolves to NULL and
 * we fall back to a no-op, which is only safe while nothing is drawn from XRAM.
 */
extern int xv_kmod_dcache_clean(const void *ptr, SceSize len) __attribute__((weak));

/* Per-draw dcache cleans were 3 kernel calls per draw (streams + indices): 300-450 syscalls a frame.
 * Collect ranges instead and clean the merged set once, right before the GXM submit (xv_gpu_flush_pending). */
#define XV_FLUSH_MAX 512
static struct { uintptr_t lo, hi; } g_flush[XV_FLUSH_MAX]; static unsigned g_nflush; static unsigned g_flush_overflow;
static inline void xv_gpu_ensure_visible(const void *ptr, SceSize len)
{
    if (!xv_kmod_dcache_clean || !len) return;
    uintptr_t lo = (uintptr_t)ptr & ~(uintptr_t)63u, hi = ((uintptr_t)ptr + len + 63u) & ~(uintptr_t)63u;
    for (unsigned i = 0; i < g_nflush; ++i) {                          /* merge with an overlapping/adjacent range */
        if (lo <= g_flush[i].hi + 4096u && hi + 4096u >= g_flush[i].lo) { if (lo < g_flush[i].lo) g_flush[i].lo = lo; if (hi > g_flush[i].hi) g_flush[i].hi = hi; return; }
    }
    if (g_nflush < XV_FLUSH_MAX) { g_flush[g_nflush].lo = lo; g_flush[g_nflush].hi = hi; g_nflush++; }
    else { g_flush_overflow++; xv_kmod_dcache_clean(ptr, len); }         /* table full: clean immediately */
}
void xv_gpu_flush_pending(void)
{
    if (!xv_kmod_dcache_clean) { g_nflush = 0; return; }
    for (unsigned i = 0; i < g_nflush; ++i) xv_kmod_dcache_clean((const void *)g_flush[i].lo, (SceSize)(g_flush[i].hi - g_flush[i].lo));
    g_nflush = 0;
}

/* Host services used by the D3D HLE (xv_d3d.h). */
void *xv_guest_ptr(uint32_t guest_addr)              { return xv_gpu_ptr(guest_addr); }
void  xv_gpu_flush(const void *ptr, uint32_t len)    { xv_gpu_ensure_visible(ptr, len); }

#ifndef XV_RUN_RECOMP
static int xv_xram_init(void)
{
    /* Exactly 64 MB, cached (USER_RW), 4 KB alignment is implicit for this type. */
    g_xram_uid = sceKernelAllocMemBlock("xv_xram", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                        XV_XRAM_SIZE, NULL);
    if (g_xram_uid < 0) {
        XV_LOG("XRAM alloc failed: 0x%08X\n", g_xram_uid);
        return -1;
    }
    sceKernelGetMemBlockBase(g_xram_uid, (void **)&g_xram);

    /* Zero-copy: the very same block becomes GPU-visible for reads AND writes
     * (WRITE so guest-allocated render targets in XRAM work in place too). */
    int err = sceGxmMapMemory(g_xram, XV_XRAM_SIZE,
                              SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
    if (err != SCE_OK) {
        XV_LOG("sceGxmMapMemory(XRAM) failed: 0x%08X\n", err);
        return -1;
    }

    /* Xbox memory is zero on cold boot; games rely on .bss and unallocated pages
     * reading as zero.  (The XBE .data/.rdata images are decompressed here later.) */
    memset(g_xram, 0, XV_XRAM_SIZE);

    XV_LOG("XRAM: 64 MB @ %p (guest 0x00000000-0x03FFFFFF), GXM-mapped R/W\n", g_xram);
    return 0;
}

static void __attribute__((unused)) xv_xram_shutdown(void)
{
    if (g_xram) {
        sceGxmUnmapMemory(g_xram);
        sceKernelFreeMemBlock(g_xram_uid);
        g_xram = NULL;
    }
}
#endif /* !XV_RUN_RECOMP */

/* ======================================================================================
 *  §5  libgxm bring-up — rings, context, render target, display buffers
 * ==================================================================================== */

typedef struct {
    SceUID   uid;
    void    *base;
    uint32_t size;
} xv_memblock_t;

/* Generic GPU-visible allocation.  Ring buffers are USER_RW_UNCACHE (the CPU only
 * ever streams into them), display/depth buffers are CDRAM. */
static void *xv_gpu_alloc(SceKernelMemBlockType type, uint32_t size, uint32_t attribs,
                          const char *name, xv_memblock_t *out)
{
    size = (type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW) ? ALIGN_UP(size, 256 * 1024)
                                                            : ALIGN_UP(size, 4 * 1024);
    out->uid = sceKernelAllocMemBlock(name, type, size, NULL);
    if (out->uid < 0) {
        XV_LOG("alloc %s (%u B) failed: 0x%08X\n", name, size, out->uid);
        return NULL;
    }
    sceKernelGetMemBlockBase(out->uid, &out->base);
    int err = sceGxmMapMemory(out->base, size, attribs);
    if (err != SCE_OK) {
        XV_LOG("map %s failed: 0x%08X\n", name, err);
        sceKernelFreeMemBlock(out->uid);
        return NULL;
    }
    out->size = size;
    return out->base;
}

static void xv_gpu_free(xv_memblock_t *blk)
{
    if (blk->base) {
        sceGxmUnmapMemory(blk->base);
        sceKernelFreeMemBlock(blk->uid);
        blk->base = NULL;
    }
}

/* Fragment USSE memory is mapped through its own API and addressed by offset. */
static void *xv_fragment_usse_alloc(uint32_t size, xv_memblock_t *out, uint32_t *usse_offset)
{
    size = ALIGN_UP(size, 4 * 1024);
    out->uid = sceKernelAllocMemBlock("xv_frag_usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                      size, NULL);
    if (out->uid < 0)
        return NULL;
    sceKernelGetMemBlockBase(out->uid, &out->base);
    if (sceGxmMapFragmentUsseMemory(out->base, size, usse_offset) != SCE_OK) {
        sceKernelFreeMemBlock(out->uid);
        return NULL;
    }
    out->size = size;
    return out->base;
}

/* Data handed to the display-queue callback per flip. */
typedef struct {
    void *address;
} xv_display_data_t;

typedef struct {
    SceGxmContext          *ctx;
    uint8_t                 ctx_host_mem[SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE];
    xv_memblock_t           vdm_ring;             /* command stream            */
    xv_memblock_t           vertex_ring;          /* default vertex uniforms   */
    xv_memblock_t           fragment_ring;        /* default fragment uniforms */
    xv_memblock_t           fragment_usse_ring;
    uint32_t                fragment_usse_offset;

    SceGxmRenderTarget     *render_target;
    xv_memblock_t           display_mem[XV_DISPLAY_BUFFER_COUNT];
    SceGxmColorSurface      display_surface[XV_DISPLAY_BUFFER_COUNT];
    SceGxmSyncObject       *display_sync[XV_DISPLAY_BUFFER_COUNT];
    xv_memblock_t           depth_mem;
    SceGxmDepthStencilSurface depth_surface;

    uint32_t                back_index;
    uint32_t                front_index;
    uint32_t                frame_counter;

    int                     hle_ready;            /* D3D HLE + runtime shaders loaded */
} xv_gfx_t;

/* Handles of the runtime-owned vertex programs registered with the HLE. */
static uint32_t g_h_clear, g_h_test_tex, g_h_ff, g_h_model;

/* Guest addresses of the mock scene (written by the game fiber, read by the GPU
 * through the D3D HLE exactly as a real title's buffers would be). */
#define XV_MOCK_VB_GUEST     0xF0100000u          /* 4 vertices: float3 pos, float2 uv, D3DCOLOR (24 B) */
#define XV_MOCK_IB_GUEST     0xF0100100u          /* 6 x u16                                            */
#define XV_MOCK_VBRES_GUEST  0xF0100400u          /* X_D3DResource for the vertex buffer                */
#define XV_MOCK_TEXRES_GUEST 0xF0100420u          /* X_D3DPixelContainer for the texture                */
#define XV_MOCK_TEX_GUEST    0xF0110000u          /* 64x64 A8R8G8B8, Xbox-swizzled (16 KB)             */

static xv_gfx_t g_gfx;

/* Runs on the GXM display thread: flip to the finished buffer, wait for vblank. */
static void xv_display_callback(const void *callback_data)
{
    const xv_display_data_t *dd = (const xv_display_data_t *)callback_data;
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size        = sizeof(fb);
    fb.base        = dd->address;
    fb.pitch       = XV_DISPLAY_STRIDE;
    fb.pixelformat = XV_DISPLAY_PIXEL_FORMAT;
    fb.width       = XV_DISPLAY_WIDTH;
    fb.height      = XV_DISPLAY_HEIGHT;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

static int xv_gfx_init(void)
{
    xv_gfx_t *g = &g_gfx;
    memset(g, 0, sizeof(*g));

    /* --- 1. library init: display queue depth and the parameter buffer ------------ */
    SceGxmInitializeParams init;
    memset(&init, 0, sizeof(init));
    init.flags                        = 0;
    init.displayQueueMaxPendingCount  = XV_DISPLAY_MAX_PENDING;
    init.displayQueueCallback         = xv_display_callback;
    init.displayQueueCallbackDataSize = sizeof(xv_display_data_t);
    init.parameterBufferSize          = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    int err = sceGxmInitialize(&init);
    if (err != SCE_OK) {
        XV_LOG("sceGxmInitialize failed: 0x%08X\n", err);
        return -1;
    }

    /* --- 2. ring buffers (blueprint §0 budget: 1 MB + 1 MB + 512 KB + 16 KB) ------- */
    if (!xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE,
                      SCE_GXM_MEMORY_ATTRIB_READ, "xv_vdm_ring", &g->vdm_ring))
        return -1;
    if (!xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE,
                      SCE_GXM_MEMORY_ATTRIB_READ, "xv_vertex_ring", &g->vertex_ring))
        return -1;
    if (!xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE,
                      SCE_GXM_MEMORY_ATTRIB_READ, "xv_fragment_ring", &g->fragment_ring))
        return -1;
    if (!xv_fragment_usse_alloc(SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE,
                                &g->fragment_usse_ring, &g->fragment_usse_offset))
        return -1;

    /* --- 3. the rendering context -------------------------------------------------- */
    SceGxmContextParams cp;
    memset(&cp, 0, sizeof(cp));
    cp.hostMem                       = g->ctx_host_mem;
    cp.hostMemSize                   = sizeof(g->ctx_host_mem);
    cp.vdmRingBufferMem              = g->vdm_ring.base;
    cp.vdmRingBufferMemSize          = g->vdm_ring.size;
    cp.vertexRingBufferMem           = g->vertex_ring.base;
    cp.vertexRingBufferMemSize       = g->vertex_ring.size;
    cp.fragmentRingBufferMem         = g->fragment_ring.base;
    cp.fragmentRingBufferMemSize     = g->fragment_ring.size;
    cp.fragmentUsseRingBufferMem     = g->fragment_usse_ring.base;
    cp.fragmentUsseRingBufferMemSize = g->fragment_usse_ring.size;
    cp.fragmentUsseRingBufferOffset  = g->fragment_usse_offset;
    err = sceGxmCreateContext(&cp, &g->ctx);
    if (err != SCE_OK) {
        XV_LOG("sceGxmCreateContext failed: 0x%08X\n", err);
        return -1;
    }

    /* --- 4. render target describing the tiling of the frame ----------------------- */
    SceGxmRenderTargetParams rt;
    memset(&rt, 0, sizeof(rt));
    rt.flags                = 0;
    rt.width                = XV_DISPLAY_WIDTH;
    rt.height               = XV_DISPLAY_HEIGHT;
    rt.scenesPerFrame       = 1;
    rt.multisampleMode      = SCE_GXM_MULTISAMPLE_NONE;
    rt.multisampleLocations = 0;
    rt.driverMemBlock       = -1;          /* let GXM allocate its own tiling memory */
    err = sceGxmCreateRenderTarget(&rt, &g->render_target);
    if (err != SCE_OK) {
        XV_LOG("sceGxmCreateRenderTarget failed: 0x%08X\n", err);
        return -1;
    }

    /* --- 5. two display buffers in CDRAM (separate 128 MB pool, not main RAM) ----- */
    const uint32_t display_bytes = 4 * XV_DISPLAY_STRIDE * XV_DISPLAY_HEIGHT;
    for (uint32_t i = 0; i < XV_DISPLAY_BUFFER_COUNT; ++i) {
        void *buf = xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, display_bytes,
                                 SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE,
                                 "xv_display", &g->display_mem[i]);
        if (!buf)
            return -1;

        /* CPU pre-clear to a dark slate so the very first flip is deterministic;
         * real clears happen in-scene once the Stage 3 shaders are linked in. */
        uint32_t *px = (uint32_t *)buf;
        for (uint32_t p = 0; p < XV_DISPLAY_STRIDE * XV_DISPLAY_HEIGHT; ++p)
            px[p] = 0xFF201810u;                         /* ABGR */

        err = sceGxmColorSurfaceInit(&g->display_surface[i], XV_COLOR_FORMAT,
                                     SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                                     SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
                                     XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT, XV_DISPLAY_STRIDE, buf);
        if (err != SCE_OK) {
            XV_LOG("sceGxmColorSurfaceInit failed: 0x%08X\n", err);
            return -1;
        }
        err = sceGxmSyncObjectCreate(&g->display_sync[i]);
        if (err != SCE_OK) {
            XV_LOG("sceGxmSyncObjectCreate failed: 0x%08X\n", err);
            return -1;
        }
    }

    /* --- 6. one depth/stencil buffer, tiled, in CDRAM ------------------------------ */
    const uint32_t aligned_w = ALIGN_UP(XV_DISPLAY_WIDTH,  SCE_GXM_TILE_SIZEX);
    const uint32_t aligned_h = ALIGN_UP(XV_DISPLAY_HEIGHT, SCE_GXM_TILE_SIZEY);
    void *depth = xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * aligned_w * aligned_h,
                               SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE,
                               "xv_depth", &g->depth_mem);
    if (!depth)
        return -1;
    err = sceGxmDepthStencilSurfaceInit(&g->depth_surface, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
                                        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, aligned_w, depth, NULL);
    if (err != SCE_OK) {
        XV_LOG("sceGxmDepthStencilSurfaceInit failed: 0x%08X\n", err);
        return -1;
    }

    g->back_index  = 0;
    g->front_index = XV_DISPLAY_BUFFER_COUNT - 1;
    XV_LOG("GXM: context up, %ux%u, %u display buffers, rings %u/%u/%u KB\n",
           XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT, XV_DISPLAY_BUFFER_COUNT,
           g->vdm_ring.size >> 10, g->vertex_ring.size >> 10, g->fragment_ring.size >> 10);

    /* --- 7. shader patcher + D3D HLE ----------------------------------------------
     * The HLE knows every recompiled Halo program (xv_halo_vs, matched by blob hash
     * at CreateVertexShader time) plus the runtime-owned programs registered here:
     * the clear quad, and the textured test shader used by the mock game.  Missing
     * .gxp files are not fatal - the frame loop just skips what it cannot draw. */
    extern int  xv_ui_gxm_init(void);
#ifdef XV_RUN_RECOMP
    /* Recomp path: the recompiled engine is the game (recomp/kernel/xd3d.c), so the runtime D3D HLE
     * is not used - only the shader patcher and the UI GXM bridge. */
    g->hle_ready = 0;
    if (xv_shader_init() != 0) {
        XV_LOG("shader patcher init failed\n");
    } else if (xv_ui_gxm_init() != 0) {
        XV_LOG("UI GXM bridge init failed (compile shaders/xv_ui.frag.gxp + halo_vs_03.gxp)\n");
    } else {
        XV_LOG("UI GXM bridge ready (recompiled immediate-mode path)\n");
        /* world geometry (DrawIndexedVertices) goes through the Stage-3 bridge, programs registered on demand */
        if (xv_d3d_init(xv_halo_vs, XV_HALO_VS_COUNT) == 0) {
            g_h_clear = xv_d3d_RegisterVertexShader(&xv_vs_clear);
            xv_d3d_set_clear_shader(g_h_clear);
            g->hle_ready = g_h_clear != 0;
            XV_LOG("mesh path ready (clear 0x%X)\n", g_h_clear);
        } else XV_LOG("xv_d3d init failed - world geometry disabled\n");
    }
#else
    if (xv_shader_init() == 0 && xv_d3d_init(xv_halo_vs, XV_HALO_VS_COUNT) == 0) {
        g_h_clear    = xv_d3d_RegisterVertexShader(&xv_vs_clear);
        g_h_test_tex = xv_d3d_RegisterVertexShader(&xv_vs_test_tex);
        g_h_ff       = xv_d3d_RegisterVertexShader(&xv_vs_ff_test);
        g_h_model    = xv_d3d_RegisterVertexShader(&xv_vs_model);
        xv_d3d_set_clear_shader(g_h_clear);
        g->hle_ready = (g_h_clear && g_h_test_tex);
        if (xv_ui_gxm_init() == 0)
            XV_LOG("UI GXM bridge ready (recompiled immediate-mode path)\n");
        XV_LOG("HLE shaders: clear 0x%X, test_tex 0x%X, ff_test 0x%X, model 0x%X\n",
               g_h_clear, g_h_test_tex, g_h_ff, g_h_model);
        if (!g->hle_ready)
            XV_LOG("runtime shaders missing (compile shaders/*.cg first); frames stay empty\n");
    }
#endif
    return 0;
}

/*
 * One frame on the pump thread.  This is where the D3D HLE layer's recorded draw
 * list will be replayed: vertex streams / index pointers / texture control words all
 * point straight into XRAM (xv_gpu_ptr) after xv_gpu_ensure_visible() on the ranges
 * the guest touched (§1.5).  For now the scene is empty — begin/end/flip only.
 */
static void xv_gfx_render_frame(void)
{
    xv_gfx_t *g = &g_gfx;

#ifdef XV_RUN_RECOMP
    if (g->hle_ready) {                 /* render-to-texture passes first, each in its own scene */
        extern uint32_t xv_ui_gxm_mesh_frame(void);
        uint32_t mf0 = xv_ui_gxm_mesh_frame();
        if (mf0 != 0xFFFFFFFFu) xv_d3d_render_offscreen(g->ctx, mf0);
    }
#endif
    int err = sceGxmBeginScene(g->ctx, 0, g->render_target, NULL, NULL,
                               g->display_sync[g->back_index],
                               &g->display_surface[g->back_index], &g->depth_surface);
    if (err != SCE_OK) {
        XV_LOG("sceGxmBeginScene failed: 0x%08X\n", err);
        return;
    }

    /* Replay the D3D HLE command list the game fiber recorded for this frame:
     * clears, draws with their streams/indices/textures pointing into guest RAM. */
#ifndef XV_RUN_RECOMP
    if (g->hle_ready)
        xv_d3d_render(g->ctx, g->frame_counter);
#else
    if (g->hle_ready) {
        extern uint32_t xv_ui_gxm_mesh_frame(void);
        uint32_t mf = xv_ui_gxm_mesh_frame();
        /* Halo's VS emits D3D clip space; same viewport the UI replay uses */
        sceGxmSetViewport(g->ctx, 480.0f, 480.0f, 272.0f, -272.0f, 0.5f, 0.5f);
        if (mf != 0xFFFFFFFFu) xv_d3d_render(g->ctx, mf);
    }
#endif
    { extern void xv_ui_gxm_replay(SceGxmContext *ctx); xv_ui_gxm_replay(g->ctx); }

    sceGxmEndScene(g->ctx, NULL, NULL);

    /* Present: heartbeat keeps the GPU/display in step, then queue the flip. */
    sceGxmPadHeartbeat(&g->display_surface[g->back_index], g->display_sync[g->back_index]);

    xv_display_data_t dd;
    dd.address = g->display_mem[g->back_index].base;
    sceGxmDisplayQueueAddEntry(g->display_sync[g->front_index],
                               g->display_sync[g->back_index], &dd);

    g->front_index = g->back_index;
    g->back_index  = (g->back_index + 1) % XV_DISPLAY_BUFFER_COUNT;
    g->frame_counter++;
}


/* Drain the GPU and the display queue.  Must run before ANY GPU-visible memory
 * (including the guest block) is unmapped: the last frame's draws may still be
 * reading it, and the SGX reports that as a GPU crash. */
static void xv_gfx_finish(void)
{
    xv_gfx_t *g = &g_gfx;
    if (g->ctx) {
        sceGxmFinish(g->ctx);
        sceGxmDisplayQueueFinish();
    }
}

static void __attribute__((unused)) xv_gfx_shutdown(void)
{
    xv_gfx_t *g = &g_gfx;
    XV_LOG("shutdown: finish\n");
    xv_gfx_finish();
    XV_LOG("shutdown: d3d\n");
    xv_d3d_shutdown();
    XV_LOG("shutdown: shader patcher\n");
    xv_shader_shutdown();
    XV_LOG("shutdown: display buffers\n");
    for (uint32_t i = 0; i < XV_DISPLAY_BUFFER_COUNT; ++i) {
        if (g->display_sync[i])
            sceGxmSyncObjectDestroy(g->display_sync[i]);
        xv_gpu_free(&g->display_mem[i]);
    }
    xv_gpu_free(&g->depth_mem);
    XV_LOG("shutdown: render target + context\n");
    if (g->render_target)
        sceGxmDestroyRenderTarget(g->render_target);
    if (g->ctx)
        sceGxmDestroyContext(g->ctx);
    XV_LOG("shutdown: rings + terminate\n");
    if (g->fragment_usse_ring.base) {
        sceGxmUnmapFragmentUsseMemory(g->fragment_usse_ring.base);
        sceKernelFreeMemBlock(g->fragment_usse_ring.uid);
    }
    xv_gpu_free(&g->fragment_ring);
    xv_gpu_free(&g->vertex_ring);
    xv_gpu_free(&g->vdm_ring);
    sceGxmTerminate();
}

/* ======================================================================================
 *  §3.3 / §3.4  xk — the fiber scheduler that hosts every emulated Xbox thread
 * ==================================================================================== */

typedef enum {
    XK_FREE = 0,
    XK_READY,
    XK_RUNNING,
    XK_WAIT,          /* blocked on a dispatcher object / present         */
    XK_DELAY,         /* KeDelayExecutionThread / timeout                 */
    XK_DEAD,
} xk_state_t;

/*
 * Saved callee-saved state.  Layout is shared with xk_switch below — do not reorder.
 *   +0   r4..r11   (8 words)
 *   +32  sp
 *   +36  lr
 *   +40  d8..d15   (8 doubles)
 */
typedef struct {
    uint32_t r[8];
    uint32_t sp;
    uint32_t lr;
    uint64_t vfp[8];
} xk_context_t;

typedef void (*xk_entry_t)(void *arg);

typedef struct xk_fiber {
    xk_context_t      ctx;             /* must stay first: xk_switch takes &fiber        */
    xk_state_t        state;
    uint8_t           prio;            /* 0..31, higher runs first                       */
    uint8_t           id;
    uint64_t          wake_at_us;      /* XK_DELAY deadline                              */
    volatile uint32_t *wait_word;      /* XK_WAIT: runnable when *wait_word >= wait_value */
    uint32_t          wait_value;
    struct xk_fiber  *next_ready;
    xk_entry_t        entry;
    void             *arg;
    uint32_t          guest_kpcr;      /* guest addr of the per-thread KPCR (§3.2)       */
    uint8_t          *native_stack;    /* 32 KB host stack for wrapper C frames          */
    const char       *name;
} xk_fiber_t;

typedef struct {
    xk_fiber_t  *ready_head[XK_PRIORITY_LEVELS];
    xk_fiber_t  *ready_tail[XK_PRIORITY_LEVELS];
    uint32_t     ready_mask;           /* bit p set => ready_head[p] non-empty; clz picks */
    xk_fiber_t  *cur;
    xk_fiber_t   root;                 /* the Vita thread's own context (scheduler loop)  */
    xk_fiber_t   pool[XK_MAX_FIBERS];
    uint32_t     live;                 /* fibers not FREE/DEAD                            */
    uint32_t     tick_ms;              /* mirrored into the guest KeTickCount export      */
    uint64_t     slice_end_us;
} xk_sched_t;

static xk_sched_t g_sched;

/* Fiber native stacks.  On hardware they MUST lie inside the hosting Vita thread's
 * registered stack (syscalls validate SP against it), so xks_init() carves them
 * from the bottom of the guest thread's own stack; the static array is only the
 * fallback when the thread's stack cannot be queried (or is too small). */
static uint8_t    g_native_stacks_fallback[XK_MAX_FIBERS][XK_NATIVE_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t   *g_fiber_stack_base = NULL;

/* --- context switch + first-entry trampoline (Thumb-2, interworking safe) ------------ */
__asm__(
    ".syntax unified\n"
    ".text\n"
    ".align 2\n"
    ".thumb\n"
    ".thumb_func\n"
    ".global xk_switch\n"
    ".type xk_switch, %function\n"
    "xk_switch:\n"                     /* r0 = from (xk_context_t*), r1 = to             */
    "    stmia  r0!, {r4-r11}\n"
    "    str    sp, [r0], #4\n"
    "    str    lr, [r0], #4\n"
    "    vstmia r0!, {d8-d15}\n"
    "    ldmia  r1!, {r4-r11}\n"
    "    ldr    r2, [r1], #4\n"
    "    mov    sp, r2\n"
    "    ldr    lr, [r1], #4\n"
    "    vldmia r1!, {d8-d15}\n"
    "    bx     lr\n"
    ".size xk_switch, .-xk_switch\n"

    ".align 2\n"
    ".thumb_func\n"
    ".global xk_fiber_trampoline\n"
    ".type xk_fiber_trampoline, %function\n"
    "xk_fiber_trampoline:\n"           /* first resume lands here: r4 = entry, r5 = arg  */
    "    mov    r0, r5\n"
    "    blx    r4\n"
    "    bl     xk_fiber_exit\n"       /* never returns                                  */
    ".size xk_fiber_trampoline, .-xk_fiber_trampoline\n"
);

extern void xk_switch(xk_context_t *from, xk_context_t *to);
extern void xk_fiber_trampoline(void);
void xk_fiber_exit(void);

static inline uint64_t xk_now_us(void)
{
    return sceKernelGetProcessTimeWide();
}

static void xk_rq_push(xk_fiber_t *f)
{
    f->state = XK_READY;
    f->next_ready = NULL;
    if (g_sched.ready_tail[f->prio])
        g_sched.ready_tail[f->prio]->next_ready = f;
    else
        g_sched.ready_head[f->prio] = f;
    g_sched.ready_tail[f->prio] = f;
    g_sched.ready_mask |= 1u << f->prio;
}

static xk_fiber_t *xk_rq_pop(uint32_t prio)
{
    xk_fiber_t *f = g_sched.ready_head[prio];
    g_sched.ready_head[prio] = f->next_ready;
    if (!g_sched.ready_head[prio]) {
        g_sched.ready_tail[prio] = NULL;
        g_sched.ready_mask &= ~(1u << prio);
    }
    return f;
}

/* Move DELAY / WAIT fibers whose condition is met back onto the run queue. */
static void xk_expire(uint64_t now)
{
    for (uint32_t i = 0; i < XK_MAX_FIBERS; ++i) {
        xk_fiber_t *f = &g_sched.pool[i];
        if (f->state == XK_DELAY && now >= f->wake_at_us)
            xk_rq_push(f);
        else if (f->state == XK_WAIT && f->wait_word &&
                 (int32_t)(*f->wait_word - f->wait_value) >= 0)
            xk_rq_push(f);
    }
    g_sched.tick_ms = (uint32_t)(now / 1000);
}

/* Earliest DELAY deadline, or `fallback` if none. */
static uint64_t xk_next_deadline(uint64_t fallback)
{
    uint64_t best = fallback;
    for (uint32_t i = 0; i < XK_MAX_FIBERS; ++i) {
        xk_fiber_t *f = &g_sched.pool[i];
        if (f->state == XK_DELAY && f->wake_at_us < best)
            best = f->wake_at_us;
    }
    return best;
}

/*
 * §3.4 core.  Called only from safe points (yield / wait / delay / the back-edge
 * quantum check emitted by the recompiler).  Picks the highest-priority ready fiber
 * with a single clz, switches to it, and returns when the caller is resumed.
 */
static void xk_schedule(void)
{
    xk_fiber_t *prev = g_sched.cur;
    for (;;) {
        xk_expire(xk_now_us());
        if (g_sched.ready_mask)
            break;
        /* Everyone is blocked: idle the core for real instead of spinning. */
        uint64_t now  = xk_now_us();
        uint64_t next = xk_next_deadline(now + 1000);
        uint64_t wait = next > now ? next - now : 1;
        sceKernelDelayThread(wait > 16000 ? 16000 : (SceUInt)wait);
        if (g_sched.live == 0)
            break;
    }
    if (!g_sched.ready_mask) {
        /* nothing left to run: unwind to the root context (scheduler loop returns) */
        if (prev != &g_sched.root) {
            g_sched.cur = &g_sched.root;
            xk_switch(&prev->ctx, &g_sched.root.ctx);
        }
        return;
    }
    uint32_t prio = 31u - (uint32_t)__builtin_clz(g_sched.ready_mask);
    xk_fiber_t *next = xk_rq_pop(prio);
    if (next == prev) {
        prev->state = XK_RUNNING;
        return;
    }
    next->state = XK_RUNNING;
    g_sched.cur = next;
    g_sched.slice_end_us = xk_now_us() + XK_QUANTUM_US;
    xk_switch(&prev->ctx, &next->ctx);
    /* resumed here when `prev` is picked again */
}

/* --- public fiber API used by the kernel translator / D3D HLE ------------------------ */

void xks_init(void)
{
    memset(&g_sched, 0, sizeof(g_sched));
    g_sched.root.state = XK_RUNNING;
    g_sched.root.name  = "root";
    g_sched.cur        = &g_sched.root;

    /* Place the fiber stacks inside this thread's own stack: bottom-up slices,
     * leaving the top half for the scheduler loop itself (which uses very little). */
    SceKernelThreadInfo ti;
    memset(&ti, 0, sizeof(ti));
    ti.size = sizeof(ti);
    const SceSize need = XK_MAX_FIBERS * XK_NATIVE_STACK_SIZE;
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &ti) == 0 && ti.stack &&
        (SceSize)ti.stackSize >= need + 256 * 1024) {
        g_fiber_stack_base = (uint8_t *)ti.stack;           /* lowest address of the stack */
        XV_LOG("xk: fiber stacks inside thread stack %p..%p (%u x %u KB)\n", ti.stack,
               (uint8_t *)ti.stack + ti.stackSize, XK_MAX_FIBERS, XK_NATIVE_STACK_SIZE >> 10);
    } else {
        g_fiber_stack_base = NULL;
        XV_LOG("xk: WARNING using static fiber stacks (syscalls from fibers may fault on hardware)\n");
    }
}

xk_fiber_t *xk_fiber_create(const char *name, xk_entry_t entry, void *arg, uint8_t prio)
{
    for (uint32_t i = 0; i < XK_MAX_FIBERS; ++i) {
        xk_fiber_t *f = &g_sched.pool[i];
        if (f->state != XK_FREE && f->state != XK_DEAD)
            continue;
        memset(f, 0, sizeof(*f));
        f->id           = (uint8_t)i;
        f->name         = name;
        f->entry        = entry;
        f->arg          = arg;
        f->prio         = prio < XK_PRIORITY_LEVELS ? prio : XK_DEFAULT_PRIORITY;
        f->native_stack = g_fiber_stack_base ? g_fiber_stack_base + i * XK_NATIVE_STACK_SIZE
                                             : g_native_stacks_fallback[i];

        /* Initial context: xk_switch "returns" into the trampoline with r4/r5 loaded. */
        f->ctx.r[0] = (uint32_t)(uintptr_t)entry;      /* r4 */
        f->ctx.r[1] = (uint32_t)(uintptr_t)arg;        /* r5 */
        f->ctx.sp   = (uint32_t)(uintptr_t)(f->native_stack + XK_NATIVE_STACK_SIZE);
        f->ctx.lr   = (uint32_t)(uintptr_t)xk_fiber_trampoline;

        g_sched.live++;
        xk_rq_push(f);
        return f;
    }
    return NULL;
}

void xks_yield(void)
{
    xk_fiber_t *f = g_sched.cur;
    if (f != &g_sched.root)
        xk_rq_push(f);
    xk_schedule();
}

/* The recompiler's back-edge check calls this; cheap when the slice hasn't expired. */
void xk_preempt_check(void)
{
    if (xk_now_us() >= g_sched.slice_end_us)
        xks_yield();
}

void xk_delay_us(uint64_t us)
{
    xk_fiber_t *f = g_sched.cur;
    f->state      = XK_DELAY;
    f->wake_at_us = xk_now_us() + us;
    xk_schedule();
}

/* Block until *word >= value (monotonic counters: frame fences, IO completions). */
void xk_wait_word(volatile uint32_t *word, uint32_t value)
{
    xk_fiber_t *f = g_sched.cur;
    if ((int32_t)(*word - value) >= 0)
        return;
    f->state      = XK_WAIT;
    f->wait_word  = word;
    f->wait_value = value;
    xk_schedule();
}

void xk_fiber_exit(void)
{
    xk_fiber_t *f = g_sched.cur;
    f->state = XK_DEAD;
    g_sched.live--;
    xk_schedule();          /* never returns to a dead fiber */
    for (;;) { }
}

/* Run fibers until all are dead.  Executes on the core-0 guest thread. */
void xk_run(void)
{
    while (g_sched.live) {
        xk_schedule();      /* returns to root only when the run queue drains */
        if (g_sched.live && !g_sched.ready_mask)
            sceKernelDelayThread(1000);
    }
}

/* ======================================================================================
 *  Guest <-> pump handoff (§5 frame flow): monotonic fences, no locks
 * ==================================================================================== */

static volatile uint32_t g_frame_requested = 0;    /* written by guest fiber            */
static volatile uint32_t g_frame_completed = 0;    /* written by pump thread            */
static volatile int      g_running         = 1;

/* D3DDevice_Swap() HLE lands here: ask the pump for a flip and block only this fiber.
 * Other fibers keep running; if all are blocked the core idles (§3.4). */
void xv_present(void)
{
#ifdef XV_RUN_RECOMP
    /* First-pixels path: the recompiled engine drives frames on this one thread, so render the
     * frame it just recorded synchronously (BeginScene -> xv_ui_gxm_replay -> EndScene -> flip). */
    xv_gfx_render_frame();
    return;
#else
    uint32_t ticket = __atomic_add_fetch(&g_frame_requested, 1, __ATOMIC_RELEASE);
    /* allow XV_DISPLAY_MAX_PENDING frames in flight: wait for ticket - pending */
    xk_wait_word(&g_frame_completed, ticket - (XV_DISPLAY_MAX_PENDING - 1));
#endif
}

/* Core 1: render/present pump. */
static int xv_pump_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    XV_LOG("pump: up on core 1\n");
    while (g_running) {
        uint32_t req  = __atomic_load_n(&g_frame_requested, __ATOMIC_ACQUIRE);
        uint32_t done = g_frame_completed;
        if ((int32_t)(req - done) > 0) {
            xv_gfx_render_frame();
            __atomic_store_n(&g_frame_completed, done + 1, __ATOMIC_RELEASE);
        } else {
            sceKernelDelayThread(200);      /* ~0.2 ms poll; replace with event flag later */
        }
    }
    return 0;
}

/* ======================================================================================
 *  Mock guest workload — stands in for the recompiled XBE until it is linked in
 * ==================================================================================== */

typedef struct {
    uint32_t frames;
} xv_mock_game_t;

/* "Main game thread": pretend to build a frame in XRAM, then Swap(). */
static void mock_game_main(void *arg)
{
    xv_mock_game_t *g = (xv_mock_game_t *)arg;

    /* Scene viewer: if a Halo scene pack ships in the VPK, replay it through the
     * D3D HLE (real map geometry + textures in guest RAM) instead of the mock quad. */
    if (g_h_model && xv_scene_load("app0:assets/ui_scene.bin") > 0) {
        for (uint32_t i = 0; i < g->frames && g_running; ++i) {
            xv_scene_frame(i, g_h_model);
            xv_d3d_Swap();
            if ((i % 60) == 0)
                XV_LOG("scene: frame %u\n", i);
        }
        XV_LOG("scene: done after %u frames\n", g->frames);
        g_running = 0;
        return;
    }

    /* Everything below lives in guest RAM, addressed through the 0xF0000000 write-
     * combined alias exactly as Xbox D3D hands buffers to a game.  The HLE reads the
     * same structs a real title's D3D calls would pass (blueprint 1.2 / 1.4). */
    struct mock_vertex { float x, y, z, u, v; uint8_t b, g, r, a; };       /* stride 24 */
    struct mock_vertex *vb = (struct mock_vertex *)xv_gpu_ptr(XV_MOCK_VB_GUEST);
    uint16_t *ib = (uint16_t *)xv_gpu_ptr(XV_MOCK_IB_GUEST);
    ib[0] = 0; ib[1] = 1; ib[2] = 2; ib[3] = 0; ib[4] = 2; ib[5] = 3;

    /* D3DVertexBuffer: { Common, Data = physical address, Lock } */
    X_D3DResource *vbres = (X_D3DResource *)xv_gpu_ptr(XV_MOCK_VBRES_GUEST);
    vbres->Common = 0; vbres->Data = XV_MOCK_VB_GUEST & 0x03FFFFFF; vbres->Lock = 0;

    /* 64x64 A8R8G8B8 checkerboard in Xbox swizzled (Morton) order = PowerVR twiddled:
     * the GPU reads these exact bytes, no conversion. */
    uint32_t *tex = (uint32_t *)xv_gpu_ptr(XV_MOCK_TEX_GUEST);
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 64; ++x) {
            uint32_t m = 0;
            for (int bit = 0; bit < 6; ++bit)
                m |= ((x >> bit) & 1) << (2 * bit) | ((y >> bit) & 1) << (2 * bit + 1);
            int on = ((x >> 3) ^ (y >> 3)) & 1;
            tex[m] = on ? 0xFFFFFFFFu : 0xFFFF8020u;                    /* white / orange (ARGB) */
        }
    X_D3DPixelContainer *texres = (X_D3DPixelContainer *)xv_gpu_ptr(XV_MOCK_TEXRES_GUEST);
    texres->Common = 0; texres->Data = XV_MOCK_TEX_GUEST & 0x03FFFFFF; texres->Lock = 0;
    texres->Format = (0x06u << 8) | (1u << 16) | (6u << 20) | (6u << 24);  /* A8R8G8B8, 1 mip, 64x64 */
    texres->Size = 0;

    static const float ident[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };

    for (uint32_t i = 0; i < g->frames && g_running; ++i) {
        float t = (float)(i % 200) * 0.005f - 0.5f;          /* slide left -> right */
        vb[0] = (struct mock_vertex){ -0.5f + t, -0.5f, 0.5f, 0, 1, 0x40, 0x40, 0xFF, 0xFF };   /* red   */
        vb[1] = (struct mock_vertex){  0.5f + t, -0.5f, 0.5f, 1, 1, 0x40, 0xFF, 0x40, 0xFF };   /* green */
        vb[2] = (struct mock_vertex){  0.5f + t,  0.5f, 0.5f, 1, 0, 0xFF, 0x40, 0x40, 0xFF };   /* blue  */
        vb[3] = (struct mock_vertex){ -0.5f + t,  0.5f, 0.5f, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };   /* white */

        /* --- what a game's frame looks like through the HLE ------------------------ */
        xv_d3d_Clear(X_D3DCLEAR_TARGET | X_D3DCLEAR_ZBUFFER, 0xFF201810u, 1.0f, 0);
        xv_d3d_SetRenderState_CullMode(X_D3DCULL_NONE);
        xv_d3d_SetRenderState_ZEnable(0);
        xv_d3d_SetVertexShader(g_h_test_tex);
        xv_d3d_SetVertexShaderConstant(0, ident, 4);
        xv_d3d_SetStreamSource(0, XV_MOCK_VBRES_GUEST, sizeof(struct mock_vertex));
        xv_d3d_SetTexture(0, XV_MOCK_TEXRES_GUEST);
        xv_d3d_SetTextureStageState(0, X_D3DTSS_MINFILTER, X_D3DTEXF_LINEAR);
        xv_d3d_SetTextureStageState(0, X_D3DTSS_MAGFILTER, X_D3DTEXF_LINEAR);
        xv_d3d_DrawIndexedVertices(X_D3DPT_TRIANGLELIST, 6, XV_MOCK_IB_GUEST);
        xv_d3d_Swap();                                   /* records, presents, blocks this fiber only */
        if ((i % 60) == 0)
            XV_LOG("game: frame %u (pump completed %u)\n", i, g_frame_completed);
    }
    XV_LOG("game: done after %u frames\n", g->frames);
    g_running = 0;
}

/* "Audio/streaming thread": lower priority, wakes every 5 ms. */
static void mock_worker(void *arg)
{
    uint32_t ticks = 0;
    (void)arg;
    while (g_running) {
        xk_delay_us(5000);
        ticks++;
    }
    XV_LOG("worker: exited after %u ticks\n", ticks);
}

/* Core 0: the guest execution loop — owns the scheduler and every Xbox thread. */
static int xv_guest_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    XV_LOG("guest: up on core 0\n");

    static xv_mock_game_t game = { .frames = 600 };

    xks_init();
    xk_fiber_create("game_main", mock_game_main, &game, XK_DEFAULT_PRIORITY);
    xk_fiber_create("worker",    mock_worker,    NULL,  XK_DEFAULT_PRIORITY - 1);
    xk_run();

    XV_LOG("guest: all fibers finished\n");
    return 0;
}

/* ======================================================================================
 *  Entry
 * ==================================================================================== */

static void xv_log_memory_budget(const char *when)
{
    SceKernelFreeMemorySizeInfo info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    if (sceKernelGetFreeMemorySize(&info) == 0)
        XV_LOG("%s: free main %u KB, cdram %u KB, phycont %u KB\n", when,
               info.size_user >> 10, info.size_cdram >> 10, info.size_phycont >> 10);
}

#ifdef XV_RUN_RECOMP
unsigned int _newlib_heap_size_user = 48 * 1024 * 1024;   /* page table + fiber stacks + kernel objects */
int  xv_boot_recomp(const char *game_dir, const char *save_dir);

/* Locate the game data (maps/) and a writable save dir, then run the recompiled engine. */
static int xv_recomp_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    static const char *const game_dirs[] = {
        "ux0:data/xita/haloce", "uma0:data/xita/haloce", "app0:haloce",
    };
    const char *game_dir = game_dirs[0];
    for (int i = 0; i < 3; ++i) {
        SceUID d = sceIoDopen(game_dirs[i]);
        if (d >= 0) { sceIoDclose(d); game_dir = game_dirs[i]; break; }
    }
    sceIoMkdir("ux0:data/xita", 0777);
    sceIoMkdir("ux0:data/xita/save", 0777);
    XV_LOG("recomp: game dir %s\n", game_dir);
    xv_boot_recomp(game_dir, "ux0:data/xita/save");
    return 0;
}
#endif

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    XV_LOG("Xita runtime starting\n");
    /* Homebrew boots at 333/111 MHz; ask for the full clocks (CPU 444, bus 222, GPU 222, GPU xbar 166). */
    scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
    XV_LOG("clocks: cpu %d bus %d gpu %d xbar %d MHz\n", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    xv_log_memory_budget("boot");
    /* Settings + debug knobs: ux0:data/xita/xita.cfg (and env.txt, kept for emulator scripts) hold
     * KEY=VALUE lines that become environment variables, so every getenv switch works on the card without a
     * rebuild.  Keys: XV_FPS=1 overlay, XV_VOLUME=0..100, XV_DPAD_EXTRAS=0/1, XV_D3D_HIST=<frame>,
     * XV_FS_FORCE=tex0|lm|texmod, XV_LOG_TEX/XV_LOG_RS/XV_LOG_DS=1, XV_FUNC_HIST=<frame>. */
    { static const char *const cfgs[2] = { "ux0:data/xita/xita.cfg", "ux0:data/xita/env.txt" };
      for (int ci = 0; ci < 2; ++ci) {
        SceUID fd = sceIoOpen(cfgs[ci], SCE_O_RDONLY, 0);
        if (fd < 0) continue;
        static char txt[2048]; int n = sceIoRead(fd, txt, sizeof txt - 1); sceIoClose(fd); if (n < 0) n = 0; txt[n] = 0;
        for (char *ln = txt; ln && *ln; ) { char *nl = strchr(ln, '\n'); if (nl) *nl++ = 0; char *cr = strchr(ln, '\r'); if (cr) *cr = 0; char *eq = strchr(ln, '=');
            if (eq && ln[0] != '#' && ln[0] != ';') { *eq = 0; setenv(ln, eq + 1, 1); XV_LOG("cfg %s=%s\n", ln, eq + 1); } ln = nl; } } }

    if (xv_gfx_init() != 0) {           /* GXM first: sceGxmMapMemory needs it live */
        XV_LOG("graphics init failed\n");
        goto shutdown;
    }
    xv_log_memory_budget("after gfx");

#ifdef XV_RUN_RECOMP
    /* Run the recompiled Halo engine on its own big-stack thread (core 0).  It owns guest memory
     * and drives the kernel scheduler; xv_present() renders each frame it records synchronously. */
    {
        SceUID eng = sceKernelCreateThread("xv_recomp", xv_recomp_thread, XV_THREAD_PRIORITY,
                                           2 * 1024 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        if (eng < 0) { XV_LOG("recomp thread create failed: 0x%08X\n", eng); goto shutdown; }
        sceKernelStartThread(eng, 0, NULL);
        sceKernelWaitThreadEnd(eng, NULL, NULL);
        sceKernelDeleteThread(eng);
        XV_LOG("recompiled engine finished after %u frames\n", g_gfx.frame_counter);
    }
#else
    if (xv_xram_init() != 0) {          /* §1.2: the 64 MB guest block, GXM-mapped */
        XV_LOG("guest RAM init failed\n");
        goto shutdown;
    }
    xv_log_memory_budget("after init");

    /* Core 1: render pump.  Core 0: guest scheduler.  Nothing else runs Xbox code. */
    SceUID pump = sceKernelCreateThread("xv_pump", xv_pump_thread, XV_THREAD_PRIORITY,
                                        XV_PUMP_THREAD_STACK, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    SceUID guest = sceKernelCreateThread("xv_guest", xv_guest_thread, XV_THREAD_PRIORITY,
                                         XV_GUEST_THREAD_STACK, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
    if (pump < 0 || guest < 0) {
        XV_LOG("thread creation failed: pump=0x%08X guest=0x%08X\n", pump, guest);
        goto shutdown;
    }
    sceKernelStartThread(pump, 0, NULL);
    sceKernelStartThread(guest, 0, NULL);

    /* The main thread just waits for the guest to finish, then tears down. */
    sceKernelWaitThreadEnd(guest, NULL, NULL);
    g_running = 0;
    sceKernelWaitThreadEnd(pump, NULL, NULL);
    sceKernelDeleteThread(guest);
    sceKernelDeleteThread(pump);
    XV_LOG("rendered %u frames\n", g_gfx.frame_counter);
#endif

shutdown:
    /* Exit protocol (verified on hardware + Vita3K): drain the GPU and the display
     * queue, detach the framebuffer, and let sceKernelExitProcess() reclaim every
     * memblock and GXM object.  Tearing objects down by hand while the display still
     * scans out the last frame produces a GPU fault (the console needs a hard reset),
     * so the explicit teardown below is only compiled in for the in-process reload
     * case where GXM has to survive the guest (not used yet). */
    xv_gfx_finish();
    sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    sceDisplayWaitVblankStart();
#ifdef XV_FULL_TEARDOWN
    xv_xram_shutdown();
    xv_gfx_shutdown();
#endif
    XV_LOG("Xita runtime exiting\n");
    sceKernelExitProcess(0);
    return 0;
}
