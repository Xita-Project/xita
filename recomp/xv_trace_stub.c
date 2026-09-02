/* xv_trace_stub.c - Vita build: the generated default stubs (xv_stubs_default.c) and the lifted
 * import call sites reference xv_trace_call / xv_trace_enabled.  On the host those live in
 * recomp/host/trace.c; here a minimal per-thread call trace goes to the on-card log so a hardware
 * run can be diffed against a Vita3K run of the same build (differential debugging - the only thing
 * that differs between the two is what our HLE hands back).  XV_TRACE_THREAD selects the guest
 * thread id (0 = off). */
#include "xv_x86rt.h"
#include "xk.h"
#include <stdio.h>

#ifndef XV_TRACE_THREAD
#define XV_TRACE_THREAD 0
#endif
int  xv_trace_enabled = XV_TRACE_THREAD != 0;

void xv_logf(const char *fmt, ...);
void xv_trace_call(xctx *c, const char *name, unsigned nargs)
{
    if (!xk_cur || xk_cur->id != XV_TRACE_THREAD) return;
    char buf[256]; int n = 0;
    uint32_t ret = X_M32(c->r[4]);                         /* return eip pushed by the call site */
    n += snprintf(buf + n, sizeof buf - n, "[tr] @%08X eax=%08X %s(", ret, c->r[0], name);
    if (nargs > 8) nargs = 8;
    for (unsigned i = 0; i < nargs && n < (int)sizeof buf - 12; ++i)
        n += snprintf(buf + n, sizeof buf - n, "%s%08X", i ? "," : "", X_M32(c->r[4] + 4 + 4 * i));
    /* caller's first three stack args ([ebp+8..0x10]) - e.g. RtlFreeHeap(heap, flags, block) */
    snprintf(buf + n, sizeof buf - n, ") f[%08X,%08X,%08X]\n", X_M32(c->r[5] + 8), X_M32(c->r[5] + 12), X_M32(c->r[5] + 16));
    xv_logf("%s", buf);
    if (ret == 0x1298F || ret == 0x00012BB7) {              /* XapiThreadNotify: dump the TLS chain */
        uint32_t sb = X_M32(c->fs_base + 4), idx = X_M32(0x262738), slot = sb + idx * 4, blk = X_M32(slot);
        uint32_t kt = X_M32(c->fs_base + 0x28), td = X_M32(kt + 0x28);
        xv_logf("[tr] TLS: StackBase %08X idx %08X slot %08X -> blk %08X [blk+4]=%08X [blk+8]=%08X | TlsData %08X [TlsData]=%08X ours tls %08X top %08X\n",
                sb, idx, slot, blk, X_M32(blk + 4), X_M32(blk + 8), td, X_M32(td), xk_cur->tls, xk_cur->stack_base);
    }
}
