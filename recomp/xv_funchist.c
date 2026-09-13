/* xv_funchist.c - --trace-funcs runtime: count recompiled function entries per frame.
 * XV_FUNC_HIST=<frame> (env / env.txt) turns counting on; the D3D Present of that frame dumps the
 * histogram (top entries by count) through xv_trace_func_dump(), then counting stops.  Cheap enough
 * (one open-addressing hash insert per call) to leave compiled in; xv_trace_funcs stays 0 otherwise. */
#include "xv_x86rt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int xv_trace_funcs = 0;
volatile uint32_t xv_cur_fn = 0;
#include "kernel/xk.h"          /* xk_os_log: host stderr / Vita on-card log */
#define xv_logf xk_os_log

#define FH_SIZE 32768u
static struct { uint32_t entry, n; } fh[FH_SIZE];
static unsigned fh_used;

void xv_trace_func(uint32_t entry)
{
    uint32_t h = (entry * 2654435761u) >> 17;
    for (unsigned i = 0; i < FH_SIZE; ++i) {
        uint32_t k = (h + i) & (FH_SIZE - 1);
        if (fh[k].entry == entry) { fh[k].n++; return; }
        if (!fh[k].entry) { if (fh_used < FH_SIZE - 1) { fh[k].entry = entry; fh[k].n = 1; fh_used++; } return; }
    }
}

/* XV_WATCH_FN=2D6A0,2D570,...: log entry (return address + 4 stack args) and return value (eax) of guest functions */
int xv_watch_n = -1; static uint32_t xv_watch[16]; static uint32_t xv_watch_lastptr[16];
static void watch_init(void)
{
    xv_watch_n = 0; const char *e = getenv("XV_WATCH_FN");
    while (e && *e && xv_watch_n < 16) { xv_watch[xv_watch_n++] = (uint32_t)strtoul(e, NULL, 16); while (*e && *e != ',') e++; if (*e == ',') e++; }
}
static int watched(uint32_t fn) { for (int i = 0; i < xv_watch_n; ++i) if (xv_watch[i] == fn) return 1; return 0; }
void xv_watch_enter(uint32_t fn, xctx *c)
{
    if (xv_watch_n < 0) { watch_init(); if (!xv_watch_n) return; }
    if (!watched(fn)) return;
    uint32_t sp = c->r[4];
    xv_logf("[watch] enter %05X from %05X esp %08X args %08X %08X %08X %08X eax %08X ecx %08X edx %08X\n", fn, X_M32(sp), sp, X_M32(sp + 4), X_M32(sp + 8), X_M32(sp + 12), X_M32(sp + 16), c->r[0], c->r[1], c->r[2]);
    for (unsigned a = 1; a <= 4; ++a) {              /* pointer-looking args: first 24 bytes */
        uint32_t v = X_M32(sp + 4 * a); if (v < 0x10000u || v >= 0x80000000u) continue;
        char b[80]; int n = 0; for (unsigned i = 0; i < 24; ++i) n += snprintf(b + n, sizeof b - n, "%02X", X_M8(v + i));
        char b2[80]; int n2 = 0; for (unsigned i = 0; i < 24; ++i) n2 += snprintf(b2 + n2, sizeof b2 - n2, "%02X", X_M8(v + 504 + i));   /* record tail +504..+527 */
        xv_logf("[watch]   arg%u @%08X: %s  +504: %s\n", a, v, b, b2);
        for (int i = 0; i < xv_watch_n; ++i) if (xv_watch[i] == fn) xv_watch_lastptr[i] = v;   /* remember the last pointer arg */
    }
}
void xv_watch_leave(uint32_t fn, uint32_t back, xctx *c)
{
    if (xv_watch_n <= 0 || !watched(fn)) return;
    xv_logf("[watch] leave %05X -> eax %08X (al %u) back in %05X\n", fn, c->r[0], c->r[0] & 0xFF, back);
    for (int i = 0; i < xv_watch_n; ++i) if (xv_watch[i] == fn && xv_watch_lastptr[i]) {   /* the out-buffer argument after the call */
        uint32_t v = xv_watch_lastptr[i]; char b[140]; int n = 0; for (unsigned k = 0; k < 48; ++k) n += snprintf(b + n, sizeof b - n, "%02X", X_M8(v + k));
        xv_logf("[watch]   out @%08X: %s\n", v, b); }
}

void xv_trace_func_reset(void) { memset(fh, 0, sizeof fh); fh_used = 0; }

static int cmp_desc(const void *a, const void *b)
{
    const uint32_t *x = a, *y = b;
    return x[1] < y[1] ? 1 : x[1] > y[1] ? -1 : (x[0] < y[0] ? -1 : 1);
}

/* which frame to sample: -1 = never; set from XV_FUNC_HIST at first call */
int xv_trace_func_frame(void)
{
    static int f = -2;
    if (f == -2) { const char *e = getenv("XV_FUNC_HIST"); f = e ? atoi(e) : -1; }
    return f;
}

void xv_trace_func_dump(const char *tag)
{
    static uint32_t rows[FH_SIZE][2]; unsigned n = 0;
    for (unsigned i = 0; i < FH_SIZE; ++i) if (fh[i].entry) { rows[n][0] = fh[i].entry; rows[n][1] = fh[i].n; n++; }
    qsort(rows, n, sizeof rows[0], cmp_desc);
    xv_logf("%s %u distinct functions this frame\n", tag, n);
    char line[200]; int ln = 0;
    for (unsigned i = 0; i < n; ++i) {
        ln += snprintf(line + ln, sizeof line - ln, " %X:%u", rows[i][0], rows[i][1]);
        if (ln > 160 || i + 1 == n) { xv_logf("%s%s\n", tag, line); ln = 0; }
    }
}

/* ---- sampling profiler: a 1 kHz thread samples the current guest function or
 * HLE call target and every 10k samples logs the top entries. Generated direct
 * calls, indirect dispatch and guest thread switches restore the caller marker.
 * These are wall-clock samples, not hardware CPU-cycle or GPU-time measurements. */
static struct { uint32_t fn, n; } ps[4096]; static unsigned ps_used, ps_total;
static void prof_sample(uint32_t fn)
{
    /* Zero means uninstrumented guest code. Keep it in the report instead of
     * incrementing an empty hash slot which prof_dump silently omits. */
    if (!fn) fn = UINT32_MAX;
    uint32_t h = (fn * 2654435761u) >> 20;
    for (unsigned i = 0; i < 4096; ++i) {
        uint32_t k = (h + i) & 4095;
        if (ps[k].fn == fn) { ps[k].n++; ps_total++; return; }
        if (!ps[k].fn) { if (ps_used < 4000) { ps[k].fn = fn; ps[k].n = 1; ps_used++; ps_total++; } return; }
    }
}
static void prof_dump(void)
{
    static uint32_t rows[4096][2]; unsigned n = 0;
    for (unsigned i = 0; i < 4096; ++i) if (ps[i].fn) { rows[n][0] = ps[i].fn; rows[n][1] = ps[i].n; n++; }
    qsort(rows, n, sizeof rows[0], cmp_desc);
    char line[220]; int ln = 0;
    xv_logf("[prof] %u samples, %u functions; top by time:\n", ps_total, n);
    for (unsigned i = 0; i < n && i < 40; ++i) {
        float percent = 100.0 * rows[i][1] / (ps_total ? ps_total : 1);
        if (rows[i][0] == UINT32_MAX)
            ln += snprintf(line + ln, sizeof line - ln, " unattributed:%.1f%%", percent);
        else
            ln += snprintf(line + ln, sizeof line - ln, rows[i][0] & 0x80000000u ? " H%X:%.1f%%" : " %X:%.1f%%", rows[i][0] & 0x7FFFFFFFu, percent);   /* Hxxxx = time inside the HLE called from guest address xxxx */
        if (ln > 170 || i + 1 == n || i == 39) { xv_logf("[prof]%s\n", line); ln = 0; }
    }
    memset(ps, 0, sizeof ps); ps_used = ps_total = 0;
    { extern void xk_wait_stats_request(void) __attribute__((weak)); if (xk_wait_stats_request) xk_wait_stats_request(); }
}
static void prof_thread(void *arg)
{
    (void)arg; unsigned t = 0;
    extern void xk_os_sleep_us(uint64_t us);
    for (;;) { xk_os_sleep_us(1000); prof_sample(xv_cur_fn); if (++t >= 10000) { t = 0; prof_dump(); } }
}
void xv_prof_start(void)
{
    extern void xv_phase_init(void) __attribute__((weak));
    if (xv_phase_init) xv_phase_init();
    /* Untraced entries do not resolve the lazy watch sentinel. Resolve it
     * here too, so native helpers avoid no-op watch dispatch in normal play. */
    if (xv_watch_n < 0) watch_init();
    extern const unsigned xv_guest_trace_enabled __attribute__((weak));
    int traced = &xv_guest_trace_enabled && xv_guest_trace_enabled;
    const char *e = getenv("XV_PROF");
    if (e ? !atoi(e) : !traced) { /* --trace-funcs builds collect by default; normal builds opt in */
        xv_logf("[prof] sampling profiler off; guest function tracing %s\n", traced ? "enabled" : "absent");
        return;
    }
    extern int xk_os_audio_thread_start(void (*fn)(void *), void *arg);      /* same helper: a plain thread */
    xv_logf("[prof] sampling profiler on (1 kHz); guest function tracing %s\n", traced ? "enabled" : "absent");
    xk_os_audio_thread_start(prof_thread, NULL);
}
