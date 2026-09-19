#ifndef XK_QUERY_CPU_H
#define XK_QUERY_CPU_H
#include <stdint.h>

struct xctx;
#define XV_QUERY_CPU_TRAPS UINT32_C(0x00009f00)
enum { XV_QCPU_EMPTY, XV_QCPU_RECORDING, XV_QCPU_FINISHED, XV_QCPU_INVALID };
enum {
    XV_QCPU_STATE = 1u, XV_QCPU_BOUNDS = 2u, XV_QCPU_BUDGET = 4u,
    XV_QCPU_UNTRACKED = 8u, XV_QCPU_CALLBACK = 16u, XV_QCPU_FP = 32u,
    XV_QCPU_UNKNOWN = 64u
};

typedef struct {
    uint32_t r[8], fs_base, df;
    uint32_t f_kind, f_op1, f_op2, f_res, f_bits;
    uint32_t f_cf_override, f_cf, f_of_override, f_of, fsp;
    uint16_t fsw, fcw;
} XvQueryCpuScalar;
typedef struct {
    uint64_t mm[8];
    uint32_t scratch, eip_hint, id_flag;
    uintptr_t fiber;
} XvQueryCpuUntouched;
typedef struct {
    unsigned status, reason;
    XvQueryCpuScalar entry, exit;
    XvQueryCpuUntouched untouched;
    uint64_t st_entry[8], st_exit[8];
    uint32_t xmm_entry[32], xmm_exit[32];
    uint32_t st_reads, st_writes, xmm_reads, xmm_writes;
    uint32_t fpscr_entry, fpscr_exit, consumed;
    int32_t budget_entry;
} XvQueryCpu;

/* Explicit caller-owned state: no allocation/global/TLS or native FP access.
 * FPSCR is a supplied raw word. Its exact entry value and named scalar fields
 * are conservative keys. Physical ST slots and XMM lanes are keyed only when
 * read before their first recorded write. Notify reads BEFORE use and writes
 * AFTER evaluating operands, BEFORE storing, including same-value writes.
 * XMM bit i denotes xmm[i/4][i%4]; ST index is the physical index, not ST(i).
 * NULL/inactive notifications are ignored. Every callback/unknown effect must
 * invalidate; net budget comparison cannot detect a refill followed by work.
 */
void xv_query_cpu_st_read(XvQueryCpu *state, unsigned index);
void xv_query_cpu_st_write(XvQueryCpu *state, unsigned index);
void xv_query_cpu_xmm_read(XvQueryCpu *state, uint32_t mask);
void xv_query_cpu_xmm_write(XvQueryCpu *state, uint32_t mask);
void xv_query_cpu_invalidate(XvQueryCpu *state, unsigned reason);

/* begin initializes arbitrary storage or restarts an old record. State/context
 * must be stable, disjoint host allocations. Canonical FSP0..7, positive entry
 * and exit budget, and no native trap enables are required. finish rejects
 * increased budget, changed fs_base/MM/scratch/fiber/eip_hint/optional ID, and
 * ST/XMM changes outside the declared write masks. These checks cannot discover
 * an omitted read or an omitted same-value write: complete instrumentation is
 * still the caller's responsibility. No full old xctx is retained or replayed.
 */
int xv_query_cpu_begin(XvQueryCpu *state, const struct xctx *entry, uint32_t fpscr);
int xv_query_cpu_finish(XvQueryCpu *state, const struct xctx *exit, uint32_t fpscr);
/* Validation is read-only. Warm budget is not an exact key: it must be positive
 * and strictly greater than consumed. Replay validates everything first, then
 * publishes named scalar outputs, actual ST/XMM writes, consumed budget and
 * supplied FPSCR output. Other current context fields/slots are preserved.
 * fpscr must point to a separate uint32_t, disjoint from state and context.
 * Applying the supplied FPSCR to hardware is solely the caller's responsibility.
 */
int xv_query_cpu_validate(const XvQueryCpu *state, const struct xctx *current,
    uint32_t fpscr);
int xv_query_cpu_replay(const XvQueryCpu *state, struct xctx *current,
    uint32_t *fpscr);
#endif
