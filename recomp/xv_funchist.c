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

/* ---- sampling profiler (XV_PROF=1): a 1 kHz thread on another core samples xv_cur_fn (the function the
 * game thread entered most recently) and every 10 s logs the top entries.  Attribution is "last entered
 * function", so HLE work lands on the guest function that called it - good enough to find the hot spots. */
static struct { uint32_t fn, n; } ps[4096]; static unsigned ps_used, ps_total;
static void prof_sample(uint32_t fn)
{
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
        ln += snprintf(line + ln, sizeof line - ln, " %X:%.1f%%", rows[i][0], 100.0 * rows[i][1] / (ps_total ? ps_total : 1));
        if (ln > 170 || i + 1 == n || i == 39) { xv_logf("[prof]%s\n", line); ln = 0; }
    }
    memset(ps, 0, sizeof ps); ps_used = ps_total = 0;
    { extern void xk_wait_stats_dump(void) __attribute__((weak)); if (xk_wait_stats_dump) xk_wait_stats_dump(); }
}
static void prof_thread(void *arg)
{
    (void)arg; unsigned t = 0;
    extern void xk_os_sleep_us(uint64_t us);
    for (;;) { xk_os_sleep_us(1000); prof_sample(xv_cur_fn); if (++t >= 10000) { t = 0; prof_dump(); } }
}
void xv_prof_start(void)
{
    const char *e = getenv("XV_PROF"); if (e && !atoi(e)) return;          /* on by default in a --trace-funcs build; XV_PROF=0 disables */
    extern int xk_os_audio_thread_start(void (*fn)(void *), void *arg);      /* same helper: a plain thread */
    xv_logf("[prof] sampling profiler on (1 kHz)\n");
    xk_os_audio_thread_start(prof_thread, NULL);
}
