/* XV_OCCL: skip the model pass's work for objects that nothing on screen shows.
 *
 * a30 (perf195/196, Sept 24 2026): in the lifepod intro cutscene and where the Chief spawns, 330-376 of ~450-475 draws
 * per frame pass no depth test at all, and the model pass 5B760 -> f_0005B4A0 (one call per render entry, ~180 per
 * frame) costs 75-80 ms of the scene helper's ~140 (A26B0 submitting each model: guest code plus D3D recording) - on
 * the Xbox the same objects were drawn (Halo culls by cluster, not by occlusion), just faster. From inside the pod the
 * camera counts as outdoors, so everything in the frustum is prepared, recorded and vertex-shaded behind the walls.
 *
 * Every object rendered by the 5B760 loop gets a slot in the frame (xv_d3d_occl_begin tags its draws) and, after the
 * loop, a proxy: the cube around its bounding sphere (object +50h/54h/58h center, +5Ch radius), projected with the
 * frame's world-to-clip rows (c[-96..-93]) and drawn as 12 triangles at the corners' own depths, depth-tested with
 * color and depth writes off and its own GPU sample counter (runtime/xv_d3d.c, command kind 3). After final completion
 * xv_occl_result() records per object whether any proxy sample passed. The cube contains the object, so an object
 * with a visible sample has a visible proxy sample in front of or at it (mode 1 counts violations: an object whose own
 * draws passed samples but whose proxy did not; objects covered by a later, nearer model count there too).
 *
 * XV_OCCL=1 verify: never skips; reports how many rendered objects had no visible proxy (what mode 2 would skip) and
 *                   violations (the object's own draws passed samples but its proxy did not - must stay 0).
 * XV_OCCL=2 skip:   an object whose latest proxy (at most XV_OCCL_AGE frames old, default 4) passed no sample is not
 *                   rendered (the loop's call returns at once) but still gets a proxy, so it comes back the frame
 *                   after it becomes visible (GPU latency: ~2-3 frames). No skipping while the camera turns more than
 *                   XV_OCCL_CUT_DEG (default 8) per frame or jumps (cutscene cuts), and never for objects whose cube
 *                   reaches behind the near plane. Default 0 (off). Hooks from tools/patch_occlusion_hooks.py. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void f_0005B4A0(xctx *restrict c);
extern unsigned xv_d3d_occl_begin(uint32_t handle, unsigned flags);
extern void xv_d3d_occl_end(void);
extern const float *xv_d3d_occl_matrix(void);
extern int xv_d3d_occl_proxy(unsigned slot, const float corners[8][3]);
extern uint32_t xv_d3d_occl_build_frame(void);

#define TAB 4096u
static struct { uint32_t handle, frame; uint8_t visible; } tab[TAB];   /* written by the pump, read by the recorder */
static struct { unsigned slot; float v[8][3]; } pend[255]; static unsigned npend;
static int mode = -1, age = 4; static float cut_cos = 0.990268f;
static float prev_fwd[3], prev_d; static int have_prev, cut_frames;
static unsigned n_render, n_skip, n_noproxy, n_untracked, n_cuts;
static volatile unsigned r_rendered, r_real_zero, r_proxy_zero, r_violation, r_skipped, r_reappear, r_both_zero;

static void config(void)
{
    const char *e = getenv("XV_OCCL"); mode = e ? atoi(e) : 0;
    e = getenv("XV_OCCL_AGE"); if (e) age = atoi(e);
    e = getenv("XV_OCCL_CUT_DEG"); if (e) cut_cos = cosf((float)atof(e) * 3.14159265f / 180.0f);
    if (mode) XK_LOG("[occl] mode %d (%s), cube proxies, result age <= %d frames, cut above %.1f deg/frame\n", mode,
                     mode == 1 ? "verify: never skips" : "skip hidden objects", age, acosf(cut_cos) * 180.0f / 3.14159265f);
}

static float f32(uint32_t a) { float v; uint32_t w = X_M32(a); memcpy(&v, &w, 4); return v; }
static unsigned hslot(uint32_t h) { return (h * 2654435761u) >> 20 & (TAB - 1u); }

/* The eight corners of the object's bounding cube in NDC (x/w, y/w, z/w); 0 when a corner is at or behind the near
 * plane (the caller then renders the object and records no proxy) or the cube is entirely off screen. */
static int object_cube(uint32_t body, float v[8][3])
{
    const float *m = xv_d3d_occl_matrix();
    float cx = f32(body + 0x50u), cy = f32(body + 0x54u), cz = f32(body + 0x58u), rad = f32(body + 0x5Cu);
    if (!(rad > 0.0f) || rad > 1000.0f || !isfinite(cx) || !isfinite(cy) || !isfinite(cz)) return 0;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (int k = 0; k < 8; ++k) {
        float px = cx + ((k & 1) ? rad : -rad), py = cy + ((k & 2) ? rad : -rad), pz = cz + ((k & 4) ? rad : -rad);
        float w = m[12] * px + m[13] * py + m[14] * pz + m[15];
        if (!(w > 0.05f)) return 0;
        v[k][0] = (m[0] * px + m[1] * py + m[2] * pz + m[3]) / w;
        v[k][1] = (m[4] * px + m[5] * py + m[6] * pz + m[7]) / w;
        v[k][2] = (m[8] * px + m[9] * py + m[10] * pz + m[11]) / w;
        if (!(v[k][2] > 0.0f)) return 0;
        if (v[k][0] < x0) x0 = v[k][0]; if (v[k][0] > x1) x1 = v[k][0]; if (v[k][1] < y0) y0 = v[k][1]; if (v[k][1] > y1) y1 = v[k][1];
    }
    if (x1 < -1.0f || y1 < -1.0f || x0 > 1.0f || y0 > 1.0f) return 0;    /* off screen: let the game decide */
    return 1;
}

/* Replaces the 5B760 loop's `call f_0005B4A0` (edi = render entry, [edi] = object handle; plain `ret`). */
void xv_occl_render(xctx *c)
{
    if (mode < 0) config();
    if (mode <= 0) { f_0005B4A0(c); return; }
    uint32_t handle = X_M32(c->r[7]);
    uint32_t hdr = X_M32(X_M32(0x2FC6ACu) + 0x34u) + (handle & 0xFFFFu) * 12u;
    uint32_t body = X_M32(hdr + 8u);
    if (handle == 0xFFFFFFFFu || X_M16(hdr) != (handle >> 16) || !body) { n_untracked++; f_0005B4A0(c); return; }
    float v[8][3]; int proxy = object_cube(body, v);
    uint32_t bf = xv_d3d_occl_build_frame();
    unsigned t = hslot(handle);
    int skip = mode == 2 && proxy && !cut_frames && tab[t].handle == handle && !tab[t].visible && bf - tab[t].frame <= (uint32_t)age;
    unsigned slot = xv_d3d_occl_begin(handle, skip ? 2u : 1u);
    if (skip) { c->r[4] += 4u; n_skip++; }
    else { f_0005B4A0(c); n_render++; }
    xv_d3d_occl_end();
    if (!slot) { n_untracked++; return; }
    if (!proxy) { n_noproxy++; return; }
    if (npend < 255u) { pend[npend].slot = slot; memcpy(pend[npend].v, v, sizeof v); npend++; }
}

/* After the 5B760 model pass returns (5D410): the opaque world is in the depth buffer; record the proxies. */
void xv_occl_after_models(void)
{
    if (mode <= 0) { npend = 0; return; }
    for (unsigned i = 0; i < npend; ++i) xv_d3d_occl_proxy(pend[i].slot, (const float (*)[3])pend[i].v);
    npend = 0;
    const float *m = xv_d3d_occl_matrix();   /* w row: the camera's forward axis (unnormalized) and its offset */
    float f[3] = { m[12], m[13], m[14] }, n = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n > 0.0f) { f[0] /= n; f[1] /= n; f[2] /= n; }
    float d = m[15] / (n > 0.0f ? n : 1.0f);
    if (have_prev && (f[0] * prev_fwd[0] + f[1] * prev_fwd[1] + f[2] * prev_fwd[2] < cut_cos || fabsf(d - prev_d) > 1.0f)) { cut_frames = 3; n_cuts++; }
    else if (cut_frames) cut_frames--;
    memcpy(prev_fwd, f, sizeof f); prev_d = d; have_prev = 1;
}

/* Pump thread, after final completion of `frame`: flags 1 rendered, 2 skipped, 4 proxy recorded. */
void xv_occl_result(uint32_t handle, uint32_t frame, uint32_t real, uint32_t proxy, unsigned flags)
{
    unsigned t = hslot(handle);
    if (tab[t].handle != handle || (int32_t)(frame - tab[t].frame) > 0) {
        tab[t].visible = !(flags & 4u) || proxy > 0 || ((flags & 1u) && real > 0);   /* own samples always win */
        tab[t].frame = frame; tab[t].handle = handle;
    }
    if (flags & 1u) {
        r_rendered++;
        if (!real) r_real_zero++;
        if ((flags & 4u) && !proxy) { r_proxy_zero++; if (real) r_violation++; else r_both_zero++; }
    } else if (flags & 2u) { r_skipped++; if (proxy) r_reappear++; }
}

void xv_occl_report(unsigned frames)
{
    if (mode <= 0 || !frames) return;
    XK_LOG("[occl] %u frames: rendered %u skipped %u (no proxy %u, untracked %u, camera cuts %u); results: rendered %u of which own draws empty %u, proxy empty %u (both empty %u, VIOLATIONS %u); skipped %u, back in view %u\n",
           frames, n_render, n_skip, n_noproxy, n_untracked, n_cuts, r_rendered, r_real_zero, r_proxy_zero, r_both_zero, r_violation, r_skipped, r_reappear);
    n_render = n_skip = n_noproxy = n_untracked = n_cuts = 0;
    r_rendered = r_real_zero = r_proxy_zero = r_violation = r_skipped = r_reappear = r_both_zero = 0;
}
