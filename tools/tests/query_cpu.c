#include "xk_query_cpu.h"
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static XvQueryCpu state;
static xctx entry, cold_exit;
static const uint32_t fp_entry = 0x63000090u, fp_exit = 0xa3000095u;

static uint64_t st_bits(const xctx *c, unsigned i)
{ uint64_t u; memcpy(&u, &c->st[i], sizeof(u)); return u; }
static void st_put(xctx *c, unsigned i, uint64_t u)
{ memcpy(&c->st[i], &u, sizeof(u)); }
static uint32_t xmm_bits(const xctx *c, unsigned i)
{ uint32_t u; memcpy(&u, &c->xmm[i / 4][i % 4], sizeof(u)); return u; }
static void xmm_put(xctx *c, unsigned i, uint32_t u)
{ memcpy(&c->xmm[i / 4][i % 4], &u, sizeof(u)); }

static void setup(void)
{
    unsigned i;
    memset(&entry, 0xa5, sizeof(entry));
    for (i = 0; i < 8; ++i) {
        entry.r[i] = 0x12340000u + 0x111u * i;
        st_put(&entry, i, UINT64_C(0x3ff0000000000100) + i);
        entry.mm[i] = UINT64_C(0xabcd111122223333) + i;
    }
    for (i = 0; i < 32; ++i) xmm_put(&entry, i, 0x7f800001u + i);
    entry.fs_base = 0x43210000; entry.df = 0;
    entry.f_kind = XK_SUB; entry.f_op1 = 123; entry.f_op2 = 456;
    entry.f_res = 789; entry.f_bits = 32;
    entry.f_cf_override = 1; entry.f_cf = 0;
    entry.f_of_override = 0; entry.f_of = 1;
    entry.fsp = 3; entry.fsw = 0x713f; entry.fcw = 0x023f;
    entry.scratch = 0x1234; entry.eip_hint = 0x5678;
    entry.fiber = (void *)(uintptr_t)0x123400;
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    entry.id_flag = 1;
#endif
    entry.preempt = 100;
    assert(xv_query_cpu_begin(&state, &entry, fp_entry));
}

static void finish_program(void)
{
    unsigned i;
    cold_exit = entry;
    xv_query_cpu_st_read(&state, 2);
    xv_query_cpu_st_write(&state, 2);
    st_put(&cold_exit, 2, st_bits(&cold_exit, 2) ^ UINT64_C(0x8000000000000000));
    xv_query_cpu_st_write(&state, 6); /* Same-value cold write must remain a write. */
    st_put(&cold_exit, 6, st_bits(&entry, 6));
    xv_query_cpu_st_read(&state, 6); /* Read after write is not an entry dependency. */
    xv_query_cpu_st_write(&state, 5);
    st_put(&cold_exit, 5, UINT64_C(0x7ff0000000000001)); /* Preserve signaling NaN bits. */
    xv_query_cpu_xmm_read(&state, UINT32_C(1) << 31);
    xv_query_cpu_xmm_write(&state, 0x1ff);
    for (i = 0; i < 8; ++i) xmm_put(&cold_exit, i, 0xff800101u + i);
    xmm_put(&cold_exit, 8, xmm_bits(&entry, 8)); /* Same-value lane write. */
    xv_query_cpu_xmm_read(&state, 0x1ff);
    for (i = 0; i < 8; ++i) cold_exit.r[i] ^= 0x55aa;
    cold_exit.df = 1; cold_exit.f_kind = XK_ADD;
    cold_exit.f_op1 = 901; cold_exit.f_op2 = 902; cold_exit.f_res = 903;
    cold_exit.f_bits = 8; cold_exit.f_cf_override = 0; cold_exit.f_cf = 1;
    cold_exit.f_of_override = 1; cold_exit.f_of = 0;
    cold_exit.fsp = 4; cold_exit.fsw = 0x7800; cold_exit.fcw = 0x027f;
    cold_exit.preempt = 83;
    assert(xv_query_cpu_finish(&state, &cold_exit, fp_exit));
    assert(state.consumed == 17 && state.st_reads == (1u << 2));
    assert(state.st_writes == ((1u << 2) | (1u << 5) | (1u << 6)));
    assert(state.xmm_reads == (UINT32_C(1) << 31) && state.xmm_writes == 0x1ff);
}

static void reject(const xctx *input, uint32_t fpscr)
{
    xctx current = *input, saved = current;
    XvQueryCpu saved_state = state;
    uint32_t saved_fp = fpscr;
    assert(!xv_query_cpu_validate(&state, &current, fpscr));
    assert(!xv_query_cpu_replay(&state, &current, &fpscr));
    assert(!memcmp(&current, &saved, sizeof(current)) && fpscr == saved_fp);
    assert(!memcmp(&state, &saved_state, sizeof(state)));
}

static void exact_replay(void)
{
    unsigned i;
    xctx warm, expected;
    uint32_t fpscr = fp_entry;
    setup(); finish_program(); warm = entry;
    for (i = 0; i < 8; ++i) if (i != 2) st_put(&warm, i, UINT64_C(0xfff0000000000100) + i);
    for (i = 0; i < 31; ++i) xmm_put(&warm, i, 0x80001000u + i);
    for (i = 0; i < 8; ++i) warm.mm[i] ^= UINT64_C(0xfedcba9876543210);
    warm.scratch ^= 0x55; warm.eip_hint ^= 0xaa;
    warm.fiber = (void *)(uintptr_t)0x567800;
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    warm.id_flag = 0;
#endif
    warm.preempt = 31;
    expected = warm;
    memcpy(expected.r, cold_exit.r, sizeof(expected.r));
    expected.df = cold_exit.df; expected.f_kind = cold_exit.f_kind;
    expected.f_op1 = cold_exit.f_op1; expected.f_op2 = cold_exit.f_op2;
    expected.f_res = cold_exit.f_res; expected.f_bits = cold_exit.f_bits;
    expected.f_cf_override = cold_exit.f_cf_override; expected.f_cf = cold_exit.f_cf;
    expected.f_of_override = cold_exit.f_of_override; expected.f_of = cold_exit.f_of;
    expected.fsp = cold_exit.fsp; expected.fsw = cold_exit.fsw; expected.fcw = cold_exit.fcw;
    st_put(&expected, 2, st_bits(&cold_exit, 2));
    st_put(&expected, 5, st_bits(&cold_exit, 5));
    st_put(&expected, 6, st_bits(&cold_exit, 6));
    for (i = 0; i < 9; ++i) xmm_put(&expected, i, xmm_bits(&cold_exit, i));
    expected.preempt = 14;
    assert(xv_query_cpu_replay(&state, &warm, &fpscr));
    assert(!memcmp(&warm, &expected, sizeof(warm)) && fpscr == fp_exit);
}

static void mismatch_and_budget(void)
{
    static const size_t fields[] = {
        offsetof(xctx, fs_base), offsetof(xctx, df), offsetof(xctx, f_kind),
        offsetof(xctx, f_op1), offsetof(xctx, f_op2), offsetof(xctx, f_res),
        offsetof(xctx, f_bits), offsetof(xctx, f_cf_override), offsetof(xctx, f_cf),
        offsetof(xctx, f_of_override), offsetof(xctx, f_of), offsetof(xctx, fsp),
        offsetof(xctx, fsw), offsetof(xctx, fcw)
    };
    unsigned i;
    xctx current;
    uint32_t fpscr;
    setup(); finish_program();
    for (i = 0; i < 8; ++i) { current = entry; current.r[i] ^= 1; reject(&current, fp_entry); }
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        current = entry; ((unsigned char *)&current)[fields[i]] ^= 1; reject(&current, fp_entry);
    }
    for (i = 0; i < 32; ++i) reject(&entry, fp_entry ^ (UINT32_C(1) << i));
    current = entry; st_put(&current, 2, st_bits(&current, 2) ^ 1); reject(&current, fp_entry);
    current = entry; xmm_put(&current, 31, xmm_bits(&current, 31) ^ 1); reject(&current, fp_entry);
    current = entry; current.preempt = 17; reject(&current, fp_entry);
    current.preempt = 0; reject(&current, fp_entry);
    current.preempt = -1; reject(&current, fp_entry);
    current = entry; current.preempt = 18; fpscr = fp_entry;
    assert(xv_query_cpu_replay(&state, &current, &fpscr) && current.preempt == 1);
    current = entry; current.preempt = INT32_MAX; fpscr = fp_entry;
    assert(xv_query_cpu_replay(&state, &current, &fpscr) && current.preempt == INT32_MAX - 17);
    setup(); assert(xv_query_cpu_finish(&state, &entry, fp_entry));
    assert(state.consumed == 0); current = entry; current.preempt = 1; fpscr = fp_entry;
    assert(xv_query_cpu_replay(&state, &current, &fpscr) && current.preempt == 1);
}

static void invalid_records(void)
{
    unsigned i;
    xctx current;
    uint32_t fpscr;
    for (i = 0; i < 8; ++i) {
        setup(); current = entry; current.mm[i] ^= 1;
        assert(!xv_query_cpu_finish(&state, &current, fp_entry));
        assert(state.reason & XV_QCPU_UNTRACKED); reject(&entry, fp_entry);
    }
    setup(); current = entry; current.fs_base ^= 1; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; current.scratch ^= 1; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; current.eip_hint ^= 1; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; current.fiber = NULL; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    setup(); current = entry; current.id_flag ^= 1; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
#endif
    setup(); current = entry; st_put(&current, 1, st_bits(&current, 1) ^ 1);
    assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; xmm_put(&current, 9, xmm_bits(&current, 9) ^ 1);
    assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; current.preempt = 101;
    assert(!xv_query_cpu_finish(&state, &current, fp_entry) && (state.reason & XV_QCPU_BUDGET));
    setup(); current = entry; current.preempt = 0; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    setup(); current = entry; current.preempt = -1; assert(!xv_query_cpu_begin(&state, &current, fp_entry));
    setup(); current = entry; current.fsp = 8; assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    assert(!xv_query_cpu_begin(&state, &current, fp_entry));
    for (i = 0; i < 32; ++i) if (XV_QUERY_CPU_TRAPS & (UINT32_C(1) << i)) {
        setup(); assert(!xv_query_cpu_begin(&state, &entry, fp_entry | (UINT32_C(1) << i)));
        assert(state.reason & XV_QCPU_FP);
        setup(); assert(!xv_query_cpu_finish(&state, &entry, fp_entry | (UINT32_C(1) << i)));
    }
    setup(); xv_query_cpu_st_read(&state, 8); assert(state.reason & XV_QCPU_BOUNDS);
    setup(); xv_query_cpu_st_write(&state, UINT32_MAX); assert(state.reason & XV_QCPU_BOUNDS);
    setup(); xv_query_cpu_invalidate(&state, XV_QCPU_CALLBACK);
    current = entry; current.preempt = 95;
    assert(!xv_query_cpu_finish(&state, &current, fp_entry));
    assert(state.reason & XV_QCPU_CALLBACK); reject(&entry, fp_entry);
    setup(); xv_query_cpu_invalidate(&state, 0); assert(state.reason & XV_QCPU_UNKNOWN);
    assert(!xv_query_cpu_finish(&state, &entry, fp_entry));
    setup(); finish_program(); xv_query_cpu_invalidate(&state, XV_QCPU_CALLBACK); reject(&entry, fp_entry);
    setup(); finish_program(); current = entry;
    assert(!xv_query_cpu_replay(&state, &current, &current.r[0]));
    assert(!memcmp(&current, &entry, sizeof(current)));
    assert(!xv_query_cpu_replay(&state, &current, &state.fpscr_entry));
    assert(!xv_query_cpu_replay(&state, &current, NULL));
    xv_query_cpu_st_read(NULL, 99); xv_query_cpu_st_write(NULL, 99);
    xv_query_cpu_xmm_read(NULL, UINT32_MAX); xv_query_cpu_xmm_write(NULL, UINT32_MAX);
    xv_query_cpu_invalidate(NULL, XV_QCPU_UNKNOWN);
    assert(!xv_query_cpu_begin(NULL, &entry, fp_entry));
    assert(!xv_query_cpu_finish(NULL, &entry, fp_entry));
    assert(!xv_query_cpu_validate(NULL, &entry, fp_entry));
    fpscr = fp_entry; assert(!xv_query_cpu_replay(NULL, &current, &fpscr));
    setup(); finish_program(); /* Clean restart after all rejection cases. */
}

int main(void)
{
    fenv_t saved_fp;
    int raised;
    assert(fegetenv(&saved_fp) == 0);
    assert(fesetround(FE_DOWNWARD) == 0);
    assert(feraiseexcept(FE_DIVBYZERO | FE_INEXACT) == 0);
    raised = fetestexcept(FE_ALL_EXCEPT);
    exact_replay(); mismatch_and_budget(); invalid_records();
    assert(fegetround() == FE_DOWNWARD && fetestexcept(FE_ALL_EXCEPT) == raised);
    assert(fesetenv(&saved_fp) == 0);
    printf("PASS query CPU state: masked raw FP replay, scalar/dependency rejects, preserved fields, budget, invalidation, host FP unchanged (%zu-byte state)\n", sizeof(state));
    return 0;
}
