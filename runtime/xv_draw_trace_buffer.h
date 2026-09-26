/* One diagnostic frame, single recording writer. Begin/end run on the owner
 * with the deferred recorder drained; the existing queue handoff publishes
 * this storage. Never write files or acquire the log mutex from the worker. */
#ifndef XV_DRAW_TRACE_BUFFER_H
#define XV_DRAW_TRACE_BUFFER_H
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef XV_DRAW_TRACE_BYTES
#define XV_DRAW_TRACE_BYTES (1024u * 1024u)
#endif
static struct {
    char *text;
    unsigned active, used, dropped;
} xv_draw_trace;
static void xv_draw_trace_begin(int active)
{
    xv_draw_trace.active = active != 0;
    xv_draw_trace.used = xv_draw_trace.dropped = 0;
    xv_draw_trace.text = active ? malloc(XV_DRAW_TRACE_BYTES) : NULL;
}
/* Returns acceptance, not proof of a successful eventual file write. */
static int xv_draw_trace_append(const char *format, ...)
{
    if (!xv_draw_trace.active) return 0;
    unsigned available = xv_draw_trace.text ? XV_DRAW_TRACE_BYTES - xv_draw_trace.used : 0;
    if (!available) { ++xv_draw_trace.dropped; return 0; }
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(xv_draw_trace.text + xv_draw_trace.used, available, format, ap);
    va_end(ap);
    if (n < 0 || (unsigned)n >= available) {
        ++xv_draw_trace.dropped;
        return 0; /* Discard the entire line, including any truncated suffix. */
    }
    xv_draw_trace.used += (unsigned)n;
    return 1;
}
static void xv_draw_trace_end(unsigned frame)
{
    if (!xv_draw_trace.active) return;
    if (xv_draw_trace.used) xv_log_critical_write(xv_draw_trace.text, xv_draw_trace.used);
    xv_log_criticalf("[draw-trace-buffer] frame %u bytes %u dropped-lines %u allocated %u; diagnostic timing excluded\n",
        frame, xv_draw_trace.used, xv_draw_trace.dropped, xv_draw_trace.text != NULL);
    free(xv_draw_trace.text);
    xv_draw_trace.text = NULL;
    xv_draw_trace.active = xv_draw_trace.used = xv_draw_trace.dropped = 0;
}
#endif
