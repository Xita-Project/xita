/* xv_trace_call - call tracing (default stubs always; instrumented call sites when XV_TRACE is set). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xv_x86rt.h"
#include "../kernel/xk.h"
int xv_trace_enabled;
static unsigned g_trace_after, g_trace_limit = 600;
extern unsigned xd3d_frame(void);
static const char *gstr(uint32_t a) { return xk_gstr(a); }
static const char *objname(uint32_t oa) { if (!oa) return "<null>"; uint32_t name = X_M32(oa + 4); return name ? gstr(X_M32(name + 4)) : "<noname>"; }
void xv_trace_init(void)
{
    const char *e = getenv("XV_TRACE");
    if (e) { xv_trace_enabled = 1; g_trace_after = (unsigned)atoi(e); }
    if (getenv("XV_TRACE_LIMIT")) g_trace_limit = (unsigned)atoi(getenv("XV_TRACE_LIMIT"));
}
void xv_trace_call(xctx *c, const char *name, unsigned nargs)
{
    static char last[320]; static unsigned repeats, total;
    if (xd3d_frame() < g_trace_after) return;
    char line[320]; int n = snprintf(line, sizeof line, "t%d %s(", xk_cur ? xk_cur->id : 0, name);
    for (unsigned i = 0; i < nargs && i < 6; ++i)
        n += snprintf(line + n, sizeof line - n, "%s%08X", i ? ", " : "", X_ARG(i));
    n += snprintf(line + n, sizeof line - n, "%s)", nargs > 6 ? ", ..." : "");
    if (!strcmp(name, "RtlInitAnsiString")) snprintf(line + n, sizeof line - n, "  \"%s\"", gstr(X_ARG(1)));
    else if (!strcmp(name, "NtOpenFile") || !strcmp(name, "NtCreateFile")) snprintf(line + n, sizeof line - n, "  \"%s\"", objname(X_ARG(2)));
    else if (!strcmp(name, "OutputDebugStringA") || !strcmp(name, "DbgPrint")) snprintf(line + n, sizeof line - n, "  \"%s\"", gstr(X_ARG(0)));
    if (!strcmp(line, last)) { repeats++; return; }
    if (repeats) fprintf(stderr, "    ... x%u more\n", repeats);
    repeats = 0; strcpy(last, line);
    if (total++ < g_trace_limit) fprintf(stderr, "[call] %s   [ret->%08X]\n", line, X_M32(c->r[4]));
}
