/*
 * xd3d.c - Xbox D3D8 (XDK 3925) object model + default "null" renderer, and a minimal DirectSound.
 *
 * The recompiled game calls D3DDevice_xxx / D3DResource_xxx through xv_hle_<name>(c).  This file owns the
 * guest-visible object model: D3DResource headers { Common, Data, Lock [, Format, Size] } allocated in guest
 * RAM with pixel/vertex storage in PHYSICAL memory (Data = physical address, CPU pointer = 0x80000000|Data),
 * exactly like the Xbox runtime, so game code that pokes at headers (Halo's cache resources, D3DResource_Register
 * fix-ups) behaves.  Rendering calls go through the xd3d_r_* hooks; the weak defaults here only count and log
 * (host harness).  The Vita runtime overrides them with GXM (xv_d3d.c bridge).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xk.h"
#include "xd3d.h"

#define D3DLOG(...) xk_os_log("[d3d] " __VA_ARGS__)

/* ---- Xbox D3D constants ---------------------------------------------------------------------- */
#define X_D3DCOMMON_REFCOUNT_MASK      0x0000FFFFu
#define X_D3DCOMMON_TYPE_VERTEXBUFFER  0x00000000u
#define X_D3DCOMMON_TYPE_INDEXBUFFER   0x00010000u
#define X_D3DCOMMON_TYPE_PUSHBUFFER    0x00020000u
#define X_D3DCOMMON_TYPE_PALETTE       0x00030000u
#define X_D3DCOMMON_TYPE_TEXTURE       0x00040000u
#define X_D3DCOMMON_TYPE_SURFACE       0x00050000u
#define X_D3DCOMMON_TYPE_FIXUP         0x00060000u
#define X_D3DCOMMON_D3DCREATED         0x01000000u
#define X_D3DCOMMON_ISLOCKED           0x02000000u

#define RES_COMMON(r)  X_M32(r)
#define RES_DATA(r)    X_M32((r) + 4)
#define RES_LOCK(r)    X_M32((r) + 8)
#define PC_FORMAT(r)   X_M32((r) + 12)
#define PC_SIZE(r)     X_M32((r) + 16)
#define SURF_PARENT(r) X_M32((r) + 20)

#define GUEST_PTR(phys) (0x80000000u | ((phys) & 0x03FFFFFFu))

/* the exported D3D globals inside the image (from halo_symbols.json) */
extern const uint32_t xv_d3d_g_pDevice_va;          /* D3D8__VAR__D3D_g_pDevice */
uint32_t g_xd3d_device;                             /* guest D3DDevice struct */
#define DEVICE_SIZE 0x4000
#define DEVICE_SWAPCALLBACK_OFFSET 0x24E8           /* D3DDevice__m_SwapCallback_OFFSET (3925) */

typedef enum { FMT_8 = 8, FMT_16 = 16, FMT_32 = 32, FMT_DXT1 = 4, FMT_DXT35 = 8 } fmt_bpp;
static unsigned fmt_bits(unsigned f)
{
    switch (f) {
    case 0x00: case 0x01: case 0x0B: case 0x13: case 0x19: case 0x1B: case 0x1F: return 8;
    case 0x0C: return 4;                                                                 /* DXT1 */
    case 0x0E: case 0x0F: return 8;                                                      /* DXT3/5 (per pixel) */
    case 0x06: case 0x07: case 0x12: case 0x1E: case 0x2A: case 0x2B: case 0x2E: case 0x2F: case 0x33: case 0x36:
    case 0x3A: case 0x3B: case 0x3C: case 0x3F: case 0x40: case 0x41: return 32;
    default: return 16;
    }
}
static int fmt_is_dxt(unsigned f) { return f == 0x0C || f == 0x0E || f == 0x0F; }
static int fmt_is_linear(unsigned f) { return (f >= 0x10 && f <= 0x1F && f != 0x19 && f != 0x1A) || (f >= 0x2E && f <= 0x31) || f == 0x35 || f == 0x36 || f == 0x37 || (f >= 0x3D && f <= 0x41) || f == 0x20 || f == 0x22; }
static unsigned log2u(unsigned v) { unsigned l = 0; while ((1u << l) < v) l++; return l; }

static uint32_t level_bytes(unsigned f, unsigned w, unsigned h, unsigned d)
{
    if (fmt_is_dxt(f)) { unsigned bw = (w + 3) / 4, bh = (h + 3) / 4; if (bw < 1) bw = 1; if (bh < 1) bh = 1; return bw * bh * (f == 0x0C ? 8 : 16) * d; }
    unsigned pitch = w * fmt_bits(f) / 8; if (fmt_is_linear(f)) pitch = (pitch + 63) & ~63u;
    return pitch * h * d;
}

static uint32_t new_header(uint32_t type, unsigned size)
{
    uint32_t h = xk_kalloc(size < 64 ? 64 : size);
    RES_COMMON(h) = 1 | type | X_D3DCOMMON_D3DCREATED;
    return h;
}

/* ---- device ------------------------------------------------------------------------------------ */
xd3d_state_t xd3d_state;
static struct { uint32_t prim; unsigned verts, begins_in_frame, setdata_in_frame; } g_im;
static float g_im_cur[16][4];
static xd3d_im_vtx g_im_v[1024];
/* Per-frame histogram of HLE entry points (host: XV_D3D_HIST=<frame> logs that frame's counts; also
 * dumped once on the Vita when the log asks).  Cheap enough to leave in: one strcmp-free table walk. */
#define XD3D_HIST_MAX 128
static struct { const char *name; unsigned n; } g_hist[XD3D_HIST_MAX]; static unsigned g_hist_n;
void xd3d_ds_check(const char *where, uint32_t eip);
static inline void xd3d_count(const char *name)
{
    static int on = -1; if (on < 0) on = (getenv("XV_D3D_HIST") != NULL) || (getenv("XV_DS_CHECK") != NULL);
    if (!on) return;                                           /* ~4000 calls/frame: only walk the table when asked */
    { static int dbg = -1; if (dbg < 0) dbg = getenv("XV_DS_CHECK") != NULL; if (dbg) xd3d_ds_check(name, 0); }
    for (unsigned i = 0; i < g_hist_n; ++i) if (g_hist[i].name == name) { g_hist[i].n++; return; }
    if (g_hist_n < XD3D_HIST_MAX) { g_hist[g_hist_n].name = name; g_hist[g_hist_n].n = 1; g_hist_n++; }
}
#define XD3D_COUNT(nm) xd3d_count(nm)
int  xd3d_hist_active(void);                                 /* true during the XV_D3D_HIST frame */
#define XD3D_RET(nm) do { if (xd3d_hist_active()) { D3DLOG("[hist] %s from %08X\n", nm, X_M32(c->r[4])); \
    char sb_[400]; int sn_ = 0; for (unsigned i_ = 0; i_ < 40 && sn_ < 380; ++i_) { uint32_t w_ = X_M32(c->r[4] + 4 * i_); if (w_ >= 0x11000 && w_ < 0x3A0000) sn_ += snprintf(sb_ + sn_, sizeof sb_ - sn_, " %X", w_); } \
    D3DLOG("[hist]   stack code ptrs:%s\n", sb_); } } while (0)
void xd3d_hist_dump(const char *tag)
{
    for (unsigned i = 0; i < g_hist_n; ++i) if (g_hist[i].n) D3DLOG("%s %5u %s\n", tag, g_hist[i].n, g_hist[i].name);
}
void xd3d_hist_reset(void) { for (unsigned i = 0; i < g_hist_n; ++i) g_hist[i].n = 0; }

void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n) __attribute__((weak));
void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n) { (void)prim; (void)v; (void)n; }
static void im_set2(const char *src, unsigned reg, float x, float y, float z, float w)
{
    static unsigned dbg;
    if (dbg < 30 && xd3d_frame() > 0) { dbg++; xk_os_log("[im] %s r%u = %.2f %.2f %.2f %.2f\n", src, reg & 15, x, y, z, w); }
    reg &= 15;
    g_im_cur[reg][0] = x; g_im_cur[reg][1] = y; g_im_cur[reg][2] = z; g_im_cur[reg][3] = w;
    if (reg == 0 && g_im.verts < 1024) memcpy(&g_im_v[g_im.verts++], g_im_cur, sizeof g_im_cur);   /* writing v0 completes a vertex */
    g_im.setdata_in_frame++;
}
#define im_set(...) im_set2("?", __VA_ARGS__)
static float f32arg(xctx *c, unsigned i) { float f; uint32_t v = X_ARG(i); memcpy(&f, &v, 4); return f; }
static struct { uint32_t vblank_cb, swap_cb; unsigned frame, draws, draws_total, clears; uint32_t backbuffer, depth; unsigned width, height; } g_dev;
static int g_hist_frame = -2;
static float g_vp_rows[4][4]; static unsigned g_vp_frame = 0xFFFFFFFFu;   /* view-projection of the frame's first world draw */
static struct { uint32_t m; unsigned n; uint32_t last; } g_hm[128]; static unsigned g_nhm;   /* NV2A methods poked in the hist frame */
int xd3d_hist_active(void)
{
    if (g_hist_frame == -2) { const char *e = getenv("XV_D3D_HIST"); g_hist_frame = e ? atoi(e) : -1; }
    return g_hist_frame >= 0 && (int)g_dev.frame + 1 == g_hist_frame;   /* the frame Present will number hist_frame */
}

void xd3d_r_present(unsigned frame, unsigned draws) __attribute__((weak));
void xd3d_r_present(unsigned frame, unsigned draws) { if (frame < 10 || frame % 60 == 0) D3DLOG("Present #%u (%u draws, %u clears)\n", frame, draws, g_dev.clears); }
void xd3d_r_draw(xctx *c, int indexed, uint32_t prim, uint32_t count, uint32_t data) __attribute__((weak));
void xd3d_r_draw(xctx *c, int indexed, uint32_t prim, uint32_t count, uint32_t data)
{
    (void)c;
    static unsigned logged; if (logged++ >= 40) return;
    uint32_t vs = xd3d_state.vs_handle, fnv = (vs & 1) ? X_M32((vs & ~1u) + 12) : 0;
    D3DLOG("Draw%s(prim %u, n %u, %08X) vs %08X%s%s fnv %08X vb %08X+%u tex %08X ps %08X cull %u z %u\n",
           indexed ? "Idx" : "", prim, count, data, vs, (vs & 1) ? "" : " FVF", "", fnv,
           xd3d_state.stream_vb[0] ? X_M32(xd3d_state.stream_vb[0] + 4) : 0, xd3d_state.stream_stride[0],
           xd3d_state.texture[0], xd3d_state.ps_def, xd3d_state.cull, xd3d_state.z_enable);
}
void xd3d_r_state(const char *what, uint32_t a, uint32_t b, uint32_t v) __attribute__((weak));
void xd3d_r_state(const char *what, uint32_t a, uint32_t b, uint32_t v) { static unsigned n; if (n++ < 40) D3DLOG("%s(%u, %u, %08X)\n", what, a, b, v); }

unsigned xd3d_frame(void) { return g_dev.frame; }

static void call_guest(xctx *c, uint32_t fn, uint32_t arg)   /* stdcall callback with one argument */
{
    X_PUSH32(arg); X_PUSH32(0xDEAD0010u); xv_call(c, fn);
}

/* HRESULT Direct3D_CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface) */
void xv_hle_Direct3D_CreateDevice(xctx *c)
{ XD3D_COUNT("Direct3D_CreateDevice");
    uint32_t pp = X_ARG(4);
    if (!g_xd3d_device) {
        g_xd3d_device = xk_kalloc(DEVICE_SIZE);
        g_dev.width = pp ? X_M32(pp) : 640; g_dev.height = pp ? X_M32(pp + 4) : 480;
        if (!g_dev.width) g_dev.width = 640; if (!g_dev.height) g_dev.height = 480;
        /* back buffer + depth surfaces: A8R8G8B8 / D24S8, linear */
        g_dev.backbuffer = new_header(X_D3DCOMMON_TYPE_SURFACE, 32);
        uint32_t pitch = (g_dev.width * 4 + 63) & ~63u;
        RES_DATA(g_dev.backbuffer) = xk_phys_alloc(pitch * g_dev.height, 4096, 0, 0, 1);
        PC_FORMAT(g_dev.backbuffer) = (0x12u << 8) | (2u << 4) | 1u;         /* LIN_A8R8G8B8, 2D */
        PC_SIZE(g_dev.backbuffer) = ((g_dev.width - 1) & 0xFFF) | (((g_dev.height - 1) & 0xFFF) << 12) | (((pitch / 64) - 1) << 24);
        g_dev.depth = new_header(X_D3DCOMMON_TYPE_SURFACE, 32);
        RES_DATA(g_dev.depth) = xk_phys_alloc(pitch * g_dev.height, 4096, 0, 0, 1);
        PC_FORMAT(g_dev.depth) = (0x2Eu << 8) | (2u << 4) | 1u;              /* LIN_D24S8 */
        PC_SIZE(g_dev.depth) = PC_SIZE(g_dev.backbuffer);
        D3DLOG("CreateDevice %ux%u, device struct at %08X\n", g_dev.width, g_dev.height, g_xd3d_device);
    }
    if (xv_d3d_g_pDevice_va) X_M32(xv_d3d_g_pDevice_va) = g_xd3d_device;
    if (X_ARG(5)) X_M32(X_ARG(5)) = g_xd3d_device;
    c->r[0] = 0; X_RET(6);
}
void xv_hle_D3DDevice_Release(xctx *c) { XD3D_COUNT("D3DDevice_Release"); c->r[0] = 0; X_RET(0); }
void xv_hle_D3D_SetPushBufferSize(xctx *c) { XD3D_COUNT("D3D_SetPushBufferSize"); c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_GetDeviceCaps(xctx *c) { XD3D_COUNT("D3DDevice_GetDeviceCaps"); memset(X_G(X_ARG(0)), 0, 0x150); X_M32(X_ARG(0) + 0x08) = 0x000E0000; X_M32(X_ARG(0) + 0x30) = 8; c->r[0] = 0; X_RET(1); }
void xv_hle_CMiniport_GetDisplayCapabilities(xctx *c) { XD3D_COUNT("CMiniport_GetDisplayCapabilities"); c->r[0] = 0x00000000; X_RET(0); }
void xv_hle_CDevice_InitializeFrameBuffers(xctx *c) { XD3D_COUNT("CDevice_InitializeFrameBuffers"); c->r[0] = 0; X_RET(1); }
void xv_hle_CDevice_KickOff(xctx *c) { XD3D_COUNT("CDevice_KickOff"); X_RET(0); }
void xv_hle_CDevice_MakeSpace(xctx *c) { XD3D_COUNT("CDevice_MakeSpace"); X_RET(0); }
void xv_hle_D3DDevice_KickPushBuffer(xctx *c) { XD3D_COUNT("D3DDevice_KickPushBuffer"); X_RET(0); }
void xv_hle_XMETAL_StartPush(xctx *c) { XD3D_COUNT("XMETAL_StartPush"); c->r[0] = 0; X_RET(1); }
void xv_hle_D3DDevice_IsBusy(xctx *c) { XD3D_COUNT("D3DDevice_IsBusy"); c->r[0] = 0; X_RET(0); }
void xv_hle_D3DDevice_BlockUntilVerticalBlank(xctx *c) { XD3D_COUNT("D3DDevice_BlockUntilVerticalBlank"); xk_yield(); X_RET(0); }
/* Real hardware raises vblank interrupts at 60 Hz regardless of Present; Halo's frame limiter counts
 * them.  A kernel guest thread invokes the registered callback every 16.7 ms. */
static void vblank_thread(xctx *c, void *arg)
{
    (void)arg; unsigned counter = 0;
    uint32_t data = xk_kalloc(16);
    for (;;) {
        xk_sleep_us(16667);
        if (g_dev.vblank_cb) {
            counter++;
            X_M32(data) = counter; X_M32(data + 4) = g_dev.frame; X_M32(data + 8) = 0; X_M32(data + 12) = 0;
            call_guest(c, g_dev.vblank_cb, data);
        }
    }
}
void xv_hle_D3DDevice_SetVerticalBlankCallback(xctx *c)
{ XD3D_COUNT("D3DDevice_SetVerticalBlankCallback");
    static int started;
    g_dev.vblank_cb = X_ARG(0);
    if (!started && g_dev.vblank_cb) { started = 1; xk_thread_create_host(vblank_thread, 0); D3DLOG("vblank thread started (cb %08X)\n", g_dev.vblank_cb); }
    X_RET(1);
}
void xv_hle_D3DDevice_PersistDisplay(xctx *c) { XD3D_COUNT("D3DDevice_PersistDisplay"); c->r[0] = 0; X_RET(0); }
void xv_hle_D3DDevice_SetFlickerFilter(xctx *c) { XD3D_COUNT("D3DDevice_SetFlickerFilter"); X_RET(1); }
void xv_hle_D3DDevice_SetSoftDisplayFilter(xctx *c) { XD3D_COUNT("D3DDevice_SetSoftDisplayFilter"); X_RET(1); }
void xv_hle_D3DDevice_SetShaderConstantMode(xctx *c) { XD3D_COUNT("D3DDevice_SetShaderConstantMode"); X_RET(1); }
void xv_hle_D3DDevice_GetBackBuffer(xctx *c) { XD3D_COUNT("D3DDevice_GetBackBuffer"); RES_COMMON(g_dev.backbuffer)++; X_M32(X_ARG(2)) = g_dev.backbuffer; c->r[0] = 0; X_RET(3); }
void xv_hle_D3DDevice_GetDepthStencilSurface(xctx *c) { XD3D_COUNT("D3DDevice_GetDepthStencilSurface"); RES_COMMON(g_dev.depth)++; X_M32(X_ARG(0)) = g_dev.depth; c->r[0] = 0; X_RET(1); }
void xv_hle_D3DDevice_SetRenderTarget(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderTarget"); XD3D_RET("SetRenderTarget"); if (xd3d_hist_active()) D3DLOG("[hist] SetRenderTarget(%08X, %08X) backbuffer %08X depth %08X\n", X_ARG(0), X_ARG(1), g_dev.backbuffer, g_dev.depth); xd3d_r_state("SetRenderTarget", X_ARG(0), X_ARG(1), g_dev.backbuffer); X_RET(2); }
/* D3DVIEWPORT8 { X, Y, Width, Height, MinZ, MaxZ } */
void xv_hle_D3DDevice_SetViewport(xctx *c)
{ XD3D_COUNT("D3DDevice_SetViewport");
    uint32_t v = X_ARG(0);
    xd3d_state.vp_x = X_M32(v); xd3d_state.vp_y = X_M32(v + 4); xd3d_state.vp_w = X_M32(v + 8); xd3d_state.vp_h = X_M32(v + 12);
    memcpy(&xd3d_state.vp_minz, X_G(v + 16), 4); memcpy(&xd3d_state.vp_maxz, X_G(v + 20), 4);
    X_RET(1);
}
void xv_hle_D3DDevice_SetTransform(xctx *c) { XD3D_COUNT("D3DDevice_SetTransform"); X_RET(2); }
void xv_hle_D3DDevice_GetTransform(xctx *c) { XD3D_COUNT("D3DDevice_GetTransform"); memset(X_G(X_ARG(1)), 0, 64); X_MF32(X_ARG(1)) = X_MF32(X_ARG(1) + 20) = X_MF32(X_ARG(1) + 40) = X_MF32(X_ARG(1) + 60) = 1.0f; X_RET(2); }

/* Present / Swap: fire callbacks, count the frame */
void xv_hle_D3DDevice_Present(xctx *c)
{ XD3D_COUNT("D3DDevice_Present");
    g_dev.frame++;
    xd3d_r_present(g_dev.frame, g_dev.draws);
    if (g_dev.frame % 60 == 0) { uint32_t gg = X_M32(0x2F8CA0); float pct = 0; float campos[3] = { 0, 0, 0 }, camfwd[3] = { 0, 0, 0 };
        {   /* camera from the view-projection rows c[-96..-93] of the frame's first depth-tested world draw */
            const float (*m)[4] = g_vp_rows;
            for (int i = 0; i < 3; ++i) { camfwd[i] = m[2][i];
                campos[i] = -(m[0][i] * m[0][3] / (m[0][0]*m[0][0]+m[0][1]*m[0][1]+m[0][2]*m[0][2] + 1e-9f)
                            + m[1][i] * m[1][3] / (m[1][0]*m[1][0]+m[1][1]*m[1][1]+m[1][2]*m[1][2] + 1e-9f)
                            + m[2][i] * m[2][3] / (m[2][0]*m[2][0]+m[2][1]*m[2][1]+m[2][2]*m[2][2] + 1e-9f)); } } if (gg) { uint32_t pv = X_M32(gg + 4); memcpy(&pct, &pv, 4); }
        float cx, cy, cz; uint32_t v; v = X_M32(0x2331E4); memcpy(&cx, &v, 4); v = X_M32(0x2331E8); memcpy(&cy, &v, 4); v = X_M32(0x2331EC); memcpy(&cz, &v, 4);
        D3DLOG("  frame stats: %u Begin/End, %u SetVertexData | game_globals %08X: loaded %u active %u b2 %u b3 %u pct %.2f | director on %u cam %.2f %.2f %.2f | gg+C %08X +10 %08X +14 %08X | cam %.2f %.2f %.2f fwd %.2f %.2f %.2f | act %08X\n", g_im.begins_in_frame, g_im.setdata_in_frame,
               gg, gg ? X_M8(gg) : 0, gg ? X_M8(gg + 1) : 0, gg ? X_M8(gg + 2) : 0, gg ? X_M8(gg + 3) : 0, pct, X_M8(0x2331D9), cx, cy, cz, gg ? X_M32(gg + 0xC) : 0, gg ? X_M32(gg + 0x10) : 0, gg ? X_M32(gg + 0x14) : 0, campos[0], campos[1], campos[2], camfwd[0], camfwd[1], camfwd[2], X_M32(0x276794) ? X_M32(X_M32(0x276794)) : 0); }
    {   /* --trace-funcs: count only during the requested frame, dump at its Present */
        extern int xv_trace_funcs; extern int xv_trace_func_frame(void); extern void xv_trace_func_reset(void); extern void xv_trace_func_dump(const char *);
        int ff = xv_trace_func_frame();
        if (ff >= 0) {
            if (xv_trace_funcs) { xv_trace_func_dump("[fn]"); xv_trace_funcs = 0; }
            if ((int)g_dev.frame + 1 == ff) { xv_trace_func_reset(); xv_trace_funcs = 1; }
        }
    }
    if (g_hist_frame >= 0 && (int)g_dev.frame == g_hist_frame) { xd3d_hist_dump("[hist]");
        for (unsigned i = 0; i < g_nhm; ++i) D3DLOG("[hist] rs method %04X x%u (last %08X)\n", g_hm[i].m, g_hm[i].n, g_hm[i].last); }
    { static int fa = -2; if (fa == -2) { const char *e = getenv("XV_FORCE_ACTIVE"); fa = e ? atoi(e) : -1; }
      if (fa >= 0 && (int)g_dev.frame == fa) { uint32_t gg = X_M32(0x2F8CA0); if (gg) { X_M8(gg + 1) = 1; D3DLOG("forced game_globals.active = 1\n"); } } }
    xd3d_hist_reset();
    g_im.begins_in_frame = 0; g_im.setdata_in_frame = 0;
    g_dev.draws = 0; g_dev.clears = 0;
    uint32_t swap_cb = g_xd3d_device ? X_M32(g_xd3d_device + DEVICE_SWAPCALLBACK_OFFSET) : 0;
    if (swap_cb) { uint32_t d = xk_kalloc(16); X_M32(d) = g_dev.frame; X_M32(d + 4) = 0; X_M32(d + 8) = 0; X_M32(d + 12) = 1; call_guest(c, swap_cb, d); }
    xk_yield();
    c->r[0] = 0; X_RET(4);
}
void xv_hle_D3DDevice_Swap(xctx *c) { XD3D_COUNT("D3DDevice_Swap"); g_dev.frame++; xd3d_r_present(g_dev.frame, g_dev.draws); g_dev.draws = 0; g_dev.clears = 0; xk_yield(); c->r[0] = 0; X_RET(1); }
void xd3d_r_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil) __attribute__((weak));
void xd3d_r_clear(uint32_t flags, uint32_t color, float z, uint32_t stencil) { (void)flags; (void)color; (void)z; (void)stencil; }
/* D3DDevice_Clear(Count, pRects, Flags, Color, Z, Stencil) */
void xv_hle_D3DDevice_Clear(xctx *c) { XD3D_COUNT("D3DDevice_Clear"); g_dev.clears++; float z; uint32_t zi = X_M32(c->r[4] + 4 + 16); memcpy(&z, &zi, 4); xd3d_r_clear(X_ARG(2), X_ARG(3), z, X_ARG(5)); c->r[0] = 0; X_RET(6); }
void xv_hle_D3DDevice_BeginVisibilityTest(xctx *c) { XD3D_COUNT("D3DDevice_BeginVisibilityTest"); c->r[0] = 0; X_RET(0); }
void xv_hle_D3DDevice_EndVisibilityTest(xctx *c) { XD3D_COUNT("D3DDevice_EndVisibilityTest"); c->r[0] = 0; X_RET(1); }
void xv_hle_D3DDevice_GetVisibilityTestResult(xctx *c) { XD3D_COUNT("D3DDevice_GetVisibilityTestResult"); if (X_ARG(1)) X_M32(X_ARG(1)) = 1; if (X_ARG(2)) X_M64(X_ARG(2)) = 0; c->r[0] = 0; X_RET(3); }

/* ---- draws & state ---------------------------------------------------------------------------- */
void xv_hle_D3DDevice_DrawVertices(xctx *c) { XD3D_COUNT("D3DDevice_DrawVertices"); g_dev.draws++; g_dev.draws_total++; xd3d_r_draw(c, 0, X_ARG(0), X_ARG(2), X_ARG(1)); c->r[0] = 0; X_RET(3); }
void xv_hle_D3DDevice_DrawIndexedVertices(xctx *c) { XD3D_COUNT("D3DDevice_DrawIndexedVertices");
    if (g_vp_frame != g_dev.frame && xd3d_state.z_enable) { g_vp_frame = g_dev.frame; memcpy(g_vp_rows, xd3d_state.vsc, sizeof g_vp_rows); }
    g_dev.draws++; g_dev.draws_total++; xd3d_r_draw(c, 1, X_ARG(0), X_ARG(1), X_ARG(2)); c->r[0] = 0; X_RET(3); }
void xv_hle_D3DDevice_Begin(xctx *c)
{ XD3D_COUNT("D3DDevice_Begin");
    { static int done; if (!done && xd3d_frame() > 1 && X_ARG(0) == 7 && getenv("XV_STACKDUMP")) {
        done = 1; uint32_t sp = c->r[4];
        for (unsigned i = 0; i < 100; ++i) {
            uint32_t v = X_M32(sp + i * 4);
            if ((v >= 0x11000 && v < 0x180E40) || i < 4) xk_os_log("[stk] +%03X: %08X%s\n", i * 4, v, v >= 0x11000 && v < 0x180E40 ? "  <- code" : "");
        }
    } }
    g_im.prim = X_ARG(0); g_im.verts = 0; g_im.begins_in_frame++; c->r[0] = 0; X_RET(1);
}
void xv_hle_D3DDevice_End(xctx *c)
{ XD3D_COUNT("D3DDevice_End");
    g_dev.draws++; g_dev.draws_total++;
    if (g_dev.draws_total < 20)
        D3DLOG("End: prim %u verts %u vs %08X tex %08X ps %08X\n", g_im.prim, g_im.verts, xd3d_state.vs_handle, xd3d_state.texture[0], xd3d_state.ps_def);
    xd3d_r_im_end(g_im.prim, g_im_v, g_im.verts);
    g_im.verts = 0;
    c->r[0] = 0; X_RET(0);
}
void *xv_dbg_watch_host;                 /* host address of the guest vertex array (for gdb watchpoints) */
uint32_t xv_dbg_watch_guest;
void xv_dbg_break(void) { }              /* gdb: break here after the address is known */
void xv_hle_D3DDevice_SetVertexData2f(xctx *c)
{ XD3D_COUNT("D3DDevice_SetVertexData2f");
    { static unsigned n; if (n < 12 && xd3d_frame() > 0) { n++; xk_os_log("[2f] ret %08X reg %u = %.2f %.2f (esi %08X)\n", X_M32(c->r[4]), X_ARG(0), f32arg(c, 1), f32arg(c, 2), c->r[6]); } }
    { static int armed; if (!armed && xd3d_frame() > 1 && X_ARG(0) == 0 && getenv("XV_WATCH")) {
        armed = 1; xv_dbg_watch_guest = c->r[6];                     /* esi = current vertex */
        xv_dbg_watch_host = X_G(c->r[6] + 4);                        /* watch the y of this vertex */
        xk_os_log("[dbg] watching guest %08X (host %p)\n", c->r[6] + 4, xv_dbg_watch_host);
        xv_dbg_break();
    } }
    im_set2("2f", X_ARG(0), f32arg(c, 1), f32arg(c, 2), 0.0f, 1.0f); c->r[0] = 0; X_RET(3);
}
void xv_hle_D3DDevice_SetVertexData2s(xctx *c) { XD3D_COUNT("D3DDevice_SetVertexData2s"); im_set2("2s", X_ARG(0), (float)(int16_t)(X_ARG(1) & 0xFFFF), (float)(int16_t)(X_ARG(2) & 0xFFFF), 0.0f, 1.0f); c->r[0] = 0; X_RET(3); }
void xv_hle_D3DDevice_SetVertexData4f(xctx *c) { XD3D_COUNT("D3DDevice_SetVertexData4f"); im_set2("4f", X_ARG(0), f32arg(c, 1), f32arg(c, 2), f32arg(c, 3), f32arg(c, 4)); c->r[0] = 0; X_RET(5); }
void xv_hle_D3DDevice_SetVertexData4ub(xctx *c) { XD3D_COUNT("D3DDevice_SetVertexData4ub"); im_set2("4ub", X_ARG(0), (X_ARG(1) & 0xFF) / 255.0f, (X_ARG(2) & 0xFF) / 255.0f, (X_ARG(3) & 0xFF) / 255.0f, (X_ARG(4) & 0xFF) / 255.0f); c->r[0] = 0; X_RET(5); }
void xv_hle_D3DDevice_SetVertexDataColor(xctx *c) { XD3D_COUNT("D3DDevice_SetVertexDataColor"); uint32_t d = X_ARG(1);
    im_set(X_ARG(0), ((d >> 16) & 0xFF) / 255.0f, ((d >> 8) & 0xFF) / 255.0f, (d & 0xFF) / 255.0f, ((d >> 24) & 0xFF) / 255.0f); c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_SetStreamSource(xctx *c) { XD3D_COUNT("D3DDevice_SetStreamSource"); XD3D_RET("SetStreamSource"); uint32_t i = X_ARG(0) & 3; xd3d_state.stream_vb[i] = X_ARG(1); xd3d_state.stream_stride[i] = X_ARG(2); c->r[0] = 0; X_RET(3); }
/* XDK 3925 D3D keeps state in globals the game reads straight out of the library's .data:
 *   D3D__IndexData   0x18F17C  (SetIndices writes pIndexBuffer->Data; Halo adds its own WORD offsets)
 *   D3D__TextureState 0x18F180 [stage*32 + type]   D3D__RenderState 0x18F380 [state]
 * The HLE has to keep those mirrors current or the game computes pointers from zero. */
#define D3D_G_INDEXDATA     0x0018F17Cu
#define D3D_G_TEXTURESTATE  0x0018F180u
#define D3D_G_RENDERSTATE   0x0018F380u
void xv_hle_D3DDevice_SetIndices(xctx *c) { XD3D_COUNT("D3DDevice_SetIndices"); XD3D_RET("SetIndices"); xd3d_state.indices = X_ARG(0); xd3d_state.index_base = X_ARG(1);
    X_M32(D3D_G_INDEXDATA) = X_ARG(0) ? X_M32(X_ARG(0) + 4) : 0;
    if (xd3d_hist_active()) D3DLOG("[hist] SetIndices ib %08X (common %08X data %08X lock %08X) base %u | [18F17C] %08X\n", X_ARG(0), X_ARG(0) ? X_M32(X_ARG(0)) : 0, X_ARG(0) ? X_M32(X_ARG(0) + 4) : 0, X_ARG(0) ? X_M32(X_ARG(0) + 8) : 0, X_ARG(1), X_M32(0x18F17Cu)); c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_SetTexture(xctx *c) { XD3D_COUNT("D3DDevice_SetTexture"); xd3d_state.texture[X_ARG(0) & 3] = X_ARG(1);
    { static unsigned n; const char *e = getenv("XV_LOG_TEX"); if (e && n < 40 && xd3d_frame() >= (unsigned)atoi(e)) { n++; uint32_t t = X_ARG(1);
        xk_os_log("[tex] stage %u hdr %08X ret %08X : %08X %08X %08X %08X %08X | bd115 %08X %08X %08X | bd111 %08X %08X %08X\n", X_ARG(0), t, X_M32(c->r[4]),
            t ? X_M32(t) : 0, t ? X_M32(t + 4) : 0, t ? X_M32(t + 8) : 0, t ? X_M32(t + 12) : 0, t ? X_M32(t + 16) : 0,
            X_M32(0x80492CC8 + 0x24), X_M32(0x80492CC8 + 0x28), X_M32(0x80492CC8 + 0x2C), X_M32(0x80491D54 + 0x24), X_M32(0x80491D54 + 0x28), X_M32(0x80491D54 + 0x2C)); } }
    c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_SetPalette(xctx *c) { XD3D_COUNT("D3DDevice_SetPalette"); c->r[0] = 0; X_RET(2); }
/* D3DRS_* -> NV2A method table in the D3D library (.rdata 0x1F08C8, 0x52 entries): states 0..0x33 are the
 * pixel shader def fields (D3DRS_PSALPHAINPUTS0 .. D3DRS_PSINPUTTEXTURE), the rest the simple states. */
#define D3D_RS_METHOD_TABLE 0x001F08C8u
static void rs_method(uint32_t method, uint32_t v);
void xv_hle_D3DDevice_SetRenderStateNotInline(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderStateNotInline");
    uint32_t st = X_ARG(0), v = X_ARG(1);
    if (st < 0x52) rs_method(X_M32(D3D_RS_METHOD_TABLE + st * 4), v);
    if (st < 0x74) X_M32(D3D_G_RENDERSTATE + st * 4) = v;
    c->r[0] = 0; X_RET(2); }
/* fastcall: ecx = NV2A method (push-buffer byte offset), edx = value.  Track blend / alpha-test state. */
static int ps_method_to_def(uint32_t m);
/* One NV2A render-state method (push-buffer offset) with its value: the common path for the fastcall
 * helper (ecx = method, edx = value) and for SetRenderStateNotInline (which D3D routes through it). */
static void rs_method(uint32_t method, uint32_t v)
{
    uint32_t m = method & 0x1FFC;
    switch (m) {
    case 0x300: xd3d_state.alpha_test = v; break;      /* NV097_SET_ALPHA_TEST_ENABLE */
    case 0x304: xd3d_state.alpha_blend = v; break;     /* NV097_SET_BLEND_ENABLE */
    case 0x33C: xd3d_state.alpha_func = v; break;      /* NV097_SET_ALPHA_FUNC (0x200 never .. 0x207 always) */
    case 0x340: xd3d_state.alpha_ref = v; break;
    case 0x344: xd3d_state.src_blend = v; break;       /* NV097_SET_BLEND_FUNC_SFACTOR (GL enums) */
    case 0x348: xd3d_state.dst_blend = v; break;
    case 0x350: xd3d_state.blend_op = v; break;
    case 0x354: xd3d_state.z_func = v; break;
    case 0x35C: xd3d_state.z_write = v; break;
    default: { int off = ps_method_to_def(m); if (off >= 0) { xd3d_state.ps_shadow[off / 4] = v; xd3d_state.ps_dirty = 1; } } break;
    }
    { static unsigned n; if (n < 40 && getenv("XV_LOG_RS")) { n++; xk_os_log("[rs] method %08X value %08X\n", method, v); } }
    if (xd3d_hist_active()) {                          /* which NV2A methods the game pokes directly this frame */
        unsigned i;
        for (i = 0; i < g_nhm; ++i) if (g_hm[i].m == m) break;
        if (i == g_nhm && g_nhm < 128) { g_hm[g_nhm].m = m; g_hm[g_nhm].n = 0; g_nhm++; }
        if (i < 128) { g_hm[i].n++; g_hm[i].last = v; }
    }
}
void xv_hle_D3DDevice_SetRenderState_Simple(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderState_Simple"); rs_method(c->r[1], c->r[2]); X_RET(0); }
void xv_hle_D3DDevice_SetTextureStageStateNotInline(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureStageStateNotInline"); c->r[0] = 0; X_RET(3); }
void xv_hle_D3DDevice_SetTextureState_TexCoordIndex(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureState_TexCoordIndex"); X_RET(2); }
void xv_hle_D3DDevice_SetTextureState_BorderColor(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureState_BorderColor"); X_RET(2); }
void xv_hle_D3DDevice_SetTextureState_ColorKeyColor(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureState_ColorKeyColor"); X_RET(2); }
void xv_hle_D3DDevice_SetTextureState_BumpEnv(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureState_BumpEnv"); X_RET(3); }
void xv_hle_D3DDevice_SetTextureState_Deferred(xctx *c) { XD3D_COUNT("D3DDevice_SetTextureState_Deferred"); if ((c->r[1] & 3) == c->r[1] && c->r[2] < 32) X_M32(D3D_G_TEXTURESTATE + ((c->r[1] << 5) + c->r[2]) * 4) = X_ARG(0); X_RET(1); }
void xv_hle_D3DDevice_SetRenderState_Deferred(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderState_Deferred"); X_RET(0); }
#define RS1(name) void xv_hle_D3DDevice_SetRenderState_##name(xctx *c) { X_RET(1); }
RS1(BackFillMode)
void xv_hle_D3DDevice_SetRenderState_CullMode(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderState_CullMode"); xd3d_state.cull = X_ARG(0); X_RET(1); } RS1(DoNotCullUncompressed) RS1(Dxt1NoiseEnable) RS1(EdgeAntiAlias) RS1(FillMode) RS1(FogColor)
RS1(FrontFace) RS1(LineWidth) RS1(LogicOp) RS1(MultiSampleAntiAlias) RS1(MultiSampleMask) RS1(MultiSampleType) RS1(NormalizeNormals)
RS1(OcclusionCullEnable) RS1(PSTextureModes) RS1(RopZCmpAlwaysRead) RS1(RopZRead) RS1(ShadowFunc) RS1(StencilCullEnable)
RS1(StencilEnable) RS1(StencilFail) RS1(TextureFactor) RS1(TwoSidedLighting) RS1(VertexBlend) RS1(YuvEnable) RS1(ZBias)
void xv_hle_D3DDevice_SetRenderState_ZEnable(xctx *c) { XD3D_COUNT("D3DDevice_SetRenderState_ZEnable"); xd3d_state.z_enable = X_ARG(0); X_RET(1); }

/* ---- vertex / pixel shaders ------------------------------------------------------------------ */
static uint32_t fnv1a(const uint8_t *p, unsigned n) { uint32_t h = 0x811C9DC5u; while (n--) { h ^= *p++; h *= 0x01000193u; } return h; }
/* HRESULT D3DDevice_CreateVertexShader(pDeclaration, pFunction, pHandle, Usage) */
void xv_hle_D3DDevice_CreateVertexShader(xctx *c)
{ XD3D_COUNT("D3DDevice_CreateVertexShader");
    uint32_t decl = X_ARG(0), func = X_ARG(1), ph = X_ARG(2);
    uint32_t sh = xk_kalloc(64);                 /* guest-visible shader object: { decl, func, size, hash } */
    X_M32(sh) = decl; X_M32(sh + 4) = func;
    uint32_t size = 0, hash = 0;
    if (func) { size = 4 + 16 * X_M8(func + 2); hash = fnv1a(X_G(func), size); }
    X_M32(sh + 8) = size; X_M32(sh + 12) = hash;
    X_M32(ph) = sh | 1;
    D3DLOG("CreateVertexShader(decl %08X, func %08X [%u B, fnv %08X]) -> %08X\n", decl, func, size, hash, sh | 1);
    c->r[0] = 0; X_RET(4);
}
void xv_hle_D3DDevice_DeleteVertexShader(xctx *c) { XD3D_COUNT("D3DDevice_DeleteVertexShader"); c->r[0] = 0; X_RET(1); }
void xv_hle_D3DDevice_SetVertexShader(xctx *c) { XD3D_COUNT("D3DDevice_SetVertexShader"); XD3D_RET("SetVertexShader"); xd3d_state.vs_handle = X_ARG(0); c->r[0] = 0; X_RET(1); }
void xv_hle_D3DDevice_SelectVertexShader(xctx *c) { XD3D_COUNT("D3DDevice_SelectVertexShader"); xd3d_state.vs_handle = X_ARG(0); c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_LoadVertexShader(xctx *c) { XD3D_COUNT("D3DDevice_LoadVertexShader"); c->r[0] = 0; X_RET(2); }
void xv_hle_D3DDevice_GetVertexShaderSize(xctx *c) { XD3D_COUNT("D3DDevice_GetVertexShaderSize"); uint32_t sh = X_ARG(0) & ~1u; if (X_ARG(1)) X_M32(X_ARG(1)) = (X_ARG(0) & 1) ? X_M32(sh + 8) : 0; X_RET(2); }
/* SetVertexShaderConstant(Register (-96..95), pConstantData, ConstantCount) */
void xv_hle_D3DDevice_SetVertexShaderConstant(xctx *c)
{ XD3D_COUNT("D3DDevice_SetVertexShaderConstant");
    int32_t reg = (int32_t)X_ARG(0) + 96; uint32_t src = X_ARG(1), n = X_ARG(2);
    for (uint32_t i = 0; i < n && reg + (int32_t)i < 192; ++i)
        if (reg + (int32_t)i >= 0) memcpy(xd3d_state.vsc[reg + i], X_G(src + i * 16), 16);
    if ((uint32_t)reg < xd3d_state.vsc_dirty_lo) xd3d_state.vsc_dirty_lo = (uint32_t)reg;
    if ((uint32_t)(reg + n) > xd3d_state.vsc_dirty_hi) xd3d_state.vsc_dirty_hi = (uint32_t)(reg + n);
    c->r[0] = 0; X_RET(3);
}
/* Halo builds its register-combiner programs at run time into one scratch X_D3DPIXELSHADERDEF and
 * re-submits it before each material.  Hash the program fields (everything except the constant colours)
 * so the renderer can pick the fragment program compiled offline for that combiner setup, and route the
 * def's constants (PSConstant0/1[stage] -> D3D c[PSC0/1Mapping nibble]) to psc[]. */
static uint32_t psdef_hash(const uint8_t *d)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 0xF0; ++i) {
        if ((i >= 0x28 && i < 0x68) || (i >= 0xAC && i < 0xB4)) continue;       /* PSConstant0/1[8], final combiner constants */
        h = (h ^ d[i]) * 16777619u;
    }
    return h;
}
static void psdef_load(uint32_t def)
{
    memcpy(xd3d_state.ps_shadow, X_G(def), 0xF0);
    xd3d_state.ps_dirty = 1;
}
/* NV2A method (push-buffer offset) -> byte offset of the same field in X_D3DPIXELSHADERDEF, or -1.
 * D3DDevice_SetPixelShaderProgram writes exactly these registers from the def; Halo then patches
 * individual stages/constants per material through the fastcall push helper. */
static int ps_method_to_def(uint32_t m)
{
    if (m >= 0x0260 && m < 0x0280) return 0x00 + (m - 0x0260);   /* ALPHA_ICW[8]   PSAlphaInputs   */
    if (m == 0x0288) return 0x20;                                  /* SPECULAR_FOG_CW0  final ABCD   */
    if (m == 0x028C) return 0x24;                                  /* SPECULAR_FOG_CW1  final EFG    */
    if (m >= 0x0A60 && m < 0x0A80) return 0x28 + (m - 0x0A60);   /* FACTOR0[8]     PSConstant0     */
    if (m >= 0x0A80 && m < 0x0AA0) return 0x48 + (m - 0x0A80);   /* FACTOR1[8]     PSConstant1     */
    if (m >= 0x0AA0 && m < 0x0AC0) return 0x68 + (m - 0x0AA0);   /* ALPHA_OCW[8]   PSAlphaOutputs  */
    if (m >= 0x0AC0 && m < 0x0AE0) return 0x88 + (m - 0x0AC0);   /* COLOR_ICW[8]   PSRGBInputs     */
    if (m == 0x17F8) return 0xA8;                                  /* SHADER_CLIP_PLANE_MODE PSCompareMode */
    if (m == 0x1E20) return 0xAC;                                  /* SPECULAR_FOG_FACTOR0 final c0  */
    if (m == 0x1E24) return 0xB0;                                  /* SPECULAR_FOG_FACTOR1 final c1  */
    if (m >= 0x1E40 && m < 0x1E60) return 0xB4 + (m - 0x1E40);   /* COLOR_OCW[8]   PSRGBOutputs    */
    if (m == 0x1E60) return 0xD4;                                  /* COMBINER_CONTROL PSCombinerCount */
    if (m == 0x1D90) return 0xD8;                                  /* SHADER_STAGE_PROGRAM PSTextureModes */
    if (m == 0x1E74) return 0xDC;                                  /* DOT_RGBMAPPING PSDotMapping    */
    if (m == 0x1E78) return 0xE0;                                  /* SHADER_OTHER_STAGE_INPUT PSInputTexture */
    return -1;
}
void xd3d_ps_sync(void)
{
    if (!xd3d_state.ps_dirty) return;
    xd3d_state.ps_dirty = 0;
    const uint8_t *d = (const uint8_t *)xd3d_state.ps_shadow;
    xd3d_state.ps_hash = psdef_hash(d);
    /* NV2A constants are per stage (UNIQUE_C0/C1, which Halo always sets); PSC0/1Mapping only matters for
     * SetPixelShaderConstant, which Halo never calls.  Layout matches pixelshader_recomp_gen.py's psc[18]. */
    #define PSC_SET(idx, col) do { uint32_t col_ = (col); float *o_ = xd3d_state.psc[(idx)]; \
        o_[0] = ((col_ >> 16) & 0xFF) / 255.0f; o_[1] = ((col_ >> 8) & 0xFF) / 255.0f; o_[2] = (col_ & 0xFF) / 255.0f; o_[3] = (col_ >> 24) / 255.0f; } while (0)
    for (unsigned i = 0; i < 8; ++i) {
        PSC_SET(i, xd3d_state.ps_shadow[0x28 / 4 + i]);
        PSC_SET(8 + i, xd3d_state.ps_shadow[0x48 / 4 + i]);
    }
    PSC_SET(16, xd3d_state.ps_shadow[0xAC / 4]);
    PSC_SET(17, xd3d_state.ps_shadow[0xB0 / 4]);
    #undef PSC_SET
    static uint32_t seen[512]; static unsigned nseen;
    for (unsigned i = 0; i < nseen; ++i) if (seen[i] == xd3d_state.ps_hash) return;
    if (nseen < 512) seen[nseen++] = xd3d_state.ps_hash;
    char hex[0xF0 * 2 + 1]; for (unsigned i = 0; i < 0xF0; ++i) snprintf(hex + 2 * i, 3, "%02X", d[i]);
    D3DLOG("[psdef] %08X %s\n", xd3d_state.ps_hash, hex);
}
void xv_hle_D3DDevice_SetPixelShaderProgram(xctx *c) { XD3D_COUNT("D3DDevice_SetPixelShaderProgram"); xd3d_state.ps_def = X_ARG(0); if (X_ARG(0)) psdef_load(X_ARG(0)); else { memset(xd3d_state.ps_shadow, 0, sizeof xd3d_state.ps_shadow); xd3d_state.ps_dirty = 1; } c->r[0] = 0; X_RET(1); }

/* ---- resources ------------------------------------------------------------------------------ */
static uint32_t make_pixel_container(unsigned w, unsigned h, unsigned d, unsigned levels, unsigned fmt, int cube, int volume, uint32_t *out_bytes)
{
    if (!levels) { levels = 1; unsigned m = w > h ? w : h; while (m > 1) { m >>= 1; levels++; } }
    uint32_t total = 0; unsigned lw = w, lh = h, ld = d;
    for (unsigned l = 0; l < levels; ++l) { total += level_bytes(fmt, lw, lh, ld) * (cube ? 6 : 1); lw = lw > 1 ? lw >> 1 : 1; lh = lh > 1 ? lh >> 1 : 1; ld = ld > 1 ? ld >> 1 : 1; }
    total = (total + 127) & ~127u;
    uint32_t hdr = new_header(X_D3DCOMMON_TYPE_TEXTURE, 64);
    uint32_t phys = xk_phys_alloc(total ? total : 128, 128, 0, 0, 1);
    RES_DATA(hdr) = phys;
    int linear = fmt_is_linear(fmt);
    uint32_t f = 1u | (cube ? 4u : 0) | ((volume ? 3u : 2u) << 4) | ((fmt & 0xFF) << 8) | ((levels & 0xF) << 16);
    if (!linear) f |= (log2u(w) << 20) | (log2u(h) << 24) | (log2u(d) << 28);
    PC_FORMAT(hdr) = f;
    PC_SIZE(hdr) = linear ? (((w - 1) & 0xFFF) | (((h - 1) & 0xFFF) << 12) | (((((w * fmt_bits(fmt) / 8 + 63) & ~63u) / 64) - 1) << 24)) : 0;
    if (out_bytes) *out_bytes = total;
    if (!phys) D3DLOG("out of physical memory for %ux%ux%u fmt %02X (%u KB)\n", w, h, d, fmt, total >> 10);
    return hdr;
}
/* HRESULT D3DDevice_CreateTexture(Width, Height, Levels, Usage, Format, Pool, ppTexture) */
void xv_hle_D3DDevice_CreateTexture(xctx *c)
{ XD3D_COUNT("D3DDevice_CreateTexture");
    uint32_t bytes; uint32_t t = make_pixel_container(X_ARG(0), X_ARG(1), 1, X_ARG(2), X_ARG(4), 0, 0, &bytes);
    static unsigned n; if (n++ < 16) D3DLOG("CreateTexture(%ux%u, levels %u, fmt %02X) -> %08X data %08X (%u KB)\n", X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(4), t, RES_DATA(t), bytes >> 10);
    X_M32(X_ARG(6)) = t; c->r[0] = RES_DATA(t) ? 0 : 0x8007000Eu; X_RET(7);
}
/* HRESULT D3DDevice_CreateVolumeTexture(Width, Height, Depth, Levels, Usage, Format, Pool, ppVolumeTexture) */
void xv_hle_D3DDevice_CreateVolumeTexture(xctx *c) { XD3D_COUNT("D3DDevice_CreateVolumeTexture"); uint32_t t = make_pixel_container(X_ARG(0), X_ARG(1), X_ARG(2), X_ARG(3), X_ARG(5), 0, 1, NULL); X_M32(X_ARG(7)) = t; c->r[0] = RES_DATA(t) ? 0 : 0x8007000Eu; X_RET(8); }
/* HRESULT D3DDevice_CreateCubeTexture(EdgeLength, Levels, Usage, Format, Pool, ppCubeTexture) */
void xv_hle_D3DDevice_CreateCubeTexture(xctx *c) { XD3D_COUNT("D3DDevice_CreateCubeTexture"); uint32_t t = make_pixel_container(X_ARG(0), X_ARG(0), 1, X_ARG(1), X_ARG(3), 1, 0, NULL); X_M32(X_ARG(5)) = t; c->r[0] = RES_DATA(t) ? 0 : 0x8007000Eu; X_RET(6); }
/* HRESULT D3DDevice_CreateVertexBuffer(Length, Usage, FVF, Pool, ppVertexBuffer) */
void xv_hle_D3DDevice_CreateVertexBuffer(xctx *c)
{ XD3D_COUNT("D3DDevice_CreateVertexBuffer");
    uint32_t vb = new_header(X_D3DCOMMON_TYPE_VERTEXBUFFER, 64); uint32_t len = (X_ARG(0) + 127) & ~127u;
    RES_DATA(vb) = xk_phys_alloc(len ? len : 128, 128, 0, 0, 1);
    static unsigned n; if (n++ < 8) D3DLOG("CreateVertexBuffer(%u B) -> %08X data %08X\n", X_ARG(0), vb, RES_DATA(vb));
    X_M32(X_ARG(4)) = vb; c->r[0] = RES_DATA(vb) ? 0 : 0x8007000Eu; X_RET(5);
}
/* HRESULT D3DDevice_CreateIndexBuffer(Length, Usage, Format, Pool, ppIndexBuffer) */
void xv_hle_D3DDevice_CreateIndexBuffer(xctx *c)
{ XD3D_COUNT("D3DDevice_CreateIndexBuffer");
    uint32_t ib = new_header(X_D3DCOMMON_TYPE_INDEXBUFFER, 64); uint32_t len = (X_ARG(0) + 127) & ~127u;
    RES_DATA(ib) = xk_phys_alloc(len ? len : 128, 128, 0, 0, 1);
    X_M32(X_ARG(4)) = ib; c->r[0] = RES_DATA(ib) ? 0 : 0x8007000Eu; X_RET(5);
}
/* HRESULT D3DDevice_CreatePalette(Size (0=256,1=128,2=64,3=32 entries), ppPalette) */
void xv_hle_D3DDevice_CreatePalette(xctx *c)
{ XD3D_COUNT("D3DDevice_CreatePalette");
    uint32_t p = new_header(X_D3DCOMMON_TYPE_PALETTE, 64); unsigned entries = 256u >> (X_ARG(0) & 3);
    RES_DATA(p) = xk_phys_alloc(entries * 4, 128, 0, 0, 1); RES_COMMON(p) |= (X_ARG(0) & 3) << 30;
    X_M32(X_ARG(1)) = p; c->r[0] = 0; X_RET(2);
}
/* D3DResource_Register(pThis, pBase): fix up a resource whose Data is an offset from pBase (Halo cache resources) */
void xv_hle_D3DResource_Register(xctx *c)
{ XD3D_COUNT("D3DResource_Register");
    uint32_t r = X_ARG(0), base = X_ARG(1);
    uint32_t data = RES_DATA(r);
    RES_DATA(r) = (base + data) & 0x03FFFFFFu;              /* physical */
    RES_COMMON(r) = (RES_COMMON(r) & ~X_D3DCOMMON_REFCOUNT_MASK) | 1;
    static unsigned n; if (n++ < 6) D3DLOG("Register(%08X: common %08X data %08X + base %08X -> %08X)\n", r, RES_COMMON(r), data, base, RES_DATA(r));
    c->r[0] = 0; X_RET(2);
}
void xv_hle_D3DResource_Release(xctx *c)
{ XD3D_COUNT("D3DResource_Release");
    uint32_t r = X_ARG(0); uint32_t rc = RES_COMMON(r) & X_D3DCOMMON_REFCOUNT_MASK;
    if (rc > 1) RES_COMMON(r)--; else if ((RES_COMMON(r) & X_D3DCOMMON_D3DCREATED) && RES_DATA(r)) { xk_phys_free(RES_DATA(r)); RES_DATA(r) = 0; RES_COMMON(r) &= ~X_D3DCOMMON_REFCOUNT_MASK; }
    c->r[0] = rc ? rc - 1 : 0; X_RET(1);
}
void xv_hle_D3DResource_AddRef(xctx *c) { XD3D_COUNT("D3DResource_AddRef"); RES_COMMON(X_ARG(0))++; c->r[0] = RES_COMMON(X_ARG(0)) & X_D3DCOMMON_REFCOUNT_MASK; X_RET(1); }
void xv_hle_D3DResource_IsBusy(xctx *c) { XD3D_COUNT("D3DResource_IsBusy"); c->r[0] = 0; X_RET(1); }
void xv_hle_D3DResource_BlockUntilNotBusy(xctx *c) { XD3D_COUNT("D3DResource_BlockUntilNotBusy"); X_RET(1); }
void xv_hle_D3DResource_GetDevice(xctx *c) { XD3D_COUNT("D3DResource_GetDevice"); X_M32(X_ARG(1)) = g_xd3d_device; c->r[0] = 0; X_RET(2); }

/* ---- locking: hand the game CPU pointers into the physical storage ---------------------------- */
static void level_geom(uint32_t hdr, unsigned level, unsigned *w, unsigned *h, unsigned *d, unsigned *fmt, uint32_t *offset, unsigned *pitch)
{
    uint32_t f = PC_FORMAT(hdr), s = PC_SIZE(hdr); *fmt = (f >> 8) & 0xFF;
    unsigned W, H, D = 1;
    if (s) { W = (s & 0xFFF) + 1; H = ((s >> 12) & 0xFFF) + 1; } else { W = 1u << ((f >> 20) & 0xF); H = 1u << ((f >> 24) & 0xF); D = 1u << ((f >> 28) & 0xF); }
    int cube = (f & 4) != 0; uint32_t off = 0;
    for (unsigned l = 0; l < level; ++l) { off += level_bytes(*fmt, W, H, D) * (cube ? 6 : 1); W = W > 1 ? W >> 1 : 1; H = H > 1 ? H >> 1 : 1; D = D > 1 ? D >> 1 : 1; }
    *w = W; *h = H; *d = D; *offset = off;
    *pitch = fmt_is_dxt(*fmt) ? ((W + 3) / 4) * (*fmt == 0x0C ? 8 : 16) : (s ? (((s >> 24) & 0xFF) + 1) * 64 : W * fmt_bits(*fmt) / 8);
}
/* D3DTexture_LockRect(pThis, Level, pLockedRect { INT Pitch; void *pBits; }, pRect, Flags) */
void xv_hle_D3DTexture_LockRect(xctx *c)
{ XD3D_COUNT("D3DTexture_LockRect");
    uint32_t t = X_ARG(0), lr = X_ARG(2); unsigned w, h, d, fmt, pitch; uint32_t off;
    level_geom(t, X_ARG(1), &w, &h, &d, &fmt, &off, &pitch);
    X_M32(lr) = pitch; X_M32(lr + 4) = GUEST_PTR(RES_DATA(t) + off);
    c->r[0] = 0; X_RET(5);
}
void xv_hle_D3DSurface_LockRect(xctx *c) { XD3D_COUNT("D3DSurface_LockRect"); uint32_t s = X_ARG(0), lr = X_ARG(1); unsigned w, h, d, fmt, pitch; uint32_t off; level_geom(s, 0, &w, &h, &d, &fmt, &off, &pitch); X_M32(lr) = pitch; X_M32(lr + 4) = GUEST_PTR(RES_DATA(s)); c->r[0] = 0; X_RET(4); }
/* D3DCubeTexture_LockRect(pThis, FaceType, Level, pLockedRect, pRect, Flags) */
void xv_hle_D3DCubeTexture_LockRect(xctx *c)
{ XD3D_COUNT("D3DCubeTexture_LockRect");
    uint32_t t = X_ARG(0), lr = X_ARG(3); unsigned w, h, d, fmt, pitch; uint32_t off;
    level_geom(t, X_ARG(2), &w, &h, &d, &fmt, &off, &pitch);
    uint32_t face = level_bytes(fmt, w, h, 1) * X_ARG(1);
    X_M32(lr) = pitch; X_M32(lr + 4) = GUEST_PTR(RES_DATA(t) + off + face);
    c->r[0] = 0; X_RET(6);
}
/* D3DVolumeTexture_LockBox(pThis, Level, pLockedBox { RowPitch, SlicePitch, pBits }, pBox, Flags) */
void xv_hle_D3DVolumeTexture_LockBox(xctx *c)
{ XD3D_COUNT("D3DVolumeTexture_LockBox");
    uint32_t t = X_ARG(0), lb = X_ARG(2); unsigned w, h, d, fmt, pitch; uint32_t off;
    level_geom(t, X_ARG(1), &w, &h, &d, &fmt, &off, &pitch);
    X_M32(lb) = pitch; X_M32(lb + 4) = pitch * h; X_M32(lb + 8) = GUEST_PTR(RES_DATA(t) + off);
    c->r[0] = 0; X_RET(5);
}
/* D3DVertexBuffer_Lock(pThis, OffsetToLock, SizeToLock, ppbData, Flags) */
void xv_hle_D3DVertexBuffer_Lock(xctx *c) { XD3D_COUNT("D3DVertexBuffer_Lock"); X_M32(X_ARG(3)) = GUEST_PTR(RES_DATA(X_ARG(0)) + X_ARG(1)); c->r[0] = 0; X_RET(5); }
void xv_hle_D3DIndexBuffer_Lock(xctx *c) { XD3D_COUNT("D3DIndexBuffer_Lock"); X_M32(X_ARG(3)) = GUEST_PTR(RES_DATA(X_ARG(0)) + X_ARG(1)); c->r[0] = 0; X_RET(5); }
void xv_hle_D3DPalette_Lock(xctx *c) { XD3D_COUNT("D3DPalette_Lock"); X_M32(X_ARG(1)) = GUEST_PTR(RES_DATA(X_ARG(0))); c->r[0] = 0; X_RET(3); }
/* D3DSurface_GetDesc(pThis, pDesc { Format, Type, Usage, Pool, Size, MultiSampleType, Width, Height }) */
static void fill_desc(uint32_t s, uint32_t desc)
{
    unsigned w, h, d, fmt, pitch; uint32_t off; level_geom(s, 0, &w, &h, &d, &fmt, &off, &pitch);
    X_M32(desc) = fmt; X_M32(desc + 4) = 1; X_M32(desc + 8) = 0; X_M32(desc + 12) = 0; X_M32(desc + 16) = level_bytes(fmt, w, h, d); X_M32(desc + 20) = 0; X_M32(desc + 24) = w; X_M32(desc + 28) = h;
}
void xv_hle_D3DSurface_GetDesc(xctx *c) { XD3D_COUNT("D3DSurface_GetDesc"); fill_desc(X_ARG(0), X_ARG(1)); c->r[0] = 0; X_RET(2); }
void xv_hle_Get2DSurfaceDesc(xctx *c) { XD3D_COUNT("Get2DSurfaceDesc"); fill_desc(X_ARG(0), X_ARG(2)); X_RET(3); }
/* D3DTexture_GetSurfaceLevel(pThis, Level, ppSurface): a surface header aliasing the level */
void xv_hle_D3DTexture_GetSurfaceLevel(xctx *c)
{ XD3D_COUNT("D3DTexture_GetSurfaceLevel");
    uint32_t t = X_ARG(0); unsigned w, h, d, fmt, pitch; uint32_t off; level_geom(t, X_ARG(1), &w, &h, &d, &fmt, &off, &pitch);
    uint32_t s = new_header(X_D3DCOMMON_TYPE_SURFACE, 64);
    RES_DATA(s) = RES_DATA(t) + off; RES_COMMON(s) &= ~X_D3DCOMMON_D3DCREATED;
    PC_FORMAT(s) = (PC_FORMAT(t) & ~0xFFF00000u) | (log2u(w) << 20) | (log2u(h) << 24); PC_SIZE(s) = PC_SIZE(t); SURF_PARENT(s) = t;
    RES_COMMON(t)++;
    X_M32(X_ARG(2)) = s; c->r[0] = 0; X_RET(3);
}

/* ---- DirectSound (silent) ------------------------------------------------------------------------ */
static uint32_t ds_obj(unsigned size) { uint32_t o = xk_kalloc(size); X_M32(o) = 1; { static unsigned n; if (n++ < 200 && getenv("XV_LOG_DS")) D3DLOG("ds_obj %08X (%u B)\n", o, size); } return o; }
/* Sound STREAMS are XMediaObjects: Halo drives them through the COM vtable ({QI, AddRef, Release, GetInfo,
 * GetStatus, Process, Discontinuity, Flush}), not the IDirectSoundStream_* C wrappers.  Point our objects
 * at the XBE's real CDirectSoundStream vtable (3925: 0x1D6CF4) and dispatch its methods here - they are
 * HLE symbols the recompiler only tables when called directly (xv_hle_extra is xv_call's second table). */
#define DS_STREAM_VTBL 0x001D6CF0u    /* CDirectSoundStream::vtbl: AddRef, Release, GetInfo, GetStatus, Process, Discontinuity, Flush (from the constructor at 0x1946EA) */
/* Stream packet pacing.  Bink (and the game's sound streamer) submit XMEDIAPACKETs through Process() and
 * spin until *pdwStatus leaves PENDING - with a silent sink that never happens and the intro movie hangs
 * the main thread.  Complete each packet after its real-time duration at the stream's byte rate, so the
 * movie runs at the speed its audio track dictates.  Completion is checked whenever the game touches the
 * sound system (DoWork once per frame, GetStatus/Process from the feeder). */
#include "xk_audio.h"
#define DS_MAX_STREAMS 128
#define DS_MAX_PKTS    8
typedef struct { uint32_t status_ptr, completed_ptr, size, event, context; uint64_t due_us; } ds_pkt;
typedef struct { uint32_t obj; uint32_t bytes_per_sec; uint32_t callback, cb_context, max_pkts; uint64_t tail_us; ds_pkt q[DS_MAX_PKTS]; int nq; int voice; } ds_stream;
static ds_stream g_ds_streams[DS_MAX_STREAMS];
static ds_stream *ds_stream_find(uint32_t obj) { for (int i = 0; i < DS_MAX_STREAMS; ++i) if (g_ds_streams[i].obj == obj) return &g_ds_streams[i]; return NULL; }
/* Completion: status/size out-params, the completion event, and - what Halo's mixer actually relies on -
 * the stream's DSSTREAMDESC.lpfnCallback(pStreamContext, pPacketContext, dwStatus) (__stdcall), which
 * decrements the channel's pending-packet count.  Without it the pump loop (0x28B00) spins forever once
 * a channel has 4 packets outstanding: that was the "freeze when the highlight moves" on hardware. */
static void ds_stream_pump(xctx *c, ds_stream *s)
{
    uint64_t now = xk_os_monotonic_us();
    /* complete a packet once the mixer has consumed it; fall back to its real-time due date (+300 ms slack)
     * so a stalled or absent audio device can never wedge Halo's packet pump */
    while (s->nq && ((xk_audio_available() && s->voice >= 0 && xk_audio_stream_pop_consumed(s->voice)) || s->q[0].due_us + 300000 <= now)) {
        ds_pkt p = s->q[0]; memmove(&s->q[0], &s->q[1], (size_t)(s->nq - 1) * sizeof p); s->nq--;
        if (p.completed_ptr) X_M32(p.completed_ptr) = p.size;
        if (p.status_ptr) X_M32(p.status_ptr) = 0;                        /* XMEDIAPACKET_STATUS_SUCCESS */
        if (p.event) { xk_obj *ev = xk_handle_get_type(p.event, XO_EVENT); if (ev) { ev->u.event.signaled = 1; xk_signal_check(); } }
        if (s->callback && c) { X_PUSH32(0); X_PUSH32(p.context); X_PUSH32(s->cb_context); X_PUSH32(0xDEAD0011u); xv_call(c, s->callback); }
    }
}
static void ds_pump_all(xctx *c) { for (int i = 0; i < DS_MAX_STREAMS; ++i) if (g_ds_streams[i].obj) ds_stream_pump(c, &g_ds_streams[i]); }
/* debug: detect a stream object whose vtable word got clobbered (called from the scheduler) */
void xd3d_ds_check(const char *where, uint32_t eip)
{
    static int reported;
    if (reported) return;
    for (int i = 0; i < DS_MAX_STREAMS; ++i) if (g_ds_streams[i].obj && X_M32(g_ds_streams[i].obj) != DS_STREAM_VTBL) {
        reported = 1;
        D3DLOG("STREAM OBJECT %08X CLOBBERED: word0 %08X word1 %08X (%s, eip~%08X)\n", g_ds_streams[i].obj, X_M32(g_ds_streams[i].obj), X_M32(g_ds_streams[i].obj + 4), where, eip);
        if (xk_cur) { char sb[400]; int k = 0; uint32_t esp = xk_cur->ctx.r[4];
            for (unsigned j = 0; j < 96 && k < 380; ++j) { uint32_t w = X_M32(esp + 4 * j); if (w >= 0x11000 && w < 0x3A0000) k += snprintf(sb + k, sizeof sb - k, " %X", w); }
            D3DLOG("  thread %d esp %08X regs eax %08X ecx %08X edx %08X edi %08X esi %08X stack:%s\n", xk_cur->id, esp, xk_cur->ctx.r[0], xk_cur->ctx.r[1], xk_cur->ctx.r[2], xk_cur->ctx.r[7], xk_cur->ctx.r[6], sb);
            D3DLOG("  kernel heap sample: dev %08X kthread8 word %08X | host ptr of obj now %p (page-table entry %08X)\n", X_M32(0x03D00B40), X_M32(0x03D00640), X_G(g_ds_streams[i].obj), g_xpt[g_ds_streams[i].obj >> 12]); }
    }
}
static uint32_t ds_stream_obj(uint32_t desc)
{
    uint32_t o = xk_kalloc(256); X_M32(o) = DS_STREAM_VTBL; X_M32(o + 4) = 0x001D6CE4u;   /* second interface vtbl, as the real ctor sets */
    { static unsigned n; if (n++ < 200 && getenv("XV_LOG_DS")) D3DLOG("ds_stream_obj %08X host %p pte %08X\n", o, X_G(o), g_xpt[o >> 12]); }
    ds_stream *s = ds_stream_find(0);
    if (s) {
        memset(s, 0, sizeof *s); s->obj = o; s->bytes_per_sec = 192000; s->max_pkts = DS_MAX_PKTS;   /* 48 kHz 16-bit stereo default */
        /* DSSTREAMDESC { dwFlags, dwMaxAttachedPackets, lpwfxFormat, lpMixBins, lpfnCallback, lpvContext } */
        uint32_t wfx = desc ? X_M32(desc + 8) : 0;
        s->voice = xk_audio_voice_new(2, wfx);
        if (wfx) { uint32_t bps = X_M32(wfx + 8); if (bps >= 8000 && bps <= 2000000) s->bytes_per_sec = bps; }
        /* XDK 3925 DSSTREAMDESC has no lpMixBins: callback at +12, context at +16 (verified from Halo's calls) */
        if (desc) { uint32_t mp = X_M32(desc + 4); if (mp >= 1 && mp <= DS_MAX_PKTS) s->max_pkts = mp; s->callback = X_M32(desc + 12); s->cb_context = X_M32(desc + 16); }
        { static unsigned n; if (n++ < 3) D3DLOG("CreateSoundStream -> %08X desc %08X: %08X %08X %08X %08X %08X %08X %08X\n", o, desc, desc ? X_M32(desc) : 0, desc ? X_M32(desc + 4) : 0, desc ? X_M32(desc + 8) : 0, desc ? X_M32(desc + 12) : 0, desc ? X_M32(desc + 16) : 0, desc ? X_M32(desc + 20) : 0, desc ? X_M32(desc + 24) : 0); }
    }
    return o;
}
static void xv_hle_CDirectSoundStream_GetInfo(xctx *c)      { XD3D_COUNT("CDirectSoundStream_GetInfo"); uint32_t i = X_ARG(1); if (i) { X_M32(i) = 0; X_M32(i + 4) = 0; X_M32(i + 8) = 0; X_M32(i + 12) = 0; } c->r[0] = 0; X_RET(2); }   /* XMEDIAINFO: no fixed sizes */
static void xv_hle_CDirectSoundStream_GetStatus(xctx *c)
{ XD3D_COUNT("CDirectSoundStream_GetStatus");
    ds_stream *s = ds_stream_find(X_ARG(0)); if (s) ds_stream_pump(c, s);
    if (X_ARG(1)) X_M32(X_ARG(1)) = (!s || (uint32_t)s->nq < s->max_pkts) ? 0x1 : 0;   /* XMO_STATUSF_ACCEPT_INPUT_DATA */
    c->r[0] = 0; X_RET(2);
}
static void xv_hle_CDirectSoundStream_Process(xctx *c)
{ XD3D_COUNT("CDirectSoundStream_Process");
    ds_stream *s = ds_stream_find(X_ARG(0)); uint32_t pkt = X_ARG(1);      /* XMEDIAPACKET { pvBuffer, dwMaxSize, pdwCompletedSize, pdwStatus, hCompletionEvent, pContext } */
    if (s) ds_stream_pump(c, s);
    if (pkt && s && (uint32_t)s->nq < s->max_pkts) {
        uint32_t size = X_M32(pkt + 4);
        uint64_t now = xk_os_monotonic_us(), start = s->tail_us > now ? s->tail_us : now;
        ds_pkt *p = &s->q[s->nq++];
        /* 3925 XMEDIAPACKET = { pvBuffer, dwMaxSize, pdwCompletedSize, pdwStatus, pContext }: +0x10 is the packet
         * context Halo's callback keys on (a tag-data pointer, never an event handle) */
        p->size = size; p->completed_ptr = X_M32(pkt + 8); p->status_ptr = X_M32(pkt + 12); p->event = 0; p->context = X_M32(pkt + 16);
        p->due_us = start + (uint64_t)size * 1000000ull / (s->bytes_per_sec ? s->bytes_per_sec : 192000);
        s->tail_us = p->due_us;
        if (p->status_ptr) X_M32(p->status_ptr) = 1;                       /* XMEDIAPACKET_STATUS_PENDING */
        if (s->voice >= 0) xk_audio_stream_push(s->voice, X_M32(pkt), size);   /* pvBuffer */
        c->r[0] = 0;
    } else if (pkt && s) {
        c->r[0] = 0x80004005u;                                             /* E_FAIL: no packet slot (caller polls GetStatus) */
    } else if (pkt) {
        if (X_M32(pkt + 12)) X_M32(X_M32(pkt + 12)) = 0; if (X_M32(pkt + 8)) X_M32(X_M32(pkt + 8)) = X_M32(pkt + 4);   /* no stream state: complete now */
        c->r[0] = 0;
    } else c->r[0] = 0;
    X_RET(3);
}
static void xv_hle_CDirectSoundStream_Discontinuity(xctx *c){ XD3D_COUNT("CDirectSoundStream_Discontinuity"); c->r[0] = 0; X_RET(1); }
static void xv_hle_CDirectSoundStream_Flush(xctx *c)
{
    { ds_stream *fs = ds_stream_find(X_ARG(0)); if (fs && fs->voice >= 0) xk_audio_stream_flush(fs->voice); } XD3D_COUNT("CDirectSoundStream_Flush");
    ds_stream *s = ds_stream_find(X_ARG(0));
    if (s) { int n = s->nq; ds_pkt q[DS_MAX_PKTS]; memcpy(q, s->q, sizeof q); s->nq = 0; s->tail_us = 0;
        for (int i = 0; i < n; ++i) { if (q[i].status_ptr) X_M32(q[i].status_ptr) = 2;                          /* FLUSHED */
            if (s->callback) { X_PUSH32(2); X_PUSH32(q[i].context); X_PUSH32(s->cb_context); X_PUSH32(0xDEAD0011u); xv_call(c, s->callback); } } }
    c->r[0] = 0; X_RET(1);
}
static void xv_hle_CDirectSoundStream_QueryInterface(xctx *c){ XD3D_COUNT("CDirectSoundStream_QueryInterface"); if (X_ARG(2)) X_M32(X_ARG(2)) = X_ARG(0); c->r[0] = 0; X_RET(3); }
const xv_fn_entry_t xv_hle_extra[] = {
    { 0x001943BDu, xv_hle_CDirectSoundStream_QueryInterface },
    { 0x001937AAu, xv_hle_CDirectSoundStream_GetInfo },          /* vtbl+0x08 */
    { 0x0019384Fu, xv_hle_CDirectSoundStream_GetStatus },        /* vtbl+0x0C: Halo tests bit 0 (ACCEPT_INPUT_DATA) */
    { 0x00193884u, xv_hle_CDirectSoundStream_Process },          /* vtbl+0x10: (this, pPacket, NULL) */
    { 0x001937F5u, xv_hle_CDirectSoundStream_Discontinuity },    /* vtbl+0x14 */
    { 0x00193822u, xv_hle_CDirectSoundStream_Flush },            /* vtbl+0x18 */
    { 0, 0 } };
/* Two tiny DSOUND helpers Halo calls on its voice wrappers ([wrapper+0x24] = the DSound object).  Lifted
 * they would poke real CDirectSoundBuffer internals; HLE'd (--hle-addr) they report "not playing" and
 * a no-op stop, which keeps the channel pump moving. */
void xv_hle_DSoundVoiceIsPlaying(xctx *c) { XD3D_COUNT("DSoundVoiceIsPlaying"); c->r[0] = 0; X_RET(1); }
void xv_hle_DSoundVoiceStop(xctx *c)      { XD3D_COUNT("DSoundVoiceStop"); c->r[0] = 0; X_RET(1); }
void xv_hle_DirectSoundCreate(xctx *c) { XD3D_COUNT("DirectSoundCreate"); { static int up; if (!up) { up = 1; extern int xk_audio_start(void); xk_audio_init(); if (xk_audio_start() != 0) D3DLOG("audio thread failed\n"); } } uint32_t ds = ds_obj(256); D3DLOG("DirectSoundCreate -> %08X\n", ds); X_M32(X_ARG(1)) = ds; c->r[0] = 0; X_RET(3); }
void xv_hle_DirectSoundDoWork(xctx *c) { XD3D_COUNT("DirectSoundDoWork"); ds_pump_all(c); X_RET(0); }
void xv_hle_DirectSoundUseFullHRTF(xctx *c) { XD3D_COUNT("DirectSoundUseFullHRTF"); X_RET(0); }
void xv_hle_DirectSoundEnterCriticalSection(xctx *c) { XD3D_COUNT("DirectSoundEnterCriticalSection"); X_RET(0); }
/* Sound BUFFERS.  Bink feeds the intro/attract movies through a looping DirectSound buffer and paces the
 * video off GetCurrentPosition, so the buffer needs real semantics: backing memory (app-supplied via
 * SetBufferData or allocated from DSBUFFERDESC.dwBufferBytes), Lock returning pointers into it, and a play
 * cursor that advances in real time at the format's byte rate while playing.  Silent otherwise. */
#define DS_MAX_BUFFERS 256
typedef struct { uint32_t obj, data, size, bytes_per_sec, base_pos; uint64_t start_us; int playing, looping; int voice; } ds_buffer;
static ds_buffer g_ds_buffers[DS_MAX_BUFFERS];
static ds_buffer *ds_buffer_find(uint32_t obj) { for (int i = 0; i < DS_MAX_BUFFERS; ++i) if (g_ds_buffers[i].obj == obj) return &g_ds_buffers[i]; return NULL; }
static uint32_t ds_buffer_obj(uint32_t desc)
{
    uint32_t o = ds_obj(256); ds_buffer *b = ds_buffer_find(0);
    if (!b) return o;
    memset(b, 0, sizeof *b); b->obj = o; b->bytes_per_sec = 192000; b->voice = -1;
    if (desc) {
        uint32_t bytes = X_M32(desc + 8), wfx = X_M32(desc + 12);                /* DSBUFFERDESC { dwSize, dwFlags, dwBufferBytes, lpwfxFormat, lpMixBins, dwInputMixBin } */
        if (wfx) { uint32_t bps = X_M32(wfx + 8); if (bps >= 8000 && bps <= 2000000) b->bytes_per_sec = bps; }
        if (bytes && bytes <= (8u << 20)) { b->size = bytes; b->data = xk_kalloc(bytes); }
        b->voice = xk_audio_voice_new(1, wfx);
        if (b->data) xk_audio_voice_set_data(b->voice, b->data, b->size);
    } else b->voice = xk_audio_voice_new(1, 0);
    return o;
}
static uint32_t ds_buffer_pos(ds_buffer *b)
{
    if (!b->size) return 0;
    if (xk_audio_available() && b->voice >= 0) {                        /* the mixer's cursor is the truth */
        if (b->playing && !xk_audio_voice_playing(b->voice)) { b->playing = 0; b->base_pos = 0; return 0; }
        return xk_audio_voice_pos(b->voice) % b->size;
    }
    if (!b->playing) return b->base_pos % b->size;
    uint64_t adv = (xk_os_monotonic_us() - b->start_us) * b->bytes_per_sec / 1000000ull + b->base_pos;
    if (b->looping) return (uint32_t)(adv % b->size);
    if (adv >= b->size) { b->playing = 0; b->base_pos = 0; return 0; }   /* one-shot finished */
    return (uint32_t)adv;
}
void xv_hle_DirectSoundCreateBuffer(xctx *c) { XD3D_COUNT("DirectSoundCreateBuffer"); X_M32(X_ARG(1)) = ds_buffer_obj(X_ARG(0)); c->r[0] = 0; X_RET(2); }
void xv_hle_IDirectSound_CreateSoundBuffer(xctx *c) { XD3D_COUNT("IDirectSound_CreateSoundBuffer"); X_M32(X_ARG(2)) = ds_buffer_obj(X_ARG(1)); c->r[0] = 0; X_RET(4); }
void xv_hle_IDirectSound_CreateSoundStream(xctx *c) { XD3D_COUNT("IDirectSound_CreateSoundStream"); X_M32(X_ARG(2)) = ds_stream_obj(X_ARG(1)); c->r[0] = 0; X_RET(4); }
void xv_hle_IDirectSound_DownloadEffectsImage(xctx *c) { XD3D_COUNT("IDirectSound_DownloadEffectsImage"); if (X_ARG(4)) X_M32(X_ARG(4)) = ds_obj(64); c->r[0] = 0; X_RET(5); }
/* DSCAPS { dwFree2DBuffers, dwFree3DBuffers, dwFreeBufferSGEs, dwMemoryAllocated } */
void xv_hle_IDirectSound_GetCaps(xctx *c) { XD3D_COUNT("IDirectSound_GetCaps"); X_M32(X_ARG(1)) = 256; X_M32(X_ARG(1) + 4) = 64; X_M32(X_ARG(1) + 8) = 4096; X_M32(X_ARG(1) + 12) = 0; c->r[0] = 0; X_RET(2); }
void xv_hle_IDirectSound_GetSpeakerConfig(xctx *c) { XD3D_COUNT("IDirectSound_GetSpeakerConfig"); X_M32(X_ARG(1)) = 0x00000001; c->r[0] = 0; X_RET(2); }   /* DSSPEAKER_STEREO */
void xv_hle_IDirectSound_Release(xctx *c) { XD3D_COUNT("IDirectSound_Release"); c->r[0] = 0; X_RET(1); }
void xv_hle_DSound_CRefCount_AddRef(xctx *c) { XD3D_COUNT("DSound_CRefCount_AddRef"); c->r[0] = 2; X_RET(1); }
void xv_hle_DSound_CRefCount_Release(xctx *c) { XD3D_COUNT("DSound_CRefCount_Release"); c->r[0] = 1; X_RET(1); }
#define DS_OK(name, n) void xv_hle_##name(xctx *c) { c->r[0] = 0; X_RET(n); }
DS_OK(IDirectSound_CommitDeferredSettings, 1) DS_OK(IDirectSound_SetDistanceFactor, 3) DS_OK(IDirectSound_SetI3DL2Listener, 3)
DS_OK(IDirectSound_SetMixBinHeadroom, 3) DS_OK(IDirectSound_SetOrientation, 8) DS_OK(IDirectSound_SetPosition, 5)
DS_OK(IDirectSound_SetRolloffFactor, 3) DS_OK(IDirectSound_SetVelocity, 5)
void xv_hle_IDirectSoundBuffer_GetStatus(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_GetStatus"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) ds_buffer_pos(b); if (X_ARG(1)) X_M32(X_ARG(1)) = b ? ((b->playing ? 0x1 : 0) | (b->looping ? 0x4 : 0)) : 0; c->r[0] = 0; X_RET(2); }
/* Lock(pThis, dwOffset, dwBytes, ppvAudioPtr1, pdwAudioBytes1, ppvAudioPtr2, pdwAudioBytes2, dwFlags) */
void xv_hle_IDirectSoundBuffer_Lock(xctx *c)
{
    XD3D_COUNT("IDirectSoundBuffer_Lock");
    ds_buffer *b = ds_buffer_find(X_ARG(0)); uint32_t off = X_ARG(1), bytes = X_ARG(2);
    uint32_t p1 = 0, n1 = 0, p2 = 0, n2 = 0;
    if (b && b->data && b->size) {
        if (X_ARG(7) & 0x2) bytes = b->size;                                   /* DSBLOCK_ENTIREBUFFER */
        off %= b->size; if (bytes > b->size) bytes = b->size;
        n1 = b->size - off < bytes ? b->size - off : bytes; p1 = b->data + off;
        if (bytes > n1) { n2 = bytes - n1; p2 = b->data; }
    }
    if (X_ARG(3)) X_M32(X_ARG(3)) = p1; if (X_ARG(4)) X_M32(X_ARG(4)) = n1;
    if (X_ARG(5)) X_M32(X_ARG(5)) = p2; if (X_ARG(6)) X_M32(X_ARG(6)) = n2;
    c->r[0] = (b && b->data) ? 0 : 0x88780032u;                              /* DSERR_INVALIDPARAM */
    X_RET(8);
}
void xv_hle_IDirectSoundBuffer_Play(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_Play"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) { b->base_pos = ds_buffer_pos(b); b->start_us = xk_os_monotonic_us(); b->playing = 1; b->looping = (X_ARG(3) & 1) != 0; xk_audio_voice_play(b->voice, b->looping); } c->r[0] = 0; X_RET(4); }
void xv_hle_IDirectSoundBuffer_Release(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_Release"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) { xk_audio_voice_free(b->voice); b->obj = 0; } c->r[0] = 0; X_RET(1); }
void xv_hle_IDirectSoundBuffer_SetBufferData(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetBufferData"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b && X_ARG(1)) { b->data = X_ARG(1); b->size = X_ARG(2); xk_audio_voice_set_data(b->voice, b->data, b->size); } c->r[0] = 0; X_RET(3); }
void xv_hle_IDirectSoundBuffer_SetCurrentPosition(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetCurrentPosition"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) { b->base_pos = X_ARG(1); b->start_us = xk_os_monotonic_us(); xk_audio_voice_set_pos(b->voice, X_ARG(1)); } c->r[0] = 0; X_RET(2); }
void xv_hle_IDirectSoundBuffer_SetFrequency(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetFrequency"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) xk_audio_voice_set_frequency(b->voice, X_ARG(1)); c->r[0] = 0; X_RET(2); }
void xv_hle_IDirectSoundBuffer_SetVolume(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetVolume"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) xk_audio_voice_set_volume_db100(b->voice, (int32_t)X_ARG(1)); c->r[0] = 0; X_RET(2); }
void xv_hle_IDirectSoundBuffer_SetLoopRegion(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetLoopRegion"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) xk_audio_voice_set_loop(b->voice, X_ARG(1), X_ARG(2)); c->r[0] = 0; X_RET(3); }
void xv_hle_IDirectSoundBuffer_SetFormat(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_SetFormat"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b && X_ARG(1)) { xk_audio_voice_set_format(b->voice, X_ARG(1)); uint32_t bps = X_M32(X_ARG(1) + 8); if (bps >= 8000 && bps <= 2000000) b->bytes_per_sec = bps; } c->r[0] = 0; X_RET(2); }
DS_OK(IDirectSoundBuffer_SetHeadroom, 2) DS_OK(IDirectSoundBuffer_SetMixBins, 2)
DS_OK(IDirectSoundBuffer_SetPitch, 2) DS_OK(IDirectSoundBuffer_Unlock, 5)
void xv_hle_IDirectSoundBuffer_Stop(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_Stop"); ds_buffer *b = ds_buffer_find(X_ARG(0)); if (b) { b->base_pos = ds_buffer_pos(b); b->playing = 0; xk_audio_voice_stop(b->voice); } c->r[0] = 0; X_RET(1); }
DS_OK(IDirectSoundStream_SetConeAngles, 4) DS_OK(IDirectSoundStream_SetConeOrientation, 5) DS_OK(IDirectSoundStream_SetConeOutsideVolume, 3)
DS_OK(IDirectSoundStream_SetI3DL2Source, 3) DS_OK(IDirectSoundStream_SetMaxDistance, 3) DS_OK(IDirectSoundStream_SetMinDistance, 3)
DS_OK(IDirectSoundStream_SetMixBinVolumes_12, 3) DS_OK(IDirectSoundStream_SetMode, 3) DS_OK(IDirectSoundStream_SetPosition, 5)
DS_OK(IDirectSoundStream_SetVelocity, 5) DS_OK(CDirectSoundBufferSettings_SetBufferData, 2) DS_OK(CDirectSoundStream_AddRef, 1)
DS_OK(CDirectSoundStream_Release, 1) DS_OK(CDirectSoundStream_SetMixBins, 2)
void xv_hle_CDirectSoundStream_SetFrequency(xctx *c) { XD3D_COUNT("CDirectSoundStream_SetFrequency"); ds_stream *s = ds_stream_find(X_ARG(0)); if (s) xk_audio_voice_set_frequency(s->voice, X_ARG(1)); c->r[0] = 0; X_RET(2); }
void xv_hle_CDirectSoundStream_SetVolume(xctx *c) { XD3D_COUNT("CDirectSoundStream_SetVolume"); ds_stream *s = ds_stream_find(X_ARG(0)); if (s) xk_audio_voice_set_volume_db100(s->voice, (int32_t)X_ARG(1)); c->r[0] = 0; X_RET(2); }
DS_OK(CMcpxAPU_Commit3dSettings, 1) DS_OK(CMcpxStream_Flush, 0) DS_OK(CMcpxVoiceClient_Commit3dSettings_4, 1)
static void ds_report_pos(ds_buffer *b, uint32_t pplay, uint32_t pwrite)
{
    uint32_t play = 0, write = 0;
    if (b && b->size) { play = ds_buffer_pos(b); write = (play + b->bytes_per_sec / 16) % b->size; }   /* write cursor ~62 ms ahead */
    if (pplay) X_M32(pplay) = play; if (pwrite) X_M32(pwrite) = write;
}
void xv_hle_IDirectSoundBuffer_GetCurrentPosition(xctx *c) { XD3D_COUNT("IDirectSoundBuffer_GetCurrentPosition"); ds_report_pos(ds_buffer_find(X_ARG(0)), X_ARG(1), X_ARG(2)); c->r[0] = 0; X_RET(3); }
void xv_hle_CMcpxBuffer_GetCurrentPosition(xctx *c) { XD3D_COUNT("CMcpxBuffer_GetCurrentPosition"); ds_report_pos(ds_buffer_find(X_ARG(0)), X_ARG(0) ? X_ARG(1) : 0, X_ARG(2)); c->r[0] = 0; X_RET(2); }

/* ---- Bink ------------------------------------------------------------------------------------------ */
/* Movies are skipped: BinkOpen returns NULL and Halo falls through to the menu.  Decoding 640x480 Bink in
 * recompiled C is far beyond the Vita's CPU budget, and the movie code path was only ever reached once the
 * recompiler's block-order bug was fixed (before that the game skipped them by accident). */
void xv_hle_BinkOpen(xctx *c) { XD3D_COUNT("BinkOpen"); D3DLOG("BinkOpen(\"%s\", %08X) -> NULL (movies disabled)\n", xk_gstr(X_ARG(0)), X_ARG(1)); c->r[0] = 0; X_RET(2); }
