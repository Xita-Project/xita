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
#include "xk_sound_cache_access.h"

extern unsigned xd3d_frame(void);
extern void f_001721B0(xctx *restrict c);

/* Diagnostic-only: actual collision work, separate from lookup/reuse overhead.
 * Synthetic phase ID is not a guest address. Default builds add no observers. */
#if defined(XV_SOUND_CACHE_PROFILE) && XV_SOUND_CACHE_PROFILE
static unsigned miss_reasons[16];
static unsigned endpoint_cells[6]; /* same/different/invalid grid cell, fresh then expired */
static void sound_cast(xctx *c)
{
    extern void xv_scene_phase_begin(uint32_t), xv_scene_phase_end(uint32_t);
    xv_scene_phase_begin(0xF01721B0u);
    f_001721B0(c);
    xv_scene_phase_end(0xF01721B0u);
}
#else
#define sound_cast(c) f_001721B0(c)
#endif

#define SLOTS 1024u
static struct entry { float a[3], b[3]; uint32_t frame; uint8_t used, hit; } tab[SLOTS];
static int mode = -1, verify; static float eps_l2, eps_s2;
static unsigned n_rays, n_reused, n_cast, n_other, n_verify_same, n_verify_diff;
static int configured, candidate_ways;
static xv_sound_cache_access candidate_cache;
static unsigned candidate_queries, candidate_hits, candidate_same, candidate_diff;
static unsigned candidate_unavailable, candidate_other_view;
static unsigned candidate_mismatch_reports;
#define CANDIDATE_ADD(counter) __atomic_fetch_add(&(counter), 1u, __ATOMIC_RELAXED)

/* Reserved for the world-transition integration. Candidate mode below always
 * casts and returns the guest result; it must not become a fast path until
 * world invalidation has a verified call site. */
void xv_sound_obstruction_cache_invalidate(void)
{
    xv_sound_cache_invalidate(&candidate_cache);
}

static void config(void)
{
    const char *e = getenv("XV_SOUND_OBSTRUCTION"); mode = e ? atoi(e) : 0;
    if (mode < 0) mode = 0; if (mode > 60) mode = 60;
    e = getenv("XV_SOUND_OBSTRUCTION_VERIFY"); verify = e && atoi(e) != 0;
    e = getenv("XV_SOUND_OBSTRUCTION_EPS_LISTENER"); float el = e ? (float)atof(e) : 0.3f; eps_l2 = el * el;
    e = getenv("XV_SOUND_OBSTRUCTION_EPS_SOUND"); float es = e ? (float)atof(e) : 0.1f; eps_s2 = es * es;
    e = getenv("XV_SOUND_CACHE_VERIFY_WAYS"); candidate_ways = e ? atoi(e) : 0;
    if (!xv_sound_cache_ways_valid((unsigned)candidate_ways)) candidate_ways = 0;
    if (mode) XK_LOG("[sound-obstruction] reuse a ray's answer up to %d frames while its ends move < %.2f (listener) / %.2f (sound)%s\n",
                     mode, el, es, verify ? " (verify: always casts)" : "");
    if (mode && candidate_ways) XK_LOG("[sound-cache-candidate] %d-way verification only; real cast/result always retained, world invalidation integration pending\n", candidate_ways);
}

static int configuration_ready(void)
{
    if (__atomic_load_n(&configured, __ATOMIC_ACQUIRE) == 2) return 1;
    int expected = 0;
    if (!__atomic_compare_exchange_n(&configured, &expected, 1, 0,
                                    __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return 0;
    config();
    __atomic_store_n(&configured, 2, __ATOMIC_RELEASE);
    return 1;
}

static float f32(uint32_t a) { float v; uint32_t w = X_M32(a); memcpy(&v, &w, 4); return v; }
static float d2(const float *a, const float *b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return x * x + y * y + z * z; }

void xv_sound_ray(xctx *c)
{
    /* A simultaneous first caller casts normally instead of waiting for config. */
    if (!configuration_ready()) { sound_cast(c); return; }
    uint32_t sp = c->r[4];                 /* [sp] return address 2B54E, then flags, start, vector, ignore, result */
    if (!mode || X_M32(sp + 4u) != 0xC0E1u || X_M32(sp + 16u) != 0xFFFFFFFFu) { if (mode) n_other++; sound_cast(c); return; }
    uint32_t pa = X_M32(sp + 8u), pv = X_M32(sp + 12u);
    float a[3] = { f32(pa), f32(pa + 4u), f32(pa + 8u) };
    float vector[3] = {f32(pv), f32(pv + 4u), f32(pv + 8u)};
    float b[3] = {a[0] + vector[0], a[1] + vector[1], a[2] + vector[2]};
    if (candidate_ways) {
        /* Avoid undefined float-to-int conversion in the diagnostic hash. */
        for (unsigned i = 0; i < 3; ++i) {
            if (!isfinite(a[i]) || !isfinite(b[i]) || b[i] < -1073741824.0f || b[i] >= 1073741824.0f) {
                CANDIDATE_ADD(candidate_unavailable); sound_cast(c); return;
            }
        }
    }
    /* slot by the sound end on a 0.5-unit grid: a sound's rays keep landing in the same slot while the listener walks */
    int32_t q[3] = { (int32_t)floorf(b[0] * 2.0f), (int32_t)floorf(b[1] * 2.0f), (int32_t)floorf(b[2] * 2.0f) };
    uint32_t h = ((uint32_t)q[0] * 73856093u) ^ ((uint32_t)q[1] * 19349663u) ^ ((uint32_t)q[2] * 83492791u);
    if (candidate_ways) {
        xv_sound_cache_query query = {0};
        query.ways = (unsigned)candidate_ways; query.hash = h;
        query.domain = (uintptr_t)X_PT; query.frame = xd3d_frame();
        query.lifetime = (uint32_t)mode;
        query.exact_ray = eps_l2 == 0 && eps_s2 == 0;
        memcpy(query.vector, vector, sizeof vector);
        query.listener_epsilon2 = eps_l2; query.sound_epsilon2 = eps_s2;
        memcpy(query.listener, a, sizeof a); memcpy(query.sound, b, sizeof b);
        xv_sound_cache_ticket ticket;
        uint8_t expected = 0;
        int found = xv_sound_cache_begin(&candidate_cache, &query, &ticket, &expected);
        CANDIDATE_ADD(candidate_queries);
        if (X_PT != g_xpt) CANDIDATE_ADD(candidate_other_view);
        if (!ticket.valid) CANDIDATE_ADD(candidate_unavailable);
        sound_cast(c); /* Keep complete guest state, not just AL, in verification. */
        uint8_t actual = (uint8_t)c->r[0];
        if (found) {
            CANDIDATE_ADD(candidate_hits);
            if (actual == expected) CANDIDATE_ADD(candidate_same);
            else {
                CANDIDATE_ADD(candidate_diff);
                if (__atomic_fetch_add(&candidate_mismatch_reports, 1u, __ATOMIC_RELAXED) < 32) {
                    const xv_sound_cache_entry *anchor = &ticket.matched;
                    XK_LOG("[sound-cache-mismatch] frame %u age %u view %p expected %u actual %u listener-d2 %.9g sound-d2 %.9g; listener %.9g/%.9g/%.9g sound %.9g/%.9g/%.9g\n",
                        query.frame, query.frame-anchor->frame, (void *)query.domain,
                        expected, actual, (double)d2(query.listener,anchor->listener),
                        (double)d2(query.sound,anchor->sound),
                        (double)a[0],(double)a[1],(double)a[2],
                        (double)b[0],(double)b[1],(double)b[2]);
                }
            }
        } else xv_sound_cache_commit(&candidate_cache, &ticket, actual);
        return;
    }
    struct entry *e = &tab[h & (SLOTS - 1u)];
    uint32_t frame = xd3d_frame();
    int reusable = e->used && frame - e->frame < (uint32_t)mode && d2(a, e->a) <= eps_l2 && d2(b, e->b) <= eps_s2;
    n_rays++;
#if defined(XV_SOUND_CACHE_PROFILE) && XV_SOUND_CACHE_PROFILE
    if (!reusable || verify) {
        /* Overlapping causes form a bitmask, not additive independent totals.
         * A displaced endpoint can mean motion OR a colliding cache slot;
         * the legacy cache has no stable sound identity to distinguish them. */
        unsigned reason = !e->used ? 1u :
            ((frame - e->frame >= (uint32_t)mode ? 2u : 0u) |
             (!(d2(a, e->a) <= eps_l2) ? 4u : 0u) |
             (!(d2(b, e->b) <= eps_s2) ? 8u : 0u));
        __atomic_fetch_add(&miss_reasons[reason], 1u, __ATOMIC_RELAXED);
        if (reason & 8u) {
            unsigned cell = 0;
            for (unsigned i = 0; i < 3; ++i) {
                if (!isfinite(e->b[i])) { cell = 2; break; }
                if (floorf(e->b[i] * 2.0f) != (float)q[i]) cell = 1;
            }
            /* Different spatial cells competing for this same slot is a
             * hash collision. Same cell still allows different sounds or
             * motion within the cell; neither is proof of safe reuse. */
            __atomic_fetch_add(&endpoint_cells[cell + ((reason & 2u) ? 3u : 0u)], 1u, __ATOMIC_RELAXED);
        }
    }
#endif
    if (reusable && !verify) {
        c->r[4] += 24u;                    /* ret 14h: return address + 5 arguments */
        c->r[0] = (c->r[0] & ~0xFFu) | e->hit;
        n_reused++; return;
    }
    sound_cast(c);
    uint8_t hit = (uint8_t)c->r[0];
    n_cast++;
    if (reusable) { if (hit == e->hit) n_verify_same++; else n_verify_diff++; return; }   /* verify keeps the old anchor */
    memcpy(e->a, a, sizeof a); memcpy(e->b, b, sizeof b); e->frame = frame; e->hit = hit; e->used = 1;
}

void xv_sound_obstruction_report(unsigned frames)
{
    if (__atomic_load_n(&configured, __ATOMIC_ACQUIRE) != 2 || mode <= 0 || !frames) return;
    if (candidate_ways) {
#define TAKE(counter) __atomic_exchange_n(&(counter), 0u, __ATOMIC_RELAXED)
        unsigned queries = TAKE(candidate_queries), hits = TAKE(candidate_hits);
        unsigned same = TAKE(candidate_same), diff = TAKE(candidate_diff);
        unsigned unavailable = TAKE(candidate_unavailable), other_view = TAKE(candidate_other_view);
#undef TAKE
        XK_LOG("[sound-cache-candidate] %u frames: queries %u hits %u same %u different %u unavailable %u other-view %u; always cast, asynchronous counters\n",
               frames, queries, hits, same, diff, unavailable, other_view);
        return;
    }
#if defined(XV_SOUND_CACHE_PROFILE) && XV_SOUND_CACHE_PROFILE
    for (unsigned cell = 0; cell < 6; ++cell) {
        unsigned n = __atomic_exchange_n(&endpoint_cells[cell], 0u, __ATOMIC_RELAXED);
        if (n) XK_LOG("[sound-cache-cells] %u frames: %s %s casts %u\n", frames,
            cell >= 3 ? "expired" : "fresh",
            cell % 3 == 0 ? "same-cell" : cell % 3 == 1 ? "different-cell" : "invalid-cell", n);
    }
    for (unsigned reason = 0; reason < 16; ++reason) {
        unsigned n = __atomic_exchange_n(&miss_reasons[reason], 0u, __ATOMIC_RELAXED);
        if (n) XK_LOG("[sound-cache-profile] %u frames: reason-mask %u casts %u (1 empty, 2 age, 4 listener, 8 endpoint; 0 verify-only)\n", frames, reason, n);
    }
#endif
    char tail[80] = "";
    if (verify) snprintf(tail, sizeof tail, "; verify: a reuse would answer the same %u, differently %u", n_verify_same, n_verify_diff);
    XK_LOG("[sound-obstruction] %u frames: rays %u reused %u cast %u (other flags %u)%s\n", frames, n_rays, n_reused, n_cast, n_other, tail);
    n_rays = n_reused = n_cast = n_other = n_verify_same = n_verify_diff = 0;
}
