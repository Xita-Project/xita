/* xv_record_opt.h - env-selected D3D recording optimizations with an in-process verify mode.
 *
 *   XV_<NAME>=0 (default) original path only
 *              1 verify: the original path runs and its result is used; the new path runs on the same inputs
 *                into scratch outputs, and every output is compared (mismatches counted, first ones logged)
 *              2 new path only
 *
 * Every knob reports "[rec-verify] <env> mode m: <checks> checks <mismatches> mismatches (session ...)" every 60
 * frames when its mode is not 0. XV_REC_VERIFY_ABORT=1 aborts at the first mismatch (host debugging). A knob is read
 * once, on the recording thread's first use; no knob changes what the original path records. */
#pragma once
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    const char *env;
    int mode;                       /* -1 until read */
    unsigned checks, mismatches;    /* since the last report */
    uint64_t session_checks, session_mismatches;
    unsigned logged;                /* mismatch details printed so far (bounded) */
    int ab;                         /* XV_REC_AB applies to this knob: -1 until read */
} xv_rec_opt;
#define XV_REC_OPT_INIT(name) { name, -1, 0, 0, 0, 0, 0, -1 }

/* XV_REC_AB=<frames> (measurement): every knob alternates between 0 and 2 each <frames> recorded frames, so one
 * run measures both paths under the same conditions (the Pi shares its L2/DRAM with other work; separate runs
 * drift by several percent). Knob state that the new paths rely on is maintained in both phases. The phase lives
 * in xd3d.c (xv_rec_ab_frame, called at each recording frame start); weak so standalone tests link without it. */
extern int xv_rec_ab_period __attribute__((weak));
extern unsigned xv_rec_ab_phase __attribute__((weak));
static inline int xv_rec_ab_active(void)
{
    if (!&xv_rec_ab_period) return 0;
    if (xv_rec_ab_period < 0) { const char *e = getenv("XV_REC_AB"); int p = e ? atoi(e) : 0; xv_rec_ab_period = p > 0 ? p : 0; }
    return xv_rec_ab_period > 0;
}

static inline int xv_rec_opt_configured(xv_rec_opt *o)
{
    if (o->mode < 0) {
        const char *e = getenv(o->env);
        int m = e ? atoi(e) : 0;
        o->mode = m >= 0 && m <= 2 ? m : 0;
    }
    return o->mode;
}
/* XV_REC_AB_KNOBS=<env>[,<env>...] limits the A/B alternation to these knobs; the others keep their configured mode
 * (measure new knobs on top of the ones already in use). Unset: every knob alternates. */
static inline int xv_rec_opt_ab(xv_rec_opt *o)
{
    if (o->ab < 0) {
        const char *list = getenv("XV_REC_AB_KNOBS");
        int in = !list;
        if (list) {
            size_t n = 0; while (o->env[n]) n++;
            for (const char *p = list; *p; ) {
                const char *e = p; while (*e && *e != ',') e++;
                size_t k = 0; while (k < n && p + k < e && p[k] == o->env[k]) k++;
                if (k == n && p + n == e) { in = 1; break; }
                p = *e ? e + 1 : e;
            }
        }
        o->ab = in;
    }
    return o->ab;
}
/* The path to take now: the configured mode, or the A/B phase (0 or 2). */
static inline int xv_rec_opt_mode(xv_rec_opt *o)
{
    int m = xv_rec_opt_configured(o);
    if (xv_rec_ab_active() && xv_rec_opt_ab(o)) return xv_rec_ab_phase ? 2 : 0;
    return m;
}
/* Whether state used by the new path (indexes, second hash tables) must be kept current. */
static inline int xv_rec_opt_maintain(xv_rec_opt *o) { return xv_rec_opt_configured(o) > 0 || (xv_rec_ab_active() && xv_rec_opt_ab(o)); }

/* Record one comparison. Returns nonzero when the caller should print mismatch details (the first 8). */
static inline int xv_rec_opt_result(xv_rec_opt *o, int equal)
{
    o->checks++; o->session_checks++;
    if (equal) return 0;
    o->mismatches++; o->session_mismatches++;
    static int abort_on = -1;
    if (abort_on < 0) { const char *e = getenv("XV_REC_VERIFY_ABORT"); abort_on = e && atoi(e) != 0; }
    if (abort_on) abort();
    return o->logged++ < 8;
}

/* Report and reset through the caller's logger (xv_logf on the runtime side, D3DLOG in the kernel). */
#define XV_REC_OPT_REPORT(o, frames, logf) do { \
    if ((o)->mode == 1) \
        logf("[rec-verify] %s mode %d: %u frames %u checks %u mismatches (session %llu checks %llu mismatches)\n", \
            (o)->env, (o)->mode, (unsigned)(frames), (o)->checks, (o)->mismatches, \
            (unsigned long long)(o)->session_checks, (unsigned long long)(o)->session_mismatches); \
    (o)->checks = (o)->mismatches = 0; } while (0)
