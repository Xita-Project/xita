#include "xk_query_cpu.h"
#include "../xv_x86rt.h"
#include <string.h>

_Static_assert(sizeof(((xctx *)0)->st) == sizeof(((XvQueryCpu *)0)->st_entry), "ST raw size");
_Static_assert(sizeof(((xctx *)0)->xmm) == sizeof(((XvQueryCpu *)0)->xmm_entry), "XMM raw size");

#define SCALARS(F) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) \
    F(f_cf_override) F(f_cf) F(f_of_override) F(f_of) F(fsp) F(fsw) F(fcw)

static int overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x <= y ? y - x < an : x - y < bn;
}

static int context_ok(const XvQueryCpu *s, const xctx *c)
{ return s && c && !overlap(s, sizeof(*s), c, sizeof(*c)); }

static int active(const XvQueryCpu *s)
{ return s && s->status == XV_QCPU_RECORDING; }

void xv_query_cpu_invalidate(XvQueryCpu *s, unsigned reason)
{
    if (s) { s->status = XV_QCPU_INVALID; s->reason |= reason ? reason : XV_QCPU_UNKNOWN; }
}

static int fail(XvQueryCpu *s, unsigned reason)
{ xv_query_cpu_invalidate(s, reason); return 0; }

static void scalar_get(XvQueryCpuScalar *out, const xctx *c)
{
    memset(out, 0, sizeof(*out));
    memcpy(out->r, c->r, sizeof(out->r));
    out->fs_base = c->fs_base;
#define GET(name) out->name = c->name;
    SCALARS(GET)
#undef GET
}

static void scalar_put(xctx *c, const XvQueryCpuScalar *in)
{
    memcpy(c->r, in->r, sizeof(in->r));
    /* fs_base is an entry key and must stay unchanged, not a replay output. */
#define PUT(name) c->name = in->name;
    SCALARS(PUT)
#undef PUT
}

static void untouched_get(XvQueryCpuUntouched *out, const xctx *c)
{
    memcpy(out->mm, c->mm, sizeof(out->mm));
    out->scratch = c->scratch; out->eip_hint = c->eip_hint;
    out->fiber = (uintptr_t)c->fiber;
    out->id_flag = 0;
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    out->id_flag = c->id_flag;
#endif
}

static int untouched_match(const XvQueryCpuUntouched *in, const xctx *c)
{
    if (memcmp(in->mm, c->mm, sizeof(in->mm)) || in->scratch != c->scratch ||
        in->eip_hint != c->eip_hint || in->fiber != (uintptr_t)c->fiber) return 0;
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    if (in->id_flag != c->id_flag) return 0;
#endif
    return 1;
}

void xv_query_cpu_st_read(XvQueryCpu *s, unsigned index)
{
    if (!active(s)) return;
    if (index >= 8) { fail(s, XV_QCPU_BOUNDS); return; }
    s->st_reads |= (UINT32_C(1) << index) & ~s->st_writes;
}

void xv_query_cpu_st_write(XvQueryCpu *s, unsigned index)
{
    if (!active(s)) return;
    if (index >= 8) { fail(s, XV_QCPU_BOUNDS); return; }
    s->st_writes |= UINT32_C(1) << index;
}

void xv_query_cpu_xmm_read(XvQueryCpu *s, uint32_t mask)
{ if (active(s)) s->xmm_reads |= mask & ~s->xmm_writes; }
void xv_query_cpu_xmm_write(XvQueryCpu *s, uint32_t mask)
{ if (active(s)) s->xmm_writes |= mask; }

int xv_query_cpu_begin(XvQueryCpu *s, const xctx *c, uint32_t fpscr)
{
    if (!s) return 0;
    s->status = XV_QCPU_INVALID; s->reason = 0;
    if (!context_ok(s, c)) return fail(s, XV_QCPU_BOUNDS);
    if (c->fsp >= 8) return fail(s, XV_QCPU_BOUNDS);
    if (c->preempt <= 0) return fail(s, XV_QCPU_BUDGET);
    if (fpscr & XV_QUERY_CPU_TRAPS) return fail(s, XV_QCPU_FP);
    scalar_get(&s->entry, c); untouched_get(&s->untouched, c);
    memcpy(s->st_entry, c->st, sizeof(s->st_entry));
    memcpy(s->xmm_entry, c->xmm, sizeof(s->xmm_entry));
    s->st_reads = s->st_writes = s->xmm_reads = s->xmm_writes = 0;
    s->budget_entry = c->preempt; s->consumed = 0;
    s->fpscr_entry = fpscr; s->status = XV_QCPU_RECORDING;
    return 1;
}

int xv_query_cpu_finish(XvQueryCpu *s, const xctx *c, uint32_t fpscr)
{
    unsigned i;
    if (!active(s)) return fail(s, XV_QCPU_STATE);
    if (!context_ok(s, c) || c->fsp >= 8) return fail(s, XV_QCPU_BOUNDS);
    if (c->preempt <= 0 || c->preempt > s->budget_entry) return fail(s, XV_QCPU_BUDGET);
    if (fpscr & XV_QUERY_CPU_TRAPS) return fail(s, XV_QCPU_FP);
    if (c->fs_base != s->entry.fs_base || !untouched_match(&s->untouched, c))
        return fail(s, XV_QCPU_UNTRACKED);
    memcpy(s->st_exit, c->st, sizeof(s->st_exit));
    memcpy(s->xmm_exit, c->xmm, sizeof(s->xmm_exit));
    for (i = 0; i < 8; ++i)
        if (!(s->st_writes & (UINT32_C(1) << i)) && s->st_entry[i] != s->st_exit[i])
            return fail(s, XV_QCPU_UNTRACKED);
    for (i = 0; i < 32; ++i)
        if (!(s->xmm_writes & (UINT32_C(1) << i)) && s->xmm_entry[i] != s->xmm_exit[i])
            return fail(s, XV_QCPU_UNTRACKED);
    scalar_get(&s->exit, c);
    s->fpscr_exit = fpscr;
    s->consumed = (uint32_t)s->budget_entry - (uint32_t)c->preempt;
    s->status = XV_QCPU_FINISHED;
    return 1;
}

int xv_query_cpu_validate(const XvQueryCpu *s, const xctx *c, uint32_t fpscr)
{
    XvQueryCpuScalar scalar;
    unsigned i;
    if (!context_ok(s, c) || s->status != XV_QCPU_FINISHED ||
        fpscr != s->fpscr_entry || c->preempt <= 0 || (uint32_t)c->preempt <= s->consumed) return 0;
    scalar_get(&scalar, c);
    if (memcmp(&scalar, &s->entry, sizeof(scalar))) return 0;
    for (i = 0; i < 8; ++i) if (s->st_reads & (UINT32_C(1) << i)) {
        uint64_t raw; memcpy(&raw, &c->st[i], sizeof(raw));
        if (raw != s->st_entry[i]) return 0;
    }
    for (i = 0; i < 32; ++i) if (s->xmm_reads & (UINT32_C(1) << i)) {
        uint32_t raw; memcpy(&raw, &c->xmm[i / 4][i % 4], sizeof(raw));
        if (raw != s->xmm_entry[i]) return 0;
    }
    return 1;
}

int xv_query_cpu_replay(const XvQueryCpu *s, xctx *c, uint32_t *fpscr)
{
    unsigned i;
    if (!fpscr || !context_ok(s, c) || overlap(fpscr, sizeof(*fpscr), s, sizeof(*s)) ||
        overlap(fpscr, sizeof(*fpscr), c, sizeof(*c)) || !xv_query_cpu_validate(s, c, *fpscr)) return 0;
    scalar_put(c, &s->exit);
    for (i = 0; i < 8; ++i) if (s->st_writes & (UINT32_C(1) << i))
        memcpy(&c->st[i], &s->st_exit[i], sizeof(s->st_exit[i]));
    for (i = 0; i < 32; ++i) if (s->xmm_writes & (UINT32_C(1) << i))
        memcpy(&c->xmm[i / 4][i % 4], &s->xmm_exit[i], sizeof(s->xmm_exit[i]));
    c->preempt = (int32_t)((uint32_t)c->preempt - s->consumed);
    *fpscr = s->fpscr_exit;
    return 1;
}
