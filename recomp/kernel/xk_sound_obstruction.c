/* XV_SOUND_OBSTRUCTION=<frames>: reuse the answer of a looping sound's obstruction ray while its two endpoints stay
 * put, instead of casting the same collision ray again every frame.
 *
 * a30 (perf192, Sept 24 2026): the once-per-frame real-time update f_00108FD0 spent 30 of its 42 ms in the object
 * looping sound pass 2BB70 -> 2B700 -> 27700 -> 26A50 -> f_0002B460, ~110 calls per frame. f_0002B460 (a sound's
 * obstruction and gain) casts a collision vector f_001721B0(flags C0E1, start = the listener position, vector = sound
 * minus listener, ignore -1, result buffer) when the listener's and the sound's clusters see each other, at ~300 us per
 * ray, and only tests the returned AL (hit / no hit): the result buffer lives in f_0002B460's frame and is dead.
 *
 * The patch tool replaces that one call (at 2B549) with xv_sound_ray(). With the knob on, a ray whose start and end
 * are each within XV_SOUND_OBSTRUCTION_EPS_LISTENER (0.3) / _EPS_SOUND (0.1) world units of a ray cast at most
 * <frames> frames ago returns that ray's AL; everything else in f_0002B460 (the gain from the current distance, the
 * fields it writes) still runs as guest code. f_001721B0 is `ret 14h`, x87-balanced and its caller reads only AL,
 * so a reuse pops the return address and the five arguments and sets AL. What can differ is a ray that something
 * moved into since (audio only, at most <frames> frames late). XV_SOUND_OBSTRUCTION_VERIFY=1 always casts and
 * counts how often a reuse would have answered differently. Default 0 (off). */
#include "xk.h"
#include "../xv_x86rt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned xd3d_frame(void);
extern void f_001721B0(xctx *restrict c);

#define SLOTS 1024u
static struct entry { float a[3], b[3]; uint32_t frame; uint8_t used, hit; } tab[SLOTS];
static int mode = -1, verify; static float eps_l2, eps_s2;
static unsigned n_rays, n_reused, n_cast, n_other, n_verify_same, n_verify_diff;

static void config(void)
{
    const char *e = getenv("XV_SOUND_OBSTRUCTION"); mode = e ? atoi(e) : 0;
    if (mode < 0) mode = 0; if (mode > 60) mode = 60;
    e = getenv("XV_SOUND_OBSTRUCTION_VERIFY"); verify = e && atoi(e) != 0;
    e = getenv("XV_SOUND_OBSTRUCTION_EPS_LISTENER"); float el = e ? (float)atof(e) : 0.3f; eps_l2 = el * el;
    e = getenv("XV_SOUND_OBSTRUCTION_EPS_SOUND"); float es = e ? (float)atof(e) : 0.1f; eps_s2 = es * es;
    if (mode) XK_LOG("[sound-obstruction] reuse a ray's answer up to %d frames while its ends move < %.2f (listener) / %.2f (sound)%s\n",
                     mode, el, es, verify ? " (verify: always casts)" : "");
}

static float f32(uint32_t a) { float v; uint32_t w = X_M32(a); memcpy(&v, &w, 4); return v; }
static float d2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

void xv_sound_ray(xctx *c)
{
    if (mode < 0) config();
    uint32_t sp = c->r[4];                 /* [sp] return address 2B54E, then flags, start, vector, ignore, result */
    if (!mode || X_M32(sp + 4u) != 0xC0E1u || X_M32(sp + 16u) != 0xFFFFFFFFu) { if (mode) n_other++; f_001721B0(c); return; }
    uint32_t pa = X_M32(sp + 8u), pv = X_M32(sp + 12u);
    float a[3] = { f32(pa), f32(pa + 4u), f32(pa + 8u) };
    float b[3] = { a[0] + f32(pv), a[1] + f32(pv + 4u), a[2] + f32(pv + 8u) };
    /* slot by the sound end on a 0.5-unit grid: a sound's rays keep landing in the same slot while the listener walks */
    int32_t q[3] = { (int32_t)floorf(b[0] * 2.0f), (int32_t)floorf(b[1] * 2.0f), (int32_t)floorf(b[2] * 2.0f) };
    uint32_t h = ((uint32_t)q[0] * 73856093u) ^ ((uint32_t)q[1] * 19349663u) ^ ((uint32_t)q[2] * 83492791u);
    struct entry *e = &tab[h & (SLOTS - 1u)];
    uint32_t frame = xd3d_frame();
    int reusable = e->used && frame - e->frame < (uint32_t)mode && d2(a, e->a) <= eps_l2 && d2(b, e->b) <= eps_s2;
    n_rays++;
    if (reusable && !verify) {
        c->r[4] += 24u;                    /* ret 14h: return address + 5 arguments */
        c->r[0] = (c->r[0] & ~0xFFu) | e->hit;
        n_reused++; return;
    }
    f_001721B0(c);
    uint8_t hit = (uint8_t)c->r[0];
    n_cast++;
    if (reusable) { if (hit == e->hit) n_verify_same++; else n_verify_diff++; return; }   /* verify keeps the old anchor */
    memcpy(e->a, a, sizeof a); memcpy(e->b, b, sizeof b); e->frame = frame; e->hit = hit; e->used = 1;
}

void xv_sound_obstruction_report(unsigned frames)
{
    if (mode <= 0 || !frames) return;
    char tail[80] = "";
    if (verify) snprintf(tail, sizeof tail, "; verify: a reuse would answer the same %u, differently %u", n_verify_same, n_verify_diff);
    XK_LOG("[sound-obstruction] %u frames: rays %u reused %u cast %u (other flags %u)%s\n", frames, n_rays, n_reused, n_cast, n_other, tail);
    n_rays = n_reused = n_cast = n_other = n_verify_same = n_verify_diff = 0;
}
