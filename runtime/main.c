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
 *   §3.4  Scheduler   : the mock uses xk_sched on core 0. Recompiled guest fibers
 *                       use separate kernel threads and semaphore baton handoffs,
 *                       so only one executes guest code at a time. The bootstrap
 *                       requests core 0; actual fiber affinity is logged with
 *                       XV_THREADS=1. The render/present pump requests core 1.
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

#ifdef XV_RUN_RECOMP
#include <psp2/common_dialog.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

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
#include "xv_vertex_upload.h"
#include "xv_vertex_prepare.h"
#include "xv_scene.h"
#include "xv_quality_settings.h"
#include "xv_ui_gxm.h"
#include "xv_layouts.h"        /* generated: recompiled-shader layouts (gen_layouts.py) */

/* ======================================================================================
 *  Configuration
 * ==================================================================================== */

#include "xv_log.h"
#include "xv_cpu.h"
#include "xv_frame_pacer.h"
#include "xv_render_profile.h"
#include "xv_render_target.h"
#ifdef XV_RUN_RECOMP
#include <psp2/ctrl.h>
#include "dashboard/xv_dash.h"
#include "xv_remote.h"
#include "xv_update.h"
#include <psp2/appmgr.h>
static unsigned g_update_quiesced, g_recomp_finished;
static int g_update_slot=-1;
#endif
#define XV_LOG(...)                 xv_logf("[xv] " __VA_ARGS__)
#define XV_SCENE_CENSUS_IMPLEMENTATION
#include "xv_scene_census.h"

#define XV_XRAM_SIZE                (64u * 1024u * 1024u)     /* Xbox physical RAM     */
#define XV_GUEST_MASK               0x03FFFFFFu               /* 26 bits = 64 MB       */

#define XV_DISPLAY_WIDTH            960
#define XV_DISPLAY_HEIGHT           544
#define XV_DISPLAY_STRIDE           1024                      /* pixels                */
#define XV_DISPLAY_BUFFER_COUNT     3
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

/* Uncached GPU uploads are published by their owning thread. */
#include "xv_gpu_upload.h"
#include "xv_frame_slots.h"

/* Host services used by the D3D HLE (xv_d3d.h). */
void *xv_guest_ptr(uint32_t guest_addr)              { return xv_gpu_ptr(guest_addr); }


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
    unsigned old_slot, tracked;
} xv_display_data_t;
static uint32_t g_display_free[XV_DISPLAY_BUFFER_COUNT];
static uint32_t g_display_queued, g_display_released;
static volatile unsigned *g_notifications;

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
    unsigned                render_width, render_height;
    SceGxmRenderTarget      *scaled_target;
    xv_memblock_t           scaled_mem[XV_DISPLAY_BUFFER_COUNT], scale_quad;
    SceGxmColorSurface      scaled_surface[XV_DISPLAY_BUFFER_COUNT];
    SceGxmTexture           scaled_texture[XV_DISPLAY_BUFFER_COUNT];
    xv_vshader_t            scale_vs;
    xv_fshader_t            scale_fs;

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
#ifdef XV_RUN_RECOMP
    xv_remote_frame(dd->address, XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT, XV_DISPLAY_STRIDE);
#endif
    /* XV_FB_DUMP=1: write each presented frame as ux0:data/xita/fb/NNN.ppm (A8B8G8R8 -> RGB) so the exact
     * on-screen image can be inspected off-device (e.g. what shows at a level-load transition). Ring of 240. */
    { static int on = -1; if (on < 0) { const char *e = getenv("XV_FB_DUMP"); on = e ? atoi(e) : 0; if (on) sceIoMkdir("ux0:data/xita/fb", 0777); }
      if (on && dd->address) { static unsigned fn; char path[64]; char hdr[32]; char row[XV_DISPLAY_WIDTH * 3];
          sceClibSnprintf(path, sizeof path, "ux0:data/xita/fb/%03u.ppm", fn % 240u); fn++;
          SceUID f = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
          if (f >= 0) { int hl = sceClibSnprintf(hdr, sizeof hdr, "P6\n%u %u\n255\n", (unsigned)XV_DISPLAY_WIDTH, (unsigned)XV_DISPLAY_HEIGHT); sceIoWrite(f, hdr, hl);
              const uint8_t *px = (const uint8_t *)dd->address;
              for (unsigned y = 0; y < XV_DISPLAY_HEIGHT; ++y) { const uint8_t *pr = px + (size_t)y * XV_DISPLAY_STRIDE * 4;
                  for (unsigned x = 0; x < XV_DISPLAY_WIDTH; ++x) { row[x*3+0] = pr[x*4+0]; row[x*3+1] = pr[x*4+1]; row[x*3+2] = pr[x*4+2]; }
                  sceIoWrite(f, row, XV_DISPLAY_WIDTH * 3); }
              sceIoClose(f); } } }
    sceDisplayWaitVblankStart();
    if (dd->tracked) {
        __atomic_store_n(&g_display_free[dd->old_slot], 1, __ATOMIC_RELEASE);
        __atomic_add_fetch(&g_display_released, 1, __ATOMIC_RELEASE);
    }

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

    g_notifications = sceGxmGetNotificationRegion();
    if (!g_notifications) { XV_LOG("GXM notification region unavailable\n"); return -1; }
#ifdef XV_SCENE_CENSUS
    SceKernelMemBlockInfo notification_info={0};notification_info.size=sizeof notification_info;
    int notification_info_result=sceKernelGetMemBlockInfoByAddr((void *)g_notifications,&notification_info);
    XV_LOG("[scene-census-region] result %08X region %p mappedBase %p mappedSize %u memoryType %X type %X access %X; mapping metadata only, notification capacity not inferred\n",
        (unsigned)notification_info_result,(void *)g_notifications,notification_info.mappedBase,(unsigned)notification_info.mappedSize,
        (unsigned)notification_info.memoryType,(unsigned)notification_info.type,(unsigned)notification_info.access);
    xv_sc_init(g_notifications,XV_SCENE_CENSUS_NOTIFICATION_WORDS);
#endif

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
    rt.multisampleMode      = SCE_GXM_MULTISAMPLE_NONE;
    rt.multisampleLocations = 0;
    rt.driverMemBlock       = -1;          /* let GXM allocate its own tiling memory */
    err = xv_render_target_create(&rt, UINT32_MAX, &g->render_target, NULL, "display");
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
    g->render_width = XV_DISPLAY_WIDTH; g->render_height = XV_DISPLAY_HEIGHT;
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

/* Resolution resources belong to the pump and are replaced only after draining
 * published work. Draws consume immutable per-slot vertex/index snapshots. */
static void xv_gfx_free_scale(xv_gfx_t *g)
{
    if (g->scale_fs.prog) xv_fshader_unload(&g->scale_fs);
    if (g->scale_vs.prog) xv_vshader_unload(&g->scale_vs);
    xv_gpu_free(&g->scale_quad);
    for (unsigned i = 0; i < XV_DISPLAY_BUFFER_COUNT; i++) xv_gpu_free(&g->scaled_mem[i]);
    if (g->scaled_target) { sceGxmDestroyRenderTarget(g->scaled_target); g->scaled_target = NULL; }
    g->render_width = XV_DISPLAY_WIDTH; g->render_height = XV_DISPLAY_HEIGHT;
}

/* Called after the dashboard reloads settings, before either game worker starts.
 * Each reduced surface follows its corresponding display buffer through the
 * existing frame queue; no per-frame allocation or CPU image scaling. */
static void xv_gfx_configure_resolution_height(unsigned h)
{
    xv_gfx_t *g = &g_gfx;
    if (h != 360 && h != 400 && h != 480) {
        XV_LOG("render resolution: 960x544 (native)\n"); return;
    }
    const unsigned w = xv_render_width(h);
    SceGxmRenderTargetParams rp = {0};
    rp.width = w; rp.height = h;
    rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE; rp.driverMemBlock = -1;
    if (xv_render_target_create(&rp, UINT32_MAX, &g->scaled_target, NULL, "scaled") != SCE_OK) goto fail;
    for (unsigned i = 0; i < XV_DISPLAY_BUFFER_COUNT; i++) {
        void *p = xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, w * h * 4,
                    SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE, "xv_scaled", &g->scaled_mem[i]);
        if (!p) goto fail;
        memset(p, 0, w * h * 4);
        if (sceGxmColorSurfaceInit(&g->scaled_surface[i], XV_COLOR_FORMAT,
                SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, w, h, w, p) != SCE_OK) goto fail;
        if (sceGxmTextureInitLinearStrided(&g->scaled_texture[i], p,
                SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, w, h, w * 4) != SCE_OK) goto fail;
        sceGxmTextureSetMinFilter(&g->scaled_texture[i], SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetMagFilter(&g->scaled_texture[i], SCE_GXM_TEXTURE_FILTER_LINEAR);
        sceGxmTextureSetUAddrMode(&g->scaled_texture[i], SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&g->scaled_texture[i], SCE_GXM_TEXTURE_ADDR_CLAMP);
    }
    if (xv_vshader_load(&g->scale_vs, &xv_vs_test_tex) != 0 ||
        xv_fshader_load(&g->scale_fs, "app0:shaders/xv_tex0.frag.gxp", &g->scale_vs, NULL) != 0) goto fail;
    struct scale_vertex { float x, y, z, u, v; uint32_t color; };
    struct scale_data { struct scale_vertex v[4]; uint16_t indices[6]; };
    const struct scale_data quad = {
        {{-1, 1, 0, 0, 0, 0xFFFFFFFFu}, {1, 1, 0, 1, 0, 0xFFFFFFFFu},
         {1, -1, 0, 1, 1, 0xFFFFFFFFu}, {-1, -1, 0, 0, 1, 0xFFFFFFFFu}},
        {0, 1, 2, 0, 2, 3}};
    void *q = xv_gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, sizeof quad,
                           SCE_GXM_MEMORY_ATTRIB_READ, "xv_scale_quad", &g->scale_quad);
    if (!q) goto fail;
    memcpy(q, &quad, sizeof quad);
    g->render_width = w; g->render_height = h;
    XV_LOG("render resolution: %ux%u, linear upscale to 960x544\n", w, h);
    return;
fail:
    xv_gfx_free_scale(g);
    XV_LOG("%up setup failed; using native 960x544\n", h);
}

static void xv_gfx_configure_resolution(void)
{ xv_gfx_configure_resolution_height(xv_quality_int("XV_RENDER_HEIGHT",544,360,544)); }

static int xv_gfx_upscale(xv_gfx_t *g, unsigned ui_frame, const SceGxmNotification *fence)
{
    int err = XV_RENDER_CALL(XV_RENDER_SCENE_BEGIN, sceGxmBeginScene(g->ctx, 0, g->render_target, NULL, NULL,
                        g->display_sync[g->back_index], &g->display_surface[g->back_index], NULL));
    if (err != SCE_OK) return err;
#ifdef XV_SCENE_CENSUS
    xv_sc_open(9,0,0);
#endif
    sceGxmSetViewport(g->ctx, 480, 480, 272, -272, 0.5f, 0.5f);
    sceGxmSetFrontDepthFunc(g->ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(g->ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetCullMode(g->ctx, SCE_GXM_CULL_NONE);
    xv_shader_bind(g->ctx, &g->scale_vs, &g->scale_fs);
    const void *streams[] = {g->scale_quad.base};
    xv_vshader_set_streams(g->ctx, &g->scale_vs, streams);
    const float identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    void *ub;
    if (xv_vshader_begin_constants(g->ctx, &g->scale_vs, &ub) == 0)
        xv_vshader_set_constants(ub, &g->scale_vs, 0, 4, identity);
    if (g->scale_fs.p_atest && XV_RENDER_CALL(XV_RENDER_FRAGMENT_UNIFORM, sceGxmReserveFragmentDefaultUniformBuffer(g->ctx, &ub)) == SCE_OK) {
        const float disabled[4] = {0,7,0,0};
        sceGxmSetUniformDataF(ub, g->scale_fs.p_atest, 0, 4, disabled);
    }
    sceGxmSetFragmentTexture(g->ctx, g->scale_fs.tex_index[0], &g->scaled_texture[g->back_index]);
    XV_RENDER_CALL(XV_RENDER_DRAW, sceGxmDraw(g->ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16,
               (const uint8_t *)g->scale_quad.base + 4 * 24, 6));
    xv_ui_gxm_replay_settings(g->ctx, ui_frame);
#ifdef XV_SCENE_CENSUS
    fence=xv_sc_end(9,UINT32_MAX,UINT32_MAX,fence,SC_FINAL);
#endif
    int ended=XV_RENDER_END(9, sceGxmEndScene(g->ctx, NULL, fence));
#ifdef XV_SCENE_CENSUS
    xv_sc_ended(ended);
#endif
    return ended;
}

#ifdef XV_RUN_RECOMP
static int g_net_dialog;
extern int xv_net_startup(void (*draw)(void));
extern void xv_net_shutdown(void);
#endif
/* Visibility draws end with the world scene. Its fragment fence may publish
 * query values before the upscale completes, but only the final fence releases
 * packet storage. Both paths preserve the same scenes and draw order. */
static int xv_gfx_end_scenes(xv_gfx_t *g, unsigned ui_frame,
    const SceGxmNotification *fence, const SceGxmNotification *visibility_fence)
{
    int scaled = g->scaled_target != NULL;
    const SceGxmNotification *world_fence=scaled?visibility_fence:fence;
#ifdef XV_SCENE_CENSUS
    world_fence=xv_sc_end(0,UINT32_MAX,UINT32_MAX,world_fence,scaled?SC_SCALED:SC_FINAL);
#endif
    int ended=XV_RENDER_END(0, sceGxmEndScene(g->ctx, NULL, world_fence));
#ifdef XV_SCENE_CENSUS
    xv_sc_ended(ended);
#endif
    if (ended != SCE_OK) return -1;
    if (scaled && xv_gfx_upscale(g, ui_frame, fence) != SCE_OK) { XV_LOG("upscale failed\n"); return -1; }
    return 0;
}
static int xv_gfx_render_frame(uint32_t mesh_frame, unsigned ui_frame,
    const SceGxmNotification *fence, const SceGxmNotification *visibility_fence)
{
    xv_gfx_t *g = &g_gfx;
    (void)mesh_frame;
    /* Wait only for CPU copies belonging to this packet, before opening any
     * scene. Core 0 can upload while the guest records and core 1 submits. */
    if (g->hle_ready && mesh_frame != UINT32_MAX)
        xv_vertex_upload_wait(mesh_frame % XV_FRAME_SLOTS);
    int scaled = g->scaled_target != NULL;

#ifdef XV_RUN_RECOMP
    if (g->hle_ready) {                 /* prepare the previous completed frame before opening any scene */
        uint32_t mf0 = mesh_frame;
        if (mf0 != 0xFFFFFFFFu) xv_d3d_visibility_prepare(g->ctx,mf0,g->render_width,g->render_height);
        if (mf0 != 0xFFFFFFFFu && xv_d3d_uses_previous_frame(mf0)) {
            /* Previous-frame fragments precede this frame on the same context.
             * Keep the surface alive through display-slot ownership; do not
             * block the CPU before sampling it in the later fragment pass. */
            SceGxmTexture previous = {0};
            if (sceGxmTextureInitLinearStrided(&previous, g->display_mem[g->front_index].base,
                    SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT,
                    XV_DISPLAY_STRIDE * 4) == SCE_OK) {
                sceGxmTextureSetMinFilter(&previous, SCE_GXM_TEXTURE_FILTER_LINEAR);
                sceGxmTextureSetMagFilter(&previous, SCE_GXM_TEXTURE_FILTER_LINEAR);
                sceGxmTextureSetUAddrMode(&previous, SCE_GXM_TEXTURE_ADDR_CLAMP);
                sceGxmTextureSetVAddrMode(&previous, SCE_GXM_TEXTURE_ADDR_CLAMP);
            }
            /* Clear a failed descriptor too: an older front buffer may now be
             * the active render target after the display buffers rotate. */
            xv_d3d_SetPreviousFrameTexture(&previous);
        }
    }
    int rtt = g->hle_ready && mesh_frame != 0xffffffffu && xv_d3d_has_render_targets(mesh_frame);
    if (rtt) {
        SceGxmTexture scene = {0};
        if (scaled) scene = g->scaled_texture[g->back_index];
        else if (sceGxmTextureInitLinearStrided(&scene, g->display_mem[g->back_index].base,
                SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT,
                XV_DISPLAY_STRIDE * 4) == SCE_OK) {
            sceGxmTextureSetMinFilter(&scene, SCE_GXM_TEXTURE_FILTER_LINEAR);
            sceGxmTextureSetMagFilter(&scene, SCE_GXM_TEXTURE_FILTER_LINEAR);
            sceGxmTextureSetUAddrMode(&scene, SCE_GXM_TEXTURE_ADDR_CLAMP);
            sceGxmTextureSetVAddrMode(&scene, SCE_GXM_TEXTURE_ADDR_CLAMP);
        }
        xv_d3d_SetSceneBackbufferTexture(&scene);
        int depth_tail_readonly = 0;
#ifdef XV_DEPTH_STORE
        /* After RTT replay, this branch only sets viewport, appends the owned
         * settings panel (native output) and ends the world scene. Scaled
         * settings draw in a separate depthless scene. Keep unknown dialogs on
         * the original policy. The replay separately proves its own overlay. */
        depth_tail_readonly = !g_net_dialog && xv_ui_gxm_depth_tail_readonly(ui_frame);
#endif
        if (xv_d3d_render_targets(g->ctx, mesh_frame, scaled ? g->scaled_target : g->render_target,
                scaled ? NULL : g->display_sync[g->back_index],
                scaled ? &g->scaled_surface[g->back_index] : &g->display_surface[g->back_index],
                &g->depth_surface, g->render_width, g->render_height, depth_tail_readonly) < 0) return -1;
    } else
#endif
    {
        int err = XV_RENDER_CALL(XV_RENDER_SCENE_BEGIN, sceGxmBeginScene(g->ctx, 0, scaled ? g->scaled_target : g->render_target, NULL, NULL,
                                   scaled ? NULL : g->display_sync[g->back_index],
                                   scaled ? &g->scaled_surface[g->back_index] : &g->display_surface[g->back_index], &g->depth_surface));
        if (err != SCE_OK) {
            XV_LOG("sceGxmBeginScene failed: 0x%08X\n", err);
            return -1;
        }
#ifdef XV_SCENE_CENSUS
        xv_sc_open(0,sceGxmDepthStencilSurfaceGetForceLoadMode(&g->depth_surface),sceGxmDepthStencilSurfaceGetForceStoreMode(&g->depth_surface));
#endif
    }

    /* Replay this frame's recorded commands and owned GPU-visible uploads. */
#ifndef XV_RUN_RECOMP
    if (g->hle_ready)
        xv_d3d_render(g->ctx, g->frame_counter);
#else
    if (g->hle_ready) {
        uint32_t mf = mesh_frame;
        /* Halo's VS emits D3D clip space; same viewport the UI replay uses */
        sceGxmSetViewport(g->ctx, g->render_width * 0.5f, g->render_width * 0.5f,
                          g->render_height * 0.5f, -(float)g->render_height * 0.5f, 0.5f, 0.5f);
        if (mf != 0xFFFFFFFFu && !rtt) xv_d3d_render(g->ctx, mf);
    }
#endif
#ifdef XV_RUN_RECOMP
    if (!rtt)
#endif
    { extern void xv_ui_gxm_replay_frame(SceGxmContext *, unsigned, unsigned, unsigned);
      xv_ui_gxm_replay_frame(g->ctx, g->render_width, g->render_height, ui_frame); }

    if (!scaled) xv_ui_gxm_replay_settings(g->ctx, ui_frame);
    if (xv_gfx_end_scenes(g, ui_frame, fence, visibility_fence) < 0) return -1;

#ifdef XV_RUN_RECOMP
    if (g_net_dialog) {
        SceCommonDialogUpdateParam up = {0};
        up.renderTarget.colorSurfaceData = g->display_mem[g->back_index].base;
        up.renderTarget.surfaceType = SCE_GXM_COLOR_SURFACE_LINEAR;
        up.renderTarget.colorFormat = XV_COLOR_FORMAT;
        up.renderTarget.width = XV_DISPLAY_WIDTH; up.renderTarget.height = XV_DISPLAY_HEIGHT;
        up.renderTarget.strideInPixels = XV_DISPLAY_STRIDE;
        up.displaySyncObject = g->display_sync[g->back_index];
        sceCommonDialogUpdate(&up);
    }
#endif
    /* Present: heartbeat keeps the GPU/display in step, then queue the flip. */
    sceGxmPadHeartbeat(&g->display_surface[g->back_index], g->display_sync[g->back_index]);

    xv_display_data_t dd = {g->display_mem[g->back_index].base, g->front_index, fence != NULL};
    if (dd.tracked) __atomic_store_n(&g_display_free[g->back_index], 0, __ATOMIC_RELEASE);
    xv_render_profile_stage(XV_RENDER_DISPLAY_QUEUE);
    int queued = sceGxmDisplayQueueAddEntry(g->display_sync[g->front_index],
                               g->display_sync[g->back_index], &dd);
    if (queued < 0) {
        if (dd.tracked) __atomic_store_n(&g_display_free[g->back_index], 1, __ATOMIC_RELEASE);
        XV_LOG("display queue failed %08X\n", queued);
        return queued;
    }
    if (dd.tracked) g_display_queued++;
    xv_render_profile_stage(XV_RENDER_SUBMIT);

    g->front_index = g->back_index;
    g->back_index  = (g->back_index + 1) % XV_DISPLAY_BUFFER_COUNT;
    g->frame_counter++;
    return 0;
}

#ifdef XV_RUN_RECOMP
static void xv_net_dialog_draw(void)
{
    xv_gfx_render_frame(0xFFFFFFFFu, UINT32_MAX, NULL, NULL);
    sceDisplayWaitVblankStart();
}
#endif


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

/* Keep the paired service available through GPU/display drains so a stalled
 * shutdown can still report its stage and logs. It stops before LoadExec. */
static void xv_finish_for_exit(void)
{
#ifdef XV_RUN_RECOMP
    if(xv_update_requested()) {
        int display=scePowerRequestDisplayOn();
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        XV_LOG("update: wake display rc %08X; GPU drain begins\n",display);
        xv_update_progress(XV_UPDATE_GPU_DRAIN);
    }
#endif
    xv_gfx_finish();
#ifdef XV_RUN_RECOMP
    xv_update_progress(XV_UPDATE_DISPLAY_DRAIN);
    if(xv_update_requested())XV_LOG("update: GPU drain complete; detaching display\n");
#endif
    sceDisplaySetFrameBuf(NULL,SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
    sceDisplayWaitVblankStart();
#ifdef XV_RUN_RECOMP
    if(xv_update_requested())XV_LOG("update: display detached; draining periodic log writer before network stop\n");
#endif
    /* Producers and pump are quiesced. Keep remote status/log retrieval alive
     * through a blocked or failed drain. No queue/sink lock is held here. */
    for(;;) {
        int drained=xv_log_shutdown(5000000);
        if(drained==XV_LOG_OK) break;
#ifdef XV_RUN_RECOMP
        if(xv_update_requested()) { sceKernelDelayThread(100000); continue; }
#endif
        sceClibPrintf("[xv] exit log drain failed %d; pending output not claimed durable\n",drained);
        break;
    }
#ifdef XV_RUN_RECOMP
    xv_update_progress(XV_UPDATE_NETWORK_STOP);
    xv_remote_stop();
    xv_net_shutdown();
#endif
}

static void __attribute__((unused)) xv_gfx_shutdown(void)
{
    xv_gfx_t *g = &g_gfx;
    XV_LOG("shutdown: finish\n");
    xv_gfx_finish();
    xv_gfx_free_scale(g);
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
#include "xv_frame_events.h"
#include "xv_packet_timing.h"
#include "xv_benchmark.h"
static xv_frame_events g_frame_events = { -1 };
static uint32_t g_resolution_request,g_resolution_result;
static uint32_t g_settings_frame_period;
static volatile int      g_running         = 1;
static struct {
    uint32_t mesh, ui;
    SceGxmNotification fence, visibility_fence;
    uint64_t started_us, visibility_us;
    int failed, visibility_completed;
#if XV_QUERY_PREFIX_PUBLISH
    unsigned prefix_history_blocked;
    uint32_t prefix_history_done;
#endif
#ifdef XV_QUERY_BOUNDARY
    int query_boundary;
#endif
#if XV_GPU_PACKET_TIMING
    xv_packet_timing timing;
#endif
} g_packets[XV_FRAME_TICKETS];
static xv_slot_owner g_mesh_owners[XV_FRAME_SLOTS], g_ui_owners[XV_FRAME_SLOTS];
static uint32_t g_frame_submitted; /* pump-owned, distinct from GPU completion */
static int g_pipeline_override = -1;
static int g_pipeline_configured; /* Written before starting either game worker. */
static int g_early_visibility_override = -1;
static int xv_early_visibility_enabled(void)
{
    int override = __atomic_load_n(&g_early_visibility_override, __ATOMIC_ACQUIRE);
    /* Experimental until matched physical comparisons establish its value. */
    return override < 0 ? 0 : override;
}
#ifdef XV_QUERY_BOUNDARY
#ifndef XV_QUERY_BOUNDARY_DEFAULT
#define XV_QUERY_BOUNDARY_DEFAULT 0
#endif
#if XV_QUERY_BOUNDARY_DEFAULT != 0 && XV_QUERY_BOUNDARY_DEFAULT != 1
#error XV_QUERY_BOUNDARY_DEFAULT must be 0 or 1
#endif
/* Initialized before threads; subsequent comparison changes remain drained. */
static int g_query_boundary_override = XV_QUERY_BOUNDARY_DEFAULT;
int xv_query_boundary_available(void) { return 1; }
int xv_query_boundary_enabled(void)
{ return __atomic_load_n(&g_query_boundary_override,__ATOMIC_ACQUIRE); }
#endif
static unsigned g_slot_waits;
static uint64_t g_slot_wait_us;
static int xv_pipeline_enabled(void)
{
    int override = __atomic_load_n(&g_pipeline_override, __ATOMIC_ACQUIRE);
    return override < 0 ? g_pipeline_configured : override;
}
static void xv_pipeline_configure(void)
{
    const char *value = getenv("XV_TRIPLE_BUFFER");
    /* Same boolean parsing as the dashboard; missing means conservative mode. */
    g_pipeline_configured = value && atoi(value) != 0;
    XV_LOG("triple buffering: %s; GPU completion retains ownership of each slot\n",
           g_pipeline_configured ? "on (experimental)" : "off (single-flight)");
}
/* Recording thread, after draining old frames. */
void xv_settings_pipeline(int enabled) { g_pipeline_configured=!!enabled; }
void xv_settings_frame_cap(unsigned cap)
{ __atomic_store_n(&g_settings_frame_period,cap?1000000u/cap:0,__ATOMIC_RELEASE); }
void xv_pipeline_override(int enabled)
{ __atomic_store_n(&g_pipeline_override, enabled < 0 ? -1 : !!enabled, __ATOMIC_RELEASE); }
void xv_present_drain(void);
volatile uint64_t        xv_pump_us_acc     = 0;          /* render time on the pump, for the frame-time log */

/* D3DDevice_Swap() publishes a frame, then acquires the next recording slot.
 * Only an occupied slot parks this fiber; other guest fibers remain runnable. */
/* Called only by the serialized recording thread. Slot owners refer to GPU
 * completion, never just CPU submission or a display callback. */
void xv_present(void)
{
#ifdef XV_RUN_RECOMP
    xv_cpu_guest_poll();
    extern uint32_t xv_ui_gxm_mesh_frame(void);
    extern unsigned xv_ui_gxm_published_frame(void), xv_ui_gxm_record_frame(void);
    /* Single-flight is the recovery default. The pre-launch setting or a test
     * override permits overlap; GPU slot ownership applies to either mode. */
    if (!xv_pipeline_enabled()) xv_present_drain();
    uint32_t ticket = g_frame_requested + 1u;
    unsigned q = ticket & (XV_FRAME_TICKETS - 1u);
    uint32_t mesh = xv_ui_gxm_mesh_frame();
    unsigned ui = xv_ui_gxm_published_frame();
    g_packets[q].mesh = mesh; g_packets[q].ui = ui;
    if (mesh != UINT32_MAX) g_mesh_owners[mesh % XV_FRAME_SLOTS] = (xv_slot_owner){ticket,1};
    if (ui < XV_FRAME_SLOTS) g_ui_owners[ui] = (xv_slot_owner){ticket,1};
    __atomic_store_n(&g_frame_requested, ticket, __ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
    unsigned next_mesh = xv_d3d_record_slot(), next_ui = xv_ui_gxm_record_frame();
    uint64_t start = 0;
    for (;;) {
        uint32_t done = __atomic_load_n(&g_frame_completed, __ATOMIC_ACQUIRE);
        if (!xv_slot_busy(&g_mesh_owners[next_mesh],done) &&
            !xv_slot_busy(&g_ui_owners[next_ui],done)) break;
        if (!start) { start = sceKernelGetProcessTimeWide(); g_slot_waits++; }
        /* Park just this guest fiber; other runnable guest threads may work. */
        extern void xk_sleep_us(uint64_t);
        xk_sleep_us(100);
    }
    if (start) g_slot_wait_us += sceKernelGetProcessTimeWide() - start;
    if (ticket % 60u == 0) {
        XV_LOG("[frame-acquire] 60 presents: %u busy-slot waits %llu us; async %d\n",
            g_slot_waits,(unsigned long long)g_slot_wait_us,xv_pipeline_enabled());
        g_slot_waits=0; g_slot_wait_us=0;
    }
    if(xv_update_requested()) {
        /* End of the serialized recording call: all published uploads have
         * owners. Drain them, then park without yielding the guest token.
         * The main thread stops the pump before replacing this process. */
        xv_present_drain();
        XV_LOG("update: recording paused after frame %u\n",ticket);
        xv_update_progress(XV_UPDATE_RECORDING_DRAINED);
        __atomic_store_n(&g_update_quiesced,1,__ATOMIC_RELEASE);
        for(;;)sceKernelDelayThread(10000);
    }
#else
    uint32_t ticket = __atomic_add_fetch(&g_frame_requested, 1, __ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
    xk_wait_word(&g_frame_completed, ticket);
    xv_d3d_BeginFrame();
#endif
}
/* Block until every published frame has been rendered (texture-pool purges, shutdown). */
void xv_present_drain(void)
{
    while ((int32_t)(__atomic_load_n(&g_frame_completed, __ATOMIC_ACQUIRE) - __atomic_load_n(&g_frame_requested, __ATOMIC_ACQUIRE)) < 0)
        xv_frame_events_wait(&g_frame_events,XV_FRAME_COMPLETED,100);
}

/* Called after draining, before recording rings rotate. Only the pump changes
 * GXM resources, and it acknowledges before the next frame is published. */
void xv_benchmark_optimizations(int enabled)
{
    extern void xv_flare_barrier(unsigned) __attribute__((weak));
    extern void xv_flare_defer_override(int) __attribute__((weak));
    xv_present_drain();
    /* Diagnostic controllers change only their observer/report mode here. */
    if (xv_benchmark_compare_log_writer()||xv_benchmark_compare_diagnostic_poll()||xv_benchmark_compare_light_census())return;
    if (xv_benchmark_compare_depth_store()) {
#ifdef XV_DEPTH_STORE
        xv_depth_store_override(enabled);
#endif
        return;
    }
    if (xv_benchmark_compare_vertex_blocks()) {
        xv_vertex_blocks_override(enabled);
        return;
    }
    if (xv_benchmark_compare_query_boundary()) {
#ifdef XV_QUERY_BOUNDARY
        __atomic_store_n(&g_query_boundary_override,enabled>0,__ATOMIC_RELEASE);
#endif
        return;
    }
    if (xv_benchmark_compare_polygon_edge()) {
        extern void xv_native_polygon_edge_override(int) __attribute__((weak));
        if(xv_native_polygon_edge_override)xv_native_polygon_edge_override(enabled);
        return;
    }
    if (xv_benchmark_compare_clip_region()) {
        extern void xv_native_clip_region_override(int) __attribute__((weak));
        if(xv_native_clip_region_override)xv_native_clip_region_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_pose()) {
        extern void xv_object_pose_override(int) __attribute__((weak));
        if(xv_object_pose_override)xv_object_pose_override(enabled);
        return;
    }
    if (xv_benchmark_compare_material_packet()) {
        extern void xv_material_packet_override(int) __attribute__((weak));
        if(xv_material_packet_override)xv_material_packet_override(enabled);
        return;
    }
    if (xv_benchmark_compare_index_reuse()) {
        xv_d3d_index_reuse_override(enabled);
        return;
    }
    if (xv_benchmark_compare_blend_replace()) {
        extern void xv_d3d_blend_replace_override(int);
        xv_d3d_blend_replace_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_quat()) {
        extern void xv_object_quat_override(int) __attribute__((weak));
        if(xv_object_quat_override)xv_object_quat_override(enabled);
        return;
    }
    if (xv_benchmark_compare_model_hierarchy()) {
        extern void xv_model_hierarchy_override(int) __attribute__((weak));
        if(xv_model_hierarchy_override)xv_model_hierarchy_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_point()) {
        extern void xv_object_point_override(int) __attribute__((weak));
        if(xv_object_point_override)xv_object_point_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_holds()) {
        extern void xv_object_holds_override(int) __attribute__((weak));
        if(xv_object_holds_override)xv_object_holds_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_collect()) {
        extern void xv_object_collect_override(int) __attribute__((weak));
        if(xv_object_collect_override)xv_object_collect_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_wait()) {
        extern void xv_object_wait_override(int) __attribute__((weak));
        if(xv_object_wait_override)xv_object_wait_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_lock()) {
        extern void xv_object_lock_override(int) __attribute__((weak));
        if(xv_object_lock_override)xv_object_lock_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_math()) {
        extern void xv_object_math_override(int) __attribute__((weak));
        if(xv_object_math_override)xv_object_math_override(enabled);
        return;
    }
    if (xv_benchmark_compare_depth_prepare()) {
        xv_depth_prepare_override(enabled);
        return;
    }
    if (xv_benchmark_compare_vertex_prepare()) {
        xv_vertex_prepare_override(enabled,16384);
        return;
    }
    if (xv_benchmark_compare_object_jobs()) {
        extern void xv_object_jobs_override(int) __attribute__((weak));
        if(xv_object_jobs_override)xv_object_jobs_override(enabled);
        return;
    }
    if (xv_benchmark_compare_prep_bundle()) {
        extern void xv_matrix_neon_override(int) __attribute__((weak));
        extern void xv_object_scan_override(int) __attribute__((weak));
        /* Admission requires both native helpers. Apply as one drained guest
         * boundary; -1 restores each member's own configured value. */
        if(xv_matrix_neon_override && xv_object_scan_override) {
            xv_matrix_neon_override(enabled);
            xv_object_scan_override(enabled);
            xv_d3d_texture_state_override(enabled);
        }
        return;
    }
    if (xv_benchmark_compare_guest_phases()) {
        extern void xv_phase_capture_override(int) __attribute__((weak));
        extern void xv_object_jobs_override(int) __attribute__((weak));
        /* The scope collector requires serialized guest execution. Object jobs
         * already decline during tracing; hold that same scheduling policy in
         * both surrounding arms so their difference measures tracing overhead.
         * Restore the configured policy on completion, cancellation or lost view.
         * Present has joined guest jobs before this drained boundary. */
        if(xv_phase_capture_override) {
            if(xv_object_jobs_override)xv_object_jobs_override(enabled<0?-1:0);
            xv_phase_capture_override(enabled);
        }
        return;
    }
    if (xv_benchmark_compare_snapshot_worker()) {
        xv_snapshot_worker_override(enabled);
        return;
    }
    if (xv_benchmark_compare_guest_affinity()) {
        extern void xv_guest_affinity_override(int) __attribute__((weak));
        if(xv_guest_affinity_override)xv_guest_affinity_override(enabled);
        return;
    }
    if (xv_benchmark_compare_flare_query_overlap()) {
        extern void xv_flare_query_overlap_override(int) __attribute__((weak));
        if(xv_flare_barrier)xv_flare_barrier(0);
        if(xv_flare_query_overlap_override)xv_flare_query_overlap_override(enabled);
        return;
    }
    if (xv_benchmark_compare_hle_dispatch()) {
        extern void xv_hle_dispatch_override(int) __attribute__((weak));
        if(xv_hle_dispatch_override)xv_hle_dispatch_override(enabled);
        return;
    }
    if (xv_benchmark_compare_object_scan()) {
        extern void xv_object_scan_override(int) __attribute__((weak));
        if(xv_object_scan_override)xv_object_scan_override(enabled);
        return;
    }
    if (xv_benchmark_compare_texture_state()) {
        xv_d3d_texture_state_override(enabled);
        return;
    }
    if (xv_benchmark_compare_point_math()) {
        extern void xv_point_math_override(int) __attribute__((weak));
        if(xv_point_math_override)xv_point_math_override(enabled);
        return;
    }
    if (xv_benchmark_compare_matrix_neon()) {
        extern void xv_matrix_neon_override(int) __attribute__((weak));
        if(xv_matrix_neon_override)xv_matrix_neon_override(enabled);
        return;
    }
    if (xv_benchmark_compare_early_visibility()) {
        __atomic_store_n(&g_early_visibility_override, enabled < 0 ? -1 : !!enabled, __ATOMIC_RELEASE);
        return;
    }
    if (xv_benchmark_compare_object_basis()) {
#ifdef XV_NATIVE_OBJECT_BASIS
        extern void xv_object_basis_override(int);
        xv_object_basis_override(enabled);
#endif
        return;
    }
    if (xv_benchmark_compare_model_palette()) {
#ifdef XV_NATIVE_MODEL_PALETTE
        extern void xv_model_palette_override(int);
        xv_model_palette_override(enabled);
#endif
        return;
    }
    if (xv_benchmark_compare_vertex_worker()) {
        xv_vertex_worker_override(enabled);
        return;
    }
    if (xv_benchmark_compare_vertex_references()) {
        xv_vertex_references_override(enabled);
        return;
    }
    if (xv_benchmark_compare_native_bounds()) {
        extern void xv_native_bounds_override(int);
        xv_native_bounds_override(enabled);
        return;
    }
    if (xv_benchmark_compare_vertex_copy()) {
        xv_vertex_copy_override(enabled);
        return;
    }
    if (xv_benchmark_compare_draw_scan()) {
        xv_d3d_draw_scan_override(enabled);
        return;
    }
    /* Retire retained results before switching the exact-result schedule. */
    if (xv_flare_barrier) xv_flare_barrier(0); /* XV_FLARE_NEXT */
    if (xv_flare_defer_override) xv_flare_defer_override(enabled);
}
#ifdef XV_LIGHT_QUERY_CENSUS
uint64_t xv_benchmark_boundary_time(void) {return sceKernelGetProcessTimeWide();}
#endif
void xv_benchmark_present(void)
{
#ifdef XV_LIGHT_QUERY_CENSUS
    /* Foreign native callers must not inspect the owner state machine while
     * this diagnostic is armed. OFF adds only this atomic flag branch. */
    extern unsigned xv_light_census_present_requested;
    if(__atomic_load_n(&xv_light_census_present_requested,__ATOMIC_RELAXED)&&!xv_benchmark_light_census_boundary_ok()) {
        xv_benchmark_light_census_boundary_lost(sceKernelGetProcessTimeWide());return;
    }
#endif
    if(!xv_benchmark_active())return;
    extern int xd3d_benchmark_view(float view[6]);
    float view[6]={0};int valid=xd3d_benchmark_view(view);
    unsigned height=xv_benchmark_step(sceKernelGetProcessTimeWide(),g_gfx.render_height,valid,view);
    if(!height)return;
    xv_present_drain(); /* Resolution resources require an empty queue. */
    __atomic_store_n(&g_resolution_result,0,__ATOMIC_RELAXED);
    __atomic_store_n(&g_resolution_request,height,__ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
    unsigned actual;
    while(!(actual=__atomic_load_n(&g_resolution_result,__ATOMIC_ACQUIRE)))
        xv_frame_events_wait(&g_frame_events,XV_FRAME_COMPLETED,100);
    xv_benchmark_applied(sceKernelGetProcessTimeWide(),actual);
}
unsigned xv_settings_resolution(unsigned height)
{
    __atomic_store_n(&g_resolution_result,0,__ATOMIC_RELAXED);
    __atomic_store_n(&g_resolution_request,height,__ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
    unsigned actual;
    while(!(actual=__atomic_load_n(&g_resolution_result,__ATOMIC_ACQUIRE)))
        xv_frame_events_wait(&g_frame_events,XV_FRAME_COMPLETED,100);
    return actual;
}
/* The idle pump owns configuration; there can be no outstanding frame here. */
static int xv_pump_resolution(void)
{
    unsigned height=__atomic_load_n(&g_resolution_request,__ATOMIC_ACQUIRE);
    if(!height)return 0;
    if(g_gfx.render_height!=height) {
        sceGxmFinish(g_gfx.ctx);
        xv_gfx_free_scale(&g_gfx);
        xv_gfx_configure_resolution_height(height);
    }
    __atomic_store_n(&g_resolution_request,0,__ATOMIC_RELAXED);
    __atomic_store_n(&g_resolution_result,g_gfx.render_height,__ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_COMPLETED);
    return 1;
}

/* Only called on the recording thread, while published work is drained. */
void xv_render_target_drain(void)
{
    xv_present_drain();
    sceGxmFinish(g_gfx.ctx);
}

/* Poll and retire the oldest completed packet. The GPU's fragment notification
 * protects every scene in this packet, including a final upscale. Display
 * release is tracked separately and cannot retire geometry or queries. */
static unsigned g_retired_count, g_max_pending;
static uint64_t g_completion_us;
static unsigned g_early_visibility_count;
static uint64_t g_early_visibility_us, g_visibility_tail_us;
#ifdef XV_QUERY_BOUNDARY
static unsigned g_boundary_queries,g_boundary_fallbacks,g_boundary_before_final;
static uint64_t g_boundary_query_us,g_boundary_tail_us;
#endif
#include "xv_query_publication.h"
#if XV_GPU_PACKET_TIMING
static xv_packet_timing_totals g_packet_timing;
static int xv_pump_observe(unsigned q)
{
    xv_packet_timing *p=&g_packets[q].timing;
    if(p->ticket!=g_packets[q].fence.value || !p->submitted)p->invalid=1;
    /* A prior observation may belong to a younger packet. Never move its
     * first-ready timestamp forward to ordered retirement. Keep the original
     * readiness load even then, so diagnostics cannot change fence policy. */
    if (p->ready || p->invalid) {
        int ready=__atomic_load_n(g_packets[q].fence.address,__ATOMIC_ACQUIRE)==g_packets[q].fence.value;
        if(p->ready && !ready)p->invalid=1;
        return ready;
    }
    uint64_t before=sceKernelGetProcessTimeWide();
    int ready=__atomic_load_n(g_packets[q].fence.address,__ATOMIC_ACQUIRE)==g_packets[q].fence.value;
    uint64_t after=sceKernelGetProcessTimeWide();
    xv_packet_timing_observe(p,before,after,ready);
    return ready;
}
static void xv_pump_observe_younger(void)
{
    uint32_t done=__atomic_load_n(&g_frame_completed,__ATOMIC_RELAXED);
    unsigned pending=g_frame_submitted-done;
    /* Tickets may wrap; queue capacity bounds this iteration independently of
     * unsigned ordering. The oldest is sampled by the normal retirement path. */
    if(pending>=XV_FRAME_TICKETS)return;
    for(unsigned i=2;i<=pending;i++) {
        unsigned q=(done+i)&(XV_FRAME_TICKETS-1u);
        if(!g_packets[q].failed && !g_packets[q].timing.ready)xv_pump_observe(q);
    }
}
#endif
static int xv_pump_retire(void)
{
    uint32_t done = __atomic_load_n(&g_frame_completed, __ATOMIC_RELAXED);
    if (done == g_frame_submitted) return 0;
    uint32_t ticket = done + 1u;
    unsigned q = ticket & (XV_FRAME_TICKETS - 1u);
    if (!g_packets[q].visibility_completed && g_packets[q].visibility_fence.address &&
        __atomic_load_n(g_packets[q].visibility_fence.address, __ATOMIC_ACQUIRE) == g_packets[q].visibility_fence.value) {
        g_packets[q].visibility_us = sceKernelGetProcessTimeWide();
#ifdef XV_QUERY_BOUNDARY
        /* A positive CPU interval alone does not prove a pending GPU tail. */
        if(g_packets[q].query_boundary && !g_packets[q].failed &&
           __atomic_load_n(g_packets[q].fence.address,__ATOMIC_ACQUIRE)!=g_packets[q].fence.value)
            g_boundary_before_final++;
#endif
        xv_d3d_visibility_complete(g_packets[q].mesh);
        g_packets[q].visibility_completed = 1;
        /* No ownership release here: upscale, settings and final fragments
         * may still read the frame's geometry, textures and UI snapshots. */
    }
    if (!g_packets[q].failed &&
#if XV_GPU_PACKET_TIMING
        !xv_pump_observe(q)) return 0;
#else
        __atomic_load_n(g_packets[q].fence.address, __ATOMIC_ACQUIRE) != g_packets[q].fence.value) return 0;
#endif
    uint64_t now = sceKernelGetProcessTimeWide();
    uint64_t elapsed = now - g_packets[q].started_us;
#ifdef XV_SCENE_CENSUS
    xv_sc_observe(done,g_frame_submitted);
    xv_sc_fold(ticket,g_packets[q].failed);
#endif
    if (g_gfx.hle_ready && g_packets[q].mesh != UINT32_MAX) {
        if (!g_packets[q].visibility_completed) {
#ifdef XV_QUERY_BOUNDARY
            g_boundary_fallbacks+=g_packets[q].query_boundary;
#endif
            xv_d3d_visibility_complete(g_packets[q].mesh);
        }
        xv_d3d_check_geometry(g_packets[q].mesh);
    }
    if (g_packets[q].visibility_completed) {
#ifdef XV_QUERY_BOUNDARY
        if(g_packets[q].query_boundary) {
            g_boundary_queries++;
            g_boundary_query_us+=g_packets[q].visibility_us-g_packets[q].started_us;
            g_boundary_tail_us+=now-g_packets[q].visibility_us;
        } else
#endif
        {
        g_early_visibility_count++;
        g_early_visibility_us += g_packets[q].visibility_us - g_packets[q].started_us;
        g_visibility_tail_us += now - g_packets[q].visibility_us;
        }
    }
    g_completion_us += elapsed;
#if XV_GPU_PACKET_TIMING
    /* The guest may reuse this packet as soon as completed is published. */
    xv_packet_timing_fold(&g_packet_timing,&g_packets[q].timing);
#endif
    __atomic_store_n(&g_frame_completed,ticket,__ATOMIC_RELEASE);
    xv_frame_events_signal(&g_frame_events,XV_FRAME_COMPLETED);
    if (++g_retired_count == 60) {
#ifdef XV_SCENE_CENSUS
        xv_sc_report();
#endif
#ifdef XV_NATIVE_COLLISION_VERTICES
        /* Passive observations only: no reset, mode change or guest drain. */
        extern unsigned xv_collision_vertices_count;
        extern int xv_collision_vertices_enabled(void);
        static unsigned last_collision_vertices;
        unsigned vertices=__atomic_load_n(&xv_collision_vertices_count,__ATOMIC_RELAXED);
        XV_LOG("[collision-vertices] enabled %d; %u admissions since last report; cumulative %u (asynchronous snapshot)\n",
            xv_collision_vertices_enabled(),vertices-last_collision_vertices,vertices);
        last_collision_vertices=vertices;
#endif
#ifdef XV_NATIVE_SEGMENT_SPHERE
        extern unsigned xv_segment_sphere_count;
        extern int xv_segment_sphere_enabled(void);
        static unsigned last_segment_sphere;
        unsigned spheres=__atomic_load_n(&xv_segment_sphere_count,__ATOMIC_RELAXED);
        XV_LOG("[segment-sphere] enabled %d; %u admissions since last report; cumulative %u (asynchronous snapshot)\n",
            xv_segment_sphere_enabled(),spheres-last_segment_sphere,spheres);
        last_segment_sphere=spheres;
#endif
        XV_LOG("[frame-retire] 60 frames: completion latency %.3f ms/frame; max pending %u; GPU notification retirement (overlaps guest/submission)\n",
            g_completion_us / 60000.0, g_max_pending);
#if XV_GPU_PACKET_TIMING
        xv_packet_timing_report(&g_packet_timing);
#endif
        if (g_early_visibility_count) XV_LOG("[frame-query] %u world fragment fences: query latency %.3f ms/frame; remaining final-fence tail %.3f ms/frame; scheduled polling times, overlaps CPU/GPU\n",
            g_early_visibility_count, g_early_visibility_us / (1000.0 * g_early_visibility_count),
            g_visibility_tail_us / (1000.0 * g_early_visibility_count));
#ifdef XV_QUERY_BOUNDARY
        xv_d3d_query_boundary_report();
#if XV_QUERY_PREFIX_PUBLISH
        XV_LOG("[query-prefix-publish] %u younger prefixes published, %u with older final pending, %u history-blocked packets; final ownership retained\n",
            g_query_prefix_published,g_query_prefix_pending_tail,g_query_prefix_history_blocked);
        g_query_prefix_published=g_query_prefix_pending_tail=g_query_prefix_history_blocked=0;
#endif
        XV_LOG("[frame-query-boundary] %u prefix notifications observed (%u before final) / %u final fallbacks; query latency %.3f ms/query packet; remaining final tail %.3f ms/query packet; scheduled observations, storage still retained\n",
            g_boundary_queries,g_boundary_before_final,g_boundary_fallbacks,g_boundary_queries?g_boundary_query_us/(1000.0*g_boundary_queries):0.0,
            g_boundary_queries?g_boundary_tail_us/(1000.0*g_boundary_queries):0.0);
        g_boundary_queries=g_boundary_fallbacks=g_boundary_before_final=0;g_boundary_query_us=g_boundary_tail_us=0;
#endif
#ifdef XV_DEPTH_STORE
        xv_d3d_depth_store_report();
#endif
        g_early_visibility_count=0; g_early_visibility_us=0; g_visibility_tail_us=0;
        g_retired_count=0; g_completion_us=0; g_max_pending=0;
    }
    return 1;
}
/* Core 1 owns all GXM submission. It can submit another packet while the prior
 * notification is pending; it polls fences while display slots/pacing are busy. */
static int xv_pump_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    XV_LOG("pump: triple slots, fragment notification retirement; configured queue mode; requested core 1\n");
    xv_cpu_log_thread("render-pump");
#if XV_GPU_PACKET_TIMING
    XV_LOG("[gpu-packet] enabled: pump-owned submission and bracketed notification polling; pipeline/wait policy unchanged\n");
#endif
    for (unsigned i=0;i<XV_DISPLAY_BUFFER_COUNT;i++)
        __atomic_store_n(&g_display_free[i],i!=g_gfx.front_index,__ATOMIC_RELEASE);
    xv_cpu_poll(sceKernelGetProcessTimeWide());
    int cap=xv_quality_int("XV_FRAME_CAP",0,0,60);
    uint32_t period=cap>0 ? 1000000u/(unsigned)cap : 0;
    __atomic_store_n(&g_settings_frame_period,period,__ATOMIC_RELEASE);
    uint64_t next_frame=0;
    while (g_running) {
        uint32_t updated=__atomic_load_n(&g_settings_frame_period,__ATOMIC_ACQUIRE);
        if(updated!=period) {period=updated;next_frame=0;}
#if XV_GPU_PACKET_TIMING
        xv_pump_observe_younger();
#endif
#ifdef XV_SCENE_CENSUS
        xv_sc_observe(__atomic_load_n(&g_frame_completed,__ATOMIC_RELAXED),g_frame_submitted);
#endif
        while (xv_pump_retire()) {}
#if XV_QUERY_PREFIX_PUBLISH
        xv_pump_query_prefixes();
#endif
        uint32_t requested=__atomic_load_n(&g_frame_requested,__ATOMIC_ACQUIRE);
        uint32_t done=__atomic_load_n(&g_frame_completed,__ATOMIC_ACQUIRE);
        if (g_frame_submitted != requested) {
            uint64_t now=sceKernelGetProcessTimeWide();
            int display_busy=!__atomic_load_n(&g_display_free[g_gfx.back_index],__ATOMIC_ACQUIRE) ||
                g_display_queued-__atomic_load_n(&g_display_released,__ATOMIC_ACQUIRE)>=XV_DISPLAY_MAX_PENDING;
            if (display_busy || now<next_frame) { sceKernelDelayThread(100); continue; }
            uint32_t ticket=g_frame_submitted+1u;
            unsigned q=ticket&(XV_FRAME_TICKETS-1u);
#ifndef XV_RUN_RECOMP
            g_packets[q].mesh=ticket-1u; g_packets[q].ui=UINT32_MAX;
#endif
            g_packets[q].fence=(SceGxmNotification){g_notifications+q,ticket};
            g_packets[q].visibility_fence=(SceGxmNotification){NULL,0};
            g_packets[q].visibility_completed=0;
#if XV_QUERY_PREFIX_PUBLISH
            g_packets[q].prefix_history_blocked=0;
#endif
#ifdef XV_QUERY_BOUNDARY
            g_packets[q].query_boundary=g_gfx.hle_ready && g_packets[q].mesh!=UINT32_MAX &&
                xv_d3d_query_boundary_prepare(g_packets[q].mesh,xv_query_boundary_enabled());
#endif
            if (
#ifdef XV_QUERY_BOUNDARY
                g_packets[q].query_boundary ||
#endif
                (xv_early_visibility_enabled() && g_gfx.scaled_target && g_gfx.hle_ready &&
                g_packets[q].mesh != UINT32_MAX && xv_d3d_has_visibility(g_packets[q].mesh))) {
                g_packets[q].visibility_fence=(SceGxmNotification){g_notifications+XV_FRAME_TICKETS+q,ticket};
                /* This ticket slot's prior owner has retired. Initialize its
                 * separate query word, including the uint32_t ticket wrap. */
                __atomic_store_n(g_packets[q].visibility_fence.address, ~ticket, __ATOMIC_RELEASE);
            }
#ifdef XV_QUERY_BOUNDARY
            if(g_packets[q].query_boundary)xv_d3d_query_boundary_arm(g_packets[q].mesh,&g_packets[q].visibility_fence);
#endif
            g_packets[q].started_us=now;
#ifdef XV_SCENE_CENSUS
            int sc_tracked=g_gfx.hle_ready && g_packets[q].mesh!=UINT32_MAX && !g_net_dialog;
            xv_sc_begin(ticket,g_packets[q].mesh,now,g_frame_submitted-done,sc_tracked);
            if(sc_tracked)xv_d3d_scene_census_plan(g_packets[q].mesh,g_gfx.render_width,g_gfx.render_height,g_gfx.scaled_target!=NULL);
#endif
#if XV_GPU_PACKET_TIMING
            xv_packet_timing_begin(&g_packets[q].timing,ticket,now,
                __atomic_load_n(g_packets[q].fence.address,__ATOMIC_ACQUIRE)==ticket);
#endif
            xv_cpu_poll(now);
            xv_render_profile_begin(g_packets[q].mesh);
            xv_gpu_write_barrier();
            int err=xv_gfx_render_frame(g_packets[q].mesh,g_packets[q].ui,&g_packets[q].fence,
#ifdef XV_QUERY_BOUNDARY
                g_packets[q].query_boundary ? NULL :
#endif
                g_packets[q].visibility_fence.address ? &g_packets[q].visibility_fence : NULL);
#if XV_GPU_PACKET_TIMING
            xv_packet_timing_end(&g_packets[q].timing,sceKernelGetProcessTimeWide(),err<0);
#endif
            g_packets[q].failed=err<0;
#ifdef XV_SCENE_CENSUS
            xv_sc_submitted(err<0);
#endif
            if (err<0) {
                /* A failed EndScene may never signal. Exceptional cleanup only:
                 * finish any accepted work before releasing its storage. */
                XV_LOG("[frame-fence] submission failed %08X; draining failed packet\n",err);
                sceGxmFinish(g_gfx.ctx);
            }
            g_frame_submitted=ticket;
            unsigned pending=ticket-done;
            if (pending>g_max_pending) g_max_pending=pending;
            next_frame=now+period;
            xv_pump_us_acc+=sceKernelGetProcessTimeWide()-now;
            xv_render_profile_end(); /* submission duration, not GPU latency */
        } else if (done!=g_frame_submitted) {
            sceKernelDelayThread(100); /* bounded polling, no full-GPU wait */
        } else {
            if(xv_pump_resolution())continue;
            xv_frame_events_wait(&g_frame_events,XV_FRAME_REQUESTED,200);
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
    xv_cpu_log_thread("guest-bootstrap");
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
    __atomic_store_n(&g_recomp_finished,1,__ATOMIC_RELEASE);
    return 0;
}
#endif

static void xv_load_settings(void)
{
    /* The user-facing dashboard file wins over emulator fallback settings. */
    const char *files[] = {"ux0:data/xita/env.txt", "ux0:data/xita/xita.cfg"};
    for (unsigned i = 0; i < 2; ++i) {
        FILE *f = fopen(files[i], "r");
        if (!f) continue;
        char line[2048];
        while (fgets(line, sizeof line, f)) {
            if (!strchr(line, '\n') && !feof(f)) {
                int c; while ((c = fgetc(f)) != EOF && c != '\n') {}
                continue; /* Do not execute a truncated setting. */
            }
            char *key = line; while (isspace((unsigned char)*key)) key++;
            if (*key == '#' || *key == ';' || !*key) continue;
            char *eq = strchr(key, '='); if (!eq) continue;
            char *end = eq; while (end > key && isspace((unsigned char)end[-1])) end--;
            *end = 0;
            char *value = eq + 1; while (isspace((unsigned char)*value)) value++;
            end = value;
            while (*end && *end != '#' && *end != ';' && *end != '\r' && *end != '\n') end++;
            while (end > value && isspace((unsigned char)end[-1])) end--;
            *end = 0;
            if (*key) { setenv(key, value, 1); XV_LOG("cfg %s=%s\n", key, value); }
        }
        fclose(f);
    }
}

#ifdef XV_RUN_RECOMP
typedef struct {
    xv_gfx_t *gfx;
    uint64_t frame_started, sample_started, draw_us, copy_us, wait_us;
    unsigned frames;
} xv_dashboard_platform;

static int xv_dashboard_poll(void *userdata, xv_dash_input *input)
{
    xv_dashboard_platform *p = userdata;
    if(xv_update_requested())return 1;
    p->frame_started = sceKernelGetProcessTimeWide();
    if (!p->sample_started) p->sample_started = p->frame_started;
    SceCtrlData pad = {0};
    int rc = sceCtrlPeekBufferPositive(0, &pad, 1);
    if (rc < 0) return rc;
    if (!rc) pad.lx = pad.ly = pad.rx = pad.ry = 128;
    xv_remote_pad(&pad.buttons, &pad.lx, &pad.ly, &pad.rx, &pad.ry);
    const unsigned masks[] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_CROSS, SCE_CTRL_CIRCLE};
    for (unsigned i = 0; i < 6; ++i) if (pad.buttons & masks[i]) input->buttons |= 1u << i;
    input->lx = (int)pad.lx - 128; input->ly = (int)pad.ly - 128;
    return 0;
}
static int xv_dashboard_present(void *userdata, xv_dash_framebuffer *fb)
{
    xv_dashboard_platform *p = userdata;
    xv_gfx_t *g = p->gfx;
    uint64_t drawn = sceKernelGetProcessTimeWide();
    /* Alpha blending reads its destination repeatedly: keep it in cached RAM.
     * Stream the completed image into the inactive CDRAM buffer only once.
     * Both ends of this copy are CPU addresses; the GPU never reads the canvas. */
    void *display_pixels = g->display_mem[g->back_index].base;
    memcpy(display_pixels, fb->pixels, (size_t)fb->pitch * fb->height * sizeof(uint32_t));
    xv_remote_frame(fb->pixels, fb->width, fb->height, fb->pitch);
    uint64_t copied = sceKernelGetProcessTimeWide();
    SceDisplayFrameBuf display = {0};
    display.size = sizeof display; display.base = display_pixels;
    display.pitch = fb->pitch; display.width = fb->width; display.height = fb->height;
    display.pixelformat = XV_DISPLAY_PIXEL_FORMAT;
    int rc = sceDisplaySetFrameBuf(&display, SCE_DISPLAY_SETBUF_NEXTFRAME);
    if (rc < 0) return rc;
    rc = sceDisplayWaitVblankStart(); if (rc < 0) return rc;
    g->front_index = g->back_index;
    g->back_index = (g->back_index + 1u) % XV_DISPLAY_BUFFER_COUNT;
    /* Keep fb->pixels pointing at the cached canvas when display buffers swap. */
    uint64_t shown = sceKernelGetProcessTimeWide();
    if(g_update_slot>=0 && p->frames==2) {
        const char *remote=getenv("XV_REMOTE_TEST"),*adhoc=getenv("XV_NET_ADHOC");
        int need_remote=remote&&!strcmp(remote,"1")&&!(adhoc&&atoi(adhoc)==1);
        int rc=need_remote&&!xv_remote_ready()?-1:xv_update_confirm((unsigned)g_update_slot);
        XV_LOG("update: dashboard boot confirmation slot %d rc %d\n",g_update_slot,rc);
        g_update_slot=-1;
    }
    p->draw_us += drawn - p->frame_started;
    p->copy_us += copied - drawn;
    p->wait_us += shown - copied;
    if (++p->frames == 120) {
        uint64_t elapsed = shown - p->sample_started;
        unsigned fps10 = elapsed ? (unsigned)(p->frames * 10000000ull / elapsed) : 0;
        XV_LOG("dashboard: %u.%u fps; draw %u us, copy %u us, wait %u us/frame\n",
               fps10 / 10, fps10 % 10, (unsigned)(p->draw_us / p->frames),
               (unsigned)(p->copy_us / p->frames), (unsigned)(p->wait_us / p->frames));
        p->frames = 0; p->draw_us = p->copy_us = p->wait_us = 0;
        p->sample_started = shown;
    }
    return 0;
}
static void xv_dashboard_update_status(char *text,unsigned size) {xv_update_status(text,size);}
static int xv_dashboard_start(void)
{
    const char *skip = getenv("XV_DASHBOARD");
    if (skip && !atoi(skip)) return 0; /* automation bypass */
    sceIoMkdir("ux0:data", 0777); sceIoMkdir("ux0:data/xita", 0777);
    if (sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE) < 0) return -1;
    const unsigned bytes = ALIGN_UP(XV_DISPLAY_STRIDE * XV_DISPLAY_HEIGHT * sizeof(uint32_t), 4096);
    SceUID canvas_uid = sceKernelAllocMemBlock("xv_dash_canvas", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW, bytes, NULL);
    if (canvas_uid < 0) { XV_LOG("dashboard: canvas allocation failed: 0x%08X\n", canvas_uid); return -1; }
    void *canvas = NULL;
    if (sceKernelGetMemBlockBase(canvas_uid, &canvas) < 0) { sceKernelFreeMemBlock(canvas_uid); return -1; }
    xv_dashboard_platform platform = {.gfx = &g_gfx};
    xv_dash_config cfg = {
        .framebuffer = {canvas, XV_DISPLAY_WIDTH, XV_DISPLAY_HEIGHT, XV_DISPLAY_STRIDE},
        .userdata = &platform, .poll = xv_dashboard_poll, .present = xv_dashboard_present, .simple_launcher = 1,
        .update_status=xv_dashboard_update_status, .update_action=xv_update_request
    };
    xv_dash_result choice;
    XV_LOG("dashboard: ready; cached canvas %u KB; waiting for Launch Game\n", bytes / 1024);
    int rc = xv_dash_run(&cfg, &choice);
    sceKernelFreeMemBlock(canvas_uid); /* No dashboard memory survives into Halo. */
    if(xv_update_requested())return 1;
    if (rc != 0) return -1;
    xv_load_settings(); /* All game consumers initialize after this hand-off. */
    XV_LOG("dashboard: Launch Game; settings applied\n");
    return 0;
}
#endif

static void xv_configure_cpu_clock(void)
{
    int requested = xv_quality_int("XV_CPU_MHZ", 444, 444, 500);
    if (requested != 500) requested = 444;
    int rc = scePowerSetArmClockFrequency(requested);
    int actual = scePowerGetArmClockFrequency();
    if (requested == 500 && (rc < 0 || actual < 500)) {
        scePowerSetArmClockFrequency(444);
        XV_LOG("CPU clock: 500 MHz unavailable (rc %08X, reported %d); requesting 444 MHz\n", rc, actual);
    }
    XV_LOG("game clocks: requested CPU %d, effective cpu %d bus %d gpu %d xbar %d MHz\n",
        requested, scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
        scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    XV_LOG("Xita runtime starting\n");
    /* Homebrew boots at 333/111 MHz; ask for the full clocks (CPU 444, bus 222, GPU 222, GPU xbar 166). */
    scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
    XV_LOG("clocks: cpu %d bus %d gpu %d xbar %d MHz\n", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(), scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
    xv_log_memory_budget("boot");
    xv_load_settings();
    (void)xv_log_async_start(); /* selected build default, after environment/config */
#ifdef XV_NATIVE_COLLISION_VERTICES
    { extern int xv_collision_vertices_enabled(void);
      XV_LOG("[collision-vertices] process-start mode %d; gameplay selection fixed for this launch\n",xv_collision_vertices_enabled()); }
#endif
#ifdef XV_NATIVE_SEGMENT_SPHERE
    { extern int xv_segment_sphere_enabled(void);
      XV_LOG("[segment-sphere] process-start mode %d; gameplay selection fixed for this launch\n",xv_segment_sphere_enabled()); }
#endif
    { extern int xv_collision_traversal_enabled(void) __attribute__((weak));
      if (xv_collision_traversal_enabled)
          XV_LOG("[collision-traversal] process-start mode %d; typed decisions with original continuations; no admission counter\n",xv_collision_traversal_enabled()); }

    uint64_t gfx_started = sceKernelGetProcessTimeWide();
    if (xv_gfx_init() != 0) {           /* GXM first: sceGxmMapMemory needs it live */
        XV_LOG("graphics init failed\n");
        goto shutdown;
    }
    xv_log_memory_budget("after gfx");
    XV_LOG("graphics startup: %u ms\n", (unsigned)((sceKernelGetProcessTimeWide() - gfx_started) / 1000));

#ifdef XV_RUN_RECOMP
    xv_update_init();
    for(int i=0;i<argc;i++)if(!strcmp(argv[i],"--xita-slot=0")||!strcmp(argv[i],"--xita-slot=1"))g_update_slot=argv[i][12]-'0';
    xv_remote_start();
    int dashboard_result=xv_dashboard_start();
    if(dashboard_result>0)goto shutdown;
    if (dashboard_result < 0) {
        XV_LOG("dashboard failed; game was not started\n");
        goto shutdown;
    }
    xv_configure_cpu_clock();
    g_net_dialog = 1;
    int net_result = xv_net_startup(xv_net_dialog_draw);
    g_net_dialog = 0;
    if (net_result < 0) goto shutdown;
    xv_gfx_configure_resolution();
    xv_pipeline_configure(); /* Dashboard edits loaded; workers have not started. */
    xv_d3d_configure_render_preparation();
#ifdef XV_DEPTH_STORE
    XV_LOG("[depth-store] process-start mode %d available %d; read-only continuation proof; loads and final ownership retained\n",xv_depth_store_enabled(),xv_depth_store_available());
#endif
#ifdef XV_QUERY_BOUNDARY
    XV_LOG("[query-boundary] process-start mode %d; exact prefix publication at existing scene ends; final ownership retained\n",xv_query_boundary_enabled());
#endif
#ifdef XV_NATIVE_OBJECT_COLLECT
    { extern int xv_object_collect_enabled(void);
      XV_LOG("[object-collect] process-start mode %d; startup default with explicit environment override\n",xv_object_collect_enabled()); }
#endif
    { extern void xv_cutout_override(int); xv_cutout_override(0); }
    xv_frame_events_init(&g_frame_events,xv_quality_int("XV_FRAME_EVENTS",1,0,1));
    XV_LOG("frame handoff: %s\n",g_frame_events.id>=0 ? "event notifications" : "poll fallback");
    /* Bootstrap the guest scheduler on core 0. Guest fibers have their own kernel
     * threads with default affinity; the render pump submits frames asynchronously. */
    {
        SceUID pump = sceKernelCreateThread("xv_pump", xv_pump_thread, XV_THREAD_PRIORITY,
                                            XV_PUMP_THREAD_STACK, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
        if (pump < 0) { XV_LOG("pump thread create failed: 0x%08X\n", pump); xv_frame_events_close(&g_frame_events); goto shutdown; }
        sceKernelStartThread(pump, 0, NULL);
        SceUID eng = sceKernelCreateThread("xv_recomp", xv_recomp_thread, XV_THREAD_PRIORITY,
                                           2 * 1024 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_0, NULL);
        if (eng < 0) {
            XV_LOG("recomp thread create failed: 0x%08X\n", eng); g_running=0;
            xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
            sceKernelWaitThreadEnd(pump,NULL,NULL); sceKernelDeleteThread(pump);
            xv_frame_events_close(&g_frame_events); goto shutdown;
        }
        int engine_started=sceKernelStartThread(eng,0,NULL)>=0;
        /* Vita3K's WaitThreadEnd ignores its timeout. Observe explicit owner
         * signals without joining a deliberately parked recording fiber. */
        if(engine_started) {
            while(!__atomic_load_n(&g_recomp_finished,__ATOMIC_ACQUIRE) &&
                  !__atomic_load_n(&g_update_quiesced,__ATOMIC_ACQUIRE))sceKernelDelayThread(100000);
            if(!__atomic_load_n(&g_update_quiesced,__ATOMIC_ACQUIRE))sceKernelWaitThreadEnd(eng,NULL,NULL);
        } else XV_LOG("recomp thread start failed\n");
        if(!__atomic_load_n(&g_update_quiesced,__ATOMIC_ACQUIRE))sceKernelDeleteThread(eng);
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
        { extern void xv_object_jobs_shutdown(void); xv_object_jobs_shutdown(); }
#endif
        xv_present_drain(); g_running = 0;
        xv_frame_events_signal(&g_frame_events,XV_FRAME_REQUESTED);
        sceKernelWaitThreadEnd(pump, NULL, NULL); sceKernelDeleteThread(pump);
        xv_frame_events_close(&g_frame_events);
        xv_update_progress(XV_UPDATE_PUMP_STOPPED);
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
    xv_finish_for_exit();
#ifdef XV_FULL_TEARDOWN
    xv_xram_shutdown();
    xv_gfx_shutdown();
#endif
#ifdef XV_RUN_RECOMP
    if(xv_update_requested()) {
        XV_LOG("update: handing off to boot helper\n");
        if(xv_log_flush_wait(5000000)!=XV_LOG_OK) {
            sceClibPrintf("[xv] update: final log sync failed; refusing launcher handoff\n");
            sceKernelExitProcess(1); return 1;
        }
        xv_update_progress(XV_UPDATE_LAUNCHER_HANDOFF);
        int rc=sceAppMgrLoadExec("app0:eboot.bin",NULL,NULL);
        if(rc>=0)for(;;)sceKernelDelayThread(100000);
        XV_LOG("update: launcher handoff failed %08X; exiting without replacing active slot\n",rc);
    }
#endif
    XV_LOG("Xita runtime exiting\n");
    if(xv_log_flush_wait(5000000)!=XV_LOG_OK)
        sceClibPrintf("[xv] exit: final log sync failed\n");
    sceKernelExitProcess(0);
    return 0;
}
