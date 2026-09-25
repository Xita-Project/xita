/* XV_OCCL: skip the model pass's work for objects that nothing on screen shows.
 *
 * a30 (perf195/196, Sept 24 2026): in the lifepod intro cutscene and where the Chief spawns, 330-376 of ~450-475 draws
 * per frame pass no depth test at all, and the model pass 5B760 -> f_0005B4A0 (one call per render entry, ~180 per
 * frame) costs 75-80 ms of the scene helper's ~140 (A26B0 submitting each model: guest code plus D3D recording) - on
 * the Xbox the same objects were drawn (Halo culls by cluster, not by occlusion), just faster. From inside the pod the
 * camera counts as outdoors, so everything in the frustum is prepared, recorded and vertex-shaded behind the walls.
 *
 * Every object rendered by the 5B760 loop gets a slot in the frame (xv_d3d_occl_begin tags its draws) and, after the
 * loop, a proxy: the screen rectangle of its bounding sphere's cube (object +50h/54h/58h center, +5Ch radius) at the
 * cube's nearest depth, drawn depth-tested with color and depth writes off and its own GPU sample counter
 * (runtime/xv_d3d.c, command kind 3). After final completion xv_occl_result() records per object whether any proxy
 * sample passed. The proxy is conservative: it covers the whole object at its nearest depth, so an object whose
 * draws pass any sample also passes its proxy (mode 1 counts violations of exactly that; objects covered later by a
 * nearer model, and draws without a depth test, count there too - an object's own samples therefore always mark it
 * visible). A projected cube per corner depth marked no more objects hidden in the pod and cost more GPU time
 * (perf199: +8 ms/frame), so the proxy stays a rectangle.
 *
 * XV_OCCL=1 verify: never skips; reports how many rendered objects had no visible proxy (what mode 2 would skip) and
 *                   violations (the object's own draws passed samples but its proxy did not - must stay 0).
 * XV_OCCL=2 skip:   an object whose latest proxy (at most XV_OCCL_AGE frames old, default 4) passed no sample is not
 *                   rendered (the loop's call returns at once) but still gets a proxy, so it comes back the frame
 *                   after it becomes visible (GPU latency: ~2-3 frames). No skipping for 3 frames after the camera turns
 *                   more than XV_OCCL_CUT_DEG (default 25) or moves more than XV_OCCL_CUT_DIST (default 8 world units,
 *                   along the view axis) in one frame - a cutscene cut; continuous pans and the falling lifepod's
 *                   camera (1-3 units/frame) keep skipping (was 8 deg / 1 unit: the a30 intro never skipped), and never for objects whose cube
 *                   reaches behind the near plane. Default 0 (off). Hooks from tools/patch_occlusion_hooks.py.
 * XV_OCCL_STATIC=<frames> (mode 2, default 4, 0 off): while the camera has not moved for two frames, an object that
 *                   has not moved since it was last rendered and whose own draws passed no sample in its last two
 *                   renders (its proxy passing - behind a door frame's edge, say) is rendered only every <frames>
 *                   frames. Any camera or object movement renders it at once; a static object revealed by something
 *                   else moving (a door) can come back up to <frames> frames late. */
#include "xk.h"
#include "xk_occlusion_results.h"
#include "../xv_x86rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void f_0005B4A0(xctx *restrict c);
extern unsigned xv_d3d_occl_begin(uint32_t handle, unsigned flags);
extern void xv_d3d_occl_end(void);
extern const float *xv_d3d_occl_matrix(void);
extern int xv_d3d_occl_proxy(unsigned slot, float x0, float y0, float x1, float y1, float z);
extern uint32_t xv_d3d_occl_build_frame(void);

#define TAB 4096u
static xv_occl_results result_queue;
static int results_reliable = 1;
static void apply_result(uint32_t, uint32_t, uint32_t, uint32_t, unsigned);
static void consume_results(void)
{
    xv_occl_event e;
    /* Bounded even if the pump continues publishing during this call. */
    for(unsigned n=0;n<XV_OCCL_RESULT_CAP && xv_occl_results_pop(&result_queue,&e);n++)
        apply_result(e.handle,e.frame,e.real,e.proxy,e.flags);
    if(__atomic_load_n(&result_queue.dropped,__ATOMIC_ACQUIRE)) results_reliable=0;
}

static struct {
    uint32_t handle, frame; uint8_t visible;   /* latest result, applied by the scene consumer */
    uint8_t empty_streak;                      /* scene consumer: consecutive renders with no own sample */
    uint32_t rendered; float c[3];             /* recorder: last frame it was rendered, and its center then */
} tab[TAB];
static struct { unsigned slot; float x0, y0, x1, y1, z; } pend[255]; static unsigned npend;
static int mode = -1, age = 4, static_every = 4, cam_still; static float cut_cos = 0.906308f, cut_dist = 8.0f, margin = 0.02f;
static float prev_fwd[3], prev_d, prev_m3, prev_m7; static int have_prev, cut_frames;
static unsigned n_render, n_skip, n_skip_static, n_noproxy, n_untracked, n_cuts;
static unsigned r_rendered, r_real_zero, r_proxy_zero, r_violation, r_skipped, r_reappear, r_both_zero;

static void config(void)
{
    const char *e = getenv("XV_OCCL"); mode = e ? atoi(e) : 0;
    e = getenv("XV_OCCL_AGE"); if (e) age = atoi(e);
    e = getenv("XV_OCCL_STATIC"); if (e) static_every = atoi(e);
    e = getenv("XV_OCCL_MARGIN"); if (e) margin = (float)atof(e);
    e = getenv("XV_OCCL_CUT_DEG"); if (e) cut_cos = cosf((float)atof(e) * 3.14159265f / 180.0f);
    e = getenv("XV_OCCL_CUT_DIST"); if (e) cut_dist = (float)atof(e);
    if (mode) XK_LOG("[occl] mode %d (%s), result age <= %d frames, rect margin %.3f, cut above %.1f deg/frame\n", mode,
                     mode == 1 ? "verify: never skips" : "skip hidden objects", age, margin, acosf(cut_cos) * 180.0f / 3.14159265f);
}

static float f32(uint32_t a) { float v; uint32_t w = X_M32(a); memcpy(&v, &w, 4); return v; }
static unsigned hslot(uint32_t h) { return (h * 2654435761u) >> 20 & (TAB - 1u); }

/* Screen rectangle and nearest depth of the object's bounding cube; 0 when a corner is at or behind the near plane. */
static int object_rect(uint32_t body, float *r)
{
    const float *m = xv_d3d_occl_matrix();
    float cx = f32(body + 0x50u), cy = f32(body + 0x54u), cz = f32(body + 0x58u), rad = f32(body + 0x5Cu);
    if (!(rad > 0.0f) || rad > 1000.0f || !isfinite(cx) || !isfinite(cy) || !isfinite(cz)) return 0;
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, zmin = 1e30f;
    for (int k = 0; k < 8; ++k) {
        float px = cx + ((k & 1) ? rad : -rad), py = cy + ((k & 2) ? rad : -rad), pz = cz + ((k & 4) ? rad : -rad);
        float w = m[12] * px + m[13] * py + m[14] * pz + m[15];
        if (!(w > 0.05f)) return 0;
        float x = (m[0] * px + m[1] * py + m[2] * pz + m[3]) / w, y = (m[4] * px + m[5] * py + m[6] * pz + m[7]) / w;
        float z = (m[8] * px + m[9] * py + m[10] * pz + m[11]) / w;
        if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; if (z < zmin) zmin = z;
    }
    if (!(zmin > 0.0f)) return 0;
    x0 -= margin; y0 -= margin; x1 += margin; y1 += margin;
    if (x1 < -1.0f || y1 < -1.0f || x0 > 1.0f || y0 > 1.0f) return 0;    /* off screen: let the game decide */
    r[0] = x0 < -1.0f ? -1.0f : x0; r[1] = y0 < -1.0f ? -1.0f : y0;
    r[2] = x1 > 1.0f ? 1.0f : x1;   r[3] = y1 > 1.0f ? 1.0f : y1; r[4] = zmin > 1.0f ? 1.0f : zmin;
    return 1;
}

/* Replaces the 5B760 loop's `call f_0005B4A0` (edi = render entry, [edi] = object handle; plain `ret`). */
void xv_occl_render(xctx *c)
{
    if (mode < 0) config();
    if (mode <= 0) { f_0005B4A0(c); return; }
    consume_results();
    uint32_t handle = X_M32(c->r[7]);
    uint32_t hdr = X_M32(X_M32(0x2FC6ACu) + 0x34u) + (handle & 0xFFFFu) * 12u;
    uint32_t body = X_M32(hdr + 8u);
    if (handle == 0xFFFFFFFFu || X_M16(hdr) != (handle >> 16) || !body) { n_untracked++; f_0005B4A0(c); return; }
    float r[5]; int proxy = object_rect(body, r);
    uint32_t bf = xv_d3d_occl_build_frame();
    unsigned t = hslot(handle);
    int known = tab[t].handle == handle;
    int skip = results_reliable && mode == 2 && proxy && !cut_frames && known && !tab[t].visible && bf - tab[t].frame <= (uint32_t)age;
    float cx = f32(body + 0x50u), cy = f32(body + 0x54u), cz = f32(body + 0x58u);
    if (!skip && results_reliable && mode == 2 && static_every > 1 && proxy && known && cam_still >= 2 && tab[t].empty_streak >= 2 &&
        bf - tab[t].rendered < (uint32_t)static_every) {
        float dx = cx - tab[t].c[0], dy = cy - tab[t].c[1], dz = cz - tab[t].c[2];
        if (dx * dx + dy * dy + dz * dz < 0.0004f) { skip = 1; n_skip_static++; }
    }
    unsigned slot = xv_d3d_occl_begin(handle, skip ? 2u : 1u);
    if (skip) { c->r[4] += 4u; n_skip++; }
    else {
        f_0005B4A0(c); n_render++;
        if (known) { tab[t].rendered = bf; tab[t].c[0] = cx; tab[t].c[1] = cy; tab[t].c[2] = cz; }
    }
    xv_d3d_occl_end();
    if (!slot) { n_untracked++; return; }
    if (!proxy) { n_noproxy++; return; }
    if (npend < 255u) { pend[npend].slot = slot; pend[npend].x0 = r[0]; pend[npend].y0 = r[1]; pend[npend].x1 = r[2]; pend[npend].y1 = r[3]; pend[npend].z = r[4]; npend++; }
}

/* After the 5B760 model pass returns (5D410): the opaque world is in the depth buffer; record the proxies. */
void xv_occl_after_models(void)
{
    if (mode <= 0) { npend = 0; return; }
    for (unsigned i = 0; i < npend; ++i) xv_d3d_occl_proxy(pend[i].slot, pend[i].x0, pend[i].y0, pend[i].x1, pend[i].y1, pend[i].z);
    npend = 0;
    const float *m = xv_d3d_occl_matrix();   /* w row: the camera's forward axis (unnormalized) and its offset */
    float f[3] = { m[12], m[13], m[14] }, n = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n > 0.0f) { f[0] /= n; f[1] /= n; f[2] /= n; }
    float d = m[15] / (n > 0.0f ? n : 1.0f);
    float fd = f[0] * prev_fwd[0] + f[1] * prev_fwd[1] + f[2] * prev_fwd[2];
    if (have_prev && (fd < cut_cos || fabsf(d - prev_d) > cut_dist)) { cut_frames = 3; n_cuts++; }
    else if (cut_frames) cut_frames--;
    /* still camera: under ~0.1 degree of turn and 0.01 units of offset change since the previous frame */
    if (have_prev && fd > 0.9999985f && fabsf(d - prev_d) < 0.01f && fabsf(m[3] - prev_m3) < 0.01f && fabsf(m[7] - prev_m7) < 0.01f) cam_still++;
    else cam_still = 0;
    prev_m3 = m[3]; prev_m7 = m[7];
    memcpy(prev_fwd, f, sizeof f); prev_d = d; have_prev = 1;
}

/* Pump thread, after final completion of `frame`: flags 1 rendered, 2 skipped, 4 proxy recorded. */
void xv_occl_result(uint32_t handle, uint32_t frame, uint32_t real, uint32_t proxy, unsigned flags)
{
    xv_occl_event e={handle,frame,real,proxy,flags};
    (void)xv_occl_results_push(&result_queue,e);
}

/* Only the scene consumer touches the table and result report counters. */
static void apply_result(uint32_t handle, uint32_t frame, uint32_t real, uint32_t proxy, unsigned flags)
{
    unsigned t = hslot(handle);
    if (tab[t].handle != handle) { tab[t].handle = handle; tab[t].empty_streak = 0; tab[t].rendered = 0; tab[t].frame = frame - 1u; }
    if ((int32_t)(frame - tab[t].frame) > 0) {
        tab[t].visible = !(flags & 4u) || proxy > 0 || ((flags & 1u) && real > 0);   /* own samples always win */
        tab[t].frame = frame;
    }
    if (flags & 1u) tab[t].empty_streak = real ? 0 : (tab[t].empty_streak < 255 ? tab[t].empty_streak + 1 : 255);
    if (flags & 1u) {
        r_rendered++;
        if (!real) r_real_zero++;
        if ((flags & 4u) && !proxy) { r_proxy_zero++; if (real) r_violation++; else r_both_zero++; }
    } else if (flags & 2u) { r_skipped++; if (proxy) r_reappear++; }
}

void xv_occl_report(unsigned frames)
{
    if (mode <= 0 || !frames) return;
    if (!results_reliable) XK_LOG("[occl] result queue overflow: culling disabled until restart\n");
    XK_LOG("[occl] %u frames: rendered %u skipped %u (%u static; no proxy %u, untracked %u, camera cuts %u); results: rendered %u of which own draws empty %u, proxy empty %u (both empty %u, VIOLATIONS %u); skipped %u, back in view %u\n",
           frames, n_render, n_skip, n_skip_static, n_noproxy, n_untracked, n_cuts, r_rendered, r_real_zero, r_proxy_zero, r_both_zero, r_violation, r_skipped, r_reappear);
    n_render = n_skip = n_skip_static = n_noproxy = n_untracked = n_cuts = 0;
    r_rendered = r_real_zero = r_proxy_zero = r_violation = r_skipped = r_reappear = r_both_zero = 0;
}
