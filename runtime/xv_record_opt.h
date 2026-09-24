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
} xv_rec_opt;
#define XV_REC_OPT_INIT(name) { name, -1, 0, 0, 0, 0, 0 }

static inline int xv_rec_opt_mode(xv_rec_opt *o)
{
    if (o->mode < 0) {
        const char *e = getenv(o->env);
        int m = e ? atoi(e) : 0;
        o->mode = m >= 0 && m <= 2 ? m : 0;
    }
    return o->mode;
}

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
    if ((o)->mode > 0) \
        logf("[rec-verify] %s mode %d: %u frames %u checks %u mismatches (session %llu checks %llu mismatches)\n", \
            (o)->env, (o)->mode, (unsigned)(frames), (o)->checks, (o)->mismatches, \
            (unsigned long long)(o)->session_checks, (unsigned long long)(o)->session_mismatches); \
    (o)->checks = (o)->mismatches = 0; } while (0)
