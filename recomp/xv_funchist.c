/* xv_funchist.c - --trace-funcs runtime: count recompiled function entries per frame.
 * XV_FUNC_HIST=<frame> (env / env.txt) turns counting on; the D3D Present of that frame dumps the
 * histogram (top entries by count) through xv_trace_func_dump(), then counting stops.  Cheap enough
 * (one open-addressing hash insert per call) to leave compiled in; xv_trace_funcs stays 0 otherwise. */
#include "xv_x86rt.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int xv_trace_funcs = 0;
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
