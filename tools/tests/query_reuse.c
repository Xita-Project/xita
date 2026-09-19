#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define XV_QUERY_REUSE_TEST 1
#include "../../recomp/kernel/xk_query_reuse.c"

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
int xv_watch_n, xv_trace_funcs;
unsigned xv_collision_vertices_state = 1, xv_segment_sphere_mode = 1;
const unsigned xv_collision_traversal_mode = 1;
static unsigned admitted = 1, checks, logs, originals, captured, work = 40;
static unsigned arena_bytes = 0x41000, mode, fake_fp = 0x63000090;
static unsigned expect_original_state;
static uint32_t expected_output;
static uint8_t *arena_a, *arena_b;
static uint32_t *pages_a, *pages_b;
static xctx entry;
enum { NESTED=1, LOST, ROOT_CHANGE, CONFIG_CHANGE, OVERFLOW, UNTRACKED, REFILL, FP_TRAP };
unsigned xv_object_world_run_admit(xctx *c) { (void)c; return admitted; }
void xv_object_math_report_check(void) { ++checks; }
uint32_t xk_mem_arena_size(void) { return arena_bytes; }
uint32_t xv_query_reuse_test_fpscr_get(void) { return fake_fp; }
void xv_query_reuse_test_fpscr_set(uint32_t value) { fake_fp = value; }
void xk_os_log(const char *format, ...) { assert((strstr(format, "[query-reuse]") || strstr(format, "[query-reuse-detail]"))); ++logs; }
static uint32_t get32(unsigned off) { uint32_t v; memcpy(&v, g_xram + off, 4); return v; }
static void put32(unsigned off, uint32_t v) { memcpy(g_xram + off, &v, 4); }
static void execute(XvQueryCpu *cpu, XvQueryMemory *mem, xctx *c)
{
    if (expect_original_state) {
        assert(!cpu && !mem); assert(get32(0x2100) == expected_output);
        assert(c->r[0] == entry.r[0]); expect_original_state = 0;
    }
    if (mem) {
        assert(xv_query_memory_mapping(mem, 2, g_xpt[2]));
        assert(xv_query_memory_read(mem, 0x2000, 4));
        assert(xv_query_memory_write(mem, 0x2100, 4));
    }
    uint32_t v = get32(0x2000);
    put32(0x2100, v ^ 0xa5a5a5a5u);
    c->r[0] = v; c->f_res = v + 1; c->fsw = 0x7800; c->fsp = 5;
    if (cpu) { xv_query_cpu_st_write(cpu, 3); xv_query_cpu_xmm_write(cpu, 1u); }
    uint64_t st = UINT64_C(0x7ff0000000000001); memcpy(&c->st[3], &st, 8);
    memcpy(&c->xmm[0][0], &v, 4);
    c->preempt -= (int32_t)work;
    if (mode == NESTED) {
        if (cpu) xv_query_cpu_invalidate(cpu, XV_QCPU_CALLBACK);
        if (mem) xv_query_memory_invalidate(mem, XV_QM_CALLBACK);
        unsigned saved = mode; mode = 0;
        xctx nested = entry; nested.r[7] ^= 123; nested.preempt = 1000;
        xv_query_reuse_run(&nested); assert(nested.r[0] == v);
        mode = saved;
    } else if (mode == LOST) admitted = 0;
    else if (mode == ROOT_CHANGE) { memcpy(arena_b, g_xram, arena_bytes); g_xram = arena_b; }
    else if (mode == CONFIG_CHANGE) xv_segment_sphere_mode ^= 1;
    else if (mode == OVERFLOW && mem) {
        for (unsigned i = 0; i <= XV_QUERY_MEMORY_BLOCKS; ++i)
            xv_query_memory_read(mem, 0x4000 + i * 64, 1);
    } else if (mode == UNTRACKED) memcpy(&c->st[4], &st, 8);
    else if (mode == REFILL) c->preempt += 2000;
    fake_fp = mode == FP_TRAP ? 0x63000190 : 0xa3000095;
}
void query_fused_172c95_171f94(xctx *c) { ++originals; execute(NULL, NULL, c); }
void query_captured_172c95_171f94(XvQueryCpu *cpu, XvQueryMemory *mem, xctx *c)
{ ++captured; execute(cpu, mem, c); }
static void reset(void)
{
    reset_entries(); memset(&roots, 0, sizeof roots); memset(&counts, 0, sizeof counts);
    busy = reset_pending = attempted = last_attempt = 0; epoch = 1;
    admitted = 1; mode = originals = captured = logs = checks = expect_original_state = 0;
    work = 40; fake_fp = 0x63000090; xv_watch_n = xv_trace_funcs = 0;
    xv_collision_vertices_state = xv_segment_sphere_mode = 1;
    g_xram = arena_a; g_img_base = arena_a; g_xpt = pages_a; arena_bytes = 0x41000;
    memset(arena_a, 0, arena_bytes); memset(arena_b, 0, arena_bytes);
    for (unsigned i = 0; i < (1u << 20); ++i)
        pages_a[i] = pages_b[i] = i < 0x40 ? i * 4096 : 0x40000;
    memset(&entry, 0, sizeof entry); entry.r[0] = 0x3000; entry.r[1] = 256;
    entry.r[2] = 0x3100; entry.r[4] = 0x1000; entry.preempt = 1000;
    entry.fcw = 0x23f; entry.fsw = 0x7800; entry.fsp = 0;
    uint32_t args[] = {0x171f99, 0x1800, 0x3f800000}; memcpy(g_xram + 0x1000, args, 12);
    uint32_t center[] = {0x3f800000, 0xc0000000, 0x3e800000}; memcpy(g_xram + 0x1800, center, 12);
    put32(0x2000, 0x12345678);
}
static xctx call(void)
{
    xctx c = entry; fake_fp = 0x63000090; xv_query_reuse_run(&c); return c;
}
static void promote(void)
{
    for (unsigned i = 0; i < 8; ++i) {
        if (i) xv_query_reuse_epoch();
        call();
    }
    assert(captured == 1 && counts.finished == 1 && entries[0].finished);
}
static void next_epochs(unsigned n) { while (n--) xv_query_reuse_epoch(); }
static void promotion_replay(void)
{
    reset();
    for (unsigned i = 0; i < 40; ++i) call();
    assert(!captured && entries[0].observations == 1);
    for (unsigned i = 0; i < 7; ++i) { xv_query_reuse_epoch(); call(); }
    assert(captured == 1 && counts.finished == 1);
    xctx warm = entry;
    memset(warm.st, 0xa5, sizeof warm.st); memset(warm.xmm, 0xb6, sizeof warm.xmm);
    memset(warm.mm, 0xc7, sizeof warm.mm); warm.scratch = 123; warm.eip_hint = 456;
    warm.fiber = (void *)(uintptr_t)0x4321; warm.preempt = 80;
    xctx expected = warm; fake_fp = 0x63000090;
    execute(NULL, NULL, &expected);
    put32(0x2100, 0xdeadbeef); g_xram[0x2104] = 77;
    unsigned old_originals = originals;
    fake_fp = 0x63000090; xv_query_reuse_run(&warm);
    assert(!memcmp(&warm, &expected, sizeof warm)); assert(fake_fp == 0xa3000095);
    assert(get32(0x2100) == (get32(0x2000) ^ 0xa5a5a5a5u) && g_xram[0x2104] == 77);
    assert(originals == old_originals && counts.hits == 1 && counts.saved_consumed == 40);
    warm = entry; warm.preempt = 40; fake_fp = 0x63000090; xv_query_reuse_run(&warm);
    assert(counts.short_budget == 1 && warm.preempt == 0 && entries[0].finished);
    call(); assert(counts.hits == 2);
}
static void rejection_cooldown(void)
{
    reset(); promote();
    put32(0x2000, get32(0x2000) ^ 1); put32(0x2100, 0x99887766);
    expected_output = get32(0x2100); expect_original_state = 1;
    call(); assert(!expect_original_state && counts.memory_reject == 1 && entries[0].cooling);
    assert(!entries[0].finished && entries[0].observations == 0);
    for (unsigned i = 0; i < 63; ++i) { xv_query_reuse_epoch(); call(); }
    assert(captured == 1);
    xv_query_reuse_epoch(); call(); assert(captured == 2 && entries[0].finished);
    xctx c = entry; c.df = 1; fake_fp = 0x63000090;
    put32(0x2100, 0xdeadfeed); expected_output = get32(0x2100); expect_original_state = 1;
    xv_query_reuse_run(&c); assert(!expect_original_state && counts.cpu_reject == 1);
    reset(); promote(); pages_a[2] = 0x5000; call(); assert(counts.memory_reject == 1);
}
static void admission_roots_keys(void)
{
    reset(); admitted = 0; call(); assert(originals == 1 && !counts.lookup && !entries[0].used);
    admitted = 1; xv_watch_n = 1; call(); xv_watch_n = 0;
    xv_trace_funcs = 1; call(); xv_trace_funcs = 0;
    entry.fsp = 8; call(); entry.fsp = 0;
    xctx c = entry; fake_fp = 0x63000190; xv_query_reuse_run(&c);
    assert(counts.declines == 5 && !counts.lookup);
    reset(); promote(); g_img_base = arena_a + 16; call(); assert(!entries[0].finished && counts.root_resets == 2);
    reset(); promote(); g_xpt = pages_b; call(); assert(counts.root_resets == 2 && !entries[0].finished);
    reset(); promote(); memcpy(arena_b, arena_a, arena_bytes); g_xram = arena_b; call(); assert(counts.root_resets == 2);
    reset(); promote(); arena_bytes -= 4096; call(); assert(counts.root_resets == 2 && !entries[0].finished);
    reset(); promote(); xv_collision_vertices_state ^= 1; call(); assert(counts.hits == 0 && entries[1].used);
    reset(); promote(); put32(0x1800, 3); call(); assert(!counts.hits && entries[1].used);
    reset(); promote(); put32(0x1000, 0x173020); call(); assert(!counts.hits && entries[1].used);
    reset(); entry.r[4] = UINT32_MAX - 4; call(); assert(counts.cheap == 1 && !counts.lookup);
    reset(); put32(0x1004, UINT32_MAX - 3); call(); assert(counts.cheap == 1);
    reset(); pages_a[1] = 0x40000; call(); assert(counts.cheap == 1);
    reset(); pages_a[1] = 0x40800; call(); assert(counts.cheap == 1);
    reset(); entry.r[4] = 0x1ffa;
    uint32_t args[] = {0x171f99, 0x1800, 0x3f800000};
    memcpy(g_xram + 0x1ffa, args, 6); pages_a[2] = 0x3000;
    memcpy(g_xram + 0x3000, (uint8_t *)args + 6, 6);
    call(); assert(counts.lookup == 1 && entries[0].key.stack[2] == 0x3f800000);
}
static void capture_failures(void)
{
    for (unsigned m = NESTED; m <= FP_TRAP; ++m) {
        reset();
        for (unsigned i = 0; i < 7; ++i) { call(); xv_query_reuse_epoch(); }
        mode = m; xctx c = call();
        assert(captured == 1 && !entries[0].finished && !busy);
        assert(c.r[0] == 0x12345678 && get32(0x2100) == (0x12345678 ^ 0xa5a5a5a5u));
        if (m == LOST) {
            assert(reset_pending); admitted = 1; mode = 0; call();
            assert(!reset_pending && entries[0].observations == 1);
        } else assert(counts.abandoned == 1);
        if (m == NESTED) assert(counts.busy == 1 && originals == 8);
        if (m == ROOT_CHANGE) assert(counts.root_resets == 2);
        if (m == FP_TRAP) assert(fake_fp == 0x63000190);
    }
    reset(); mode = NESTED; call(); assert(counts.busy == 1 && entries[0].observations == 1);
    reset(); mode = LOST; call(); assert(reset_pending && !busy); admitted = 1;
    xv_query_reuse_epoch(); assert(!reset_pending && !entries[0].used);
}
static void throttle_eviction_epoch(void)
{
    reset(); work = 31;
    for (unsigned i = 0; i < 40; ++i) { call(); xv_query_reuse_epoch(); }
    assert(!captured && !counts.promotable);
    reset();
    for (unsigned i = 0; i < 8; ++i) {
        call(); entry.r[7] = 1; call(); entry.r[7] = 0;
        if (i != 7) xv_query_reuse_epoch();
    }
    assert(captured == 1 && counts.promotable == 2);
    entry.r[7] = 1; next_epochs(31); call(); assert(captured == 1);
    xv_query_reuse_epoch(); call(); assert(captured == 2);
    reset(); for (unsigned i = 0; i < REUSE_ENTRIES + 1; ++i) { entry.r[7] = i; call(); }
    assert(counts.evictions == 1 && entries[0].key.r[7] == REUSE_ENTRIES);
    reset(); promote(); epoch = UINT_MAX; xv_query_reuse_epoch();
    assert(epoch == 1 && !entries[0].used && !attempted); call(); assert(!counts.hits);
    xv_query_reuse_report(0); assert(!logs);
    xv_query_reuse_report(60); assert(logs == 2 && !counts.calls && entries[0].used);
    assert(checks > 0);
}
static ReuseEntry *history_for(unsigned key)
{
    for (unsigned i = 0; i < REUSE_ENTRIES; ++i)
        if (entries[i].used && entries[i].key.r[7] == key) return &entries[i];
    return NULL;
}
static void check_record_owners(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < REUSE_ENTRIES; ++i) {
        ReuseEntry *e = &entries[i];
        if (e->used && e->finished) {
            assert(e->record < REUSE_RECORDS);
            assert(records[e->record].owner == i + 1);
            ++n;
        }
    }
    unsigned owners = 0;
    for (unsigned i = 0; i < REUSE_RECORDS; ++i) if (records[i].owner) {
        ReuseEntry *e = &entries[records[i].owner - 1];
        assert(e->used && e->finished && e->record == i);
        ++owners;
    }
    assert(n == owners);
}
static void retained_records(void)
{
    reset(); promote();
    for (unsigned i = 1; i <= REUSE_ENTRIES * 3; ++i) {
        entry.r[7] = i; call();
    }
    assert(history_for(0) && history_for(0)->finished);
    entry.r[7] = 0; call(); assert(counts.hits == 1 && !counts.record_evictions);
    check_record_owners();

    reset(); promote();
    for (unsigned key = 1; key < REUSE_RECORDS; ++key) {
        next_epochs(REUSE_CAPTURE_INTERVAL); entry.r[7] = key;
        for (unsigned j = 0; j < REUSE_OBSERVATIONS; ++j) { call(); xv_query_reuse_epoch(); }
        assert(history_for(key)->finished); check_record_owners();
    }
    /* Refresh record zero; record one is now the least recently used. */
    entry.r[7] = 0; call(); assert(counts.hits == 1);
    next_epochs(REUSE_CAPTURE_INTERVAL); entry.r[7] = REUSE_RECORDS;
    for (unsigned j = 0; j < REUSE_OBSERVATIONS; ++j) { call(); xv_query_reuse_epoch(); }
    assert(counts.record_evictions == 1 && history_for(0)->finished);
    assert(!history_for(1)->finished && history_for(1)->cooling);
    assert(history_for(REUSE_RECORDS)->finished); check_record_owners();
    entry.r[7] = 1; unsigned before = originals; call(); assert(originals == before + 1);
    entry.r[7] = 0; call(); assert(counts.hits == 2);
    /* Invalidating a retained transaction releases its slot immediately. */
    put32(0x2000, get32(0x2000) ^ 1); call();
    assert(counts.memory_reject == 1 && !history_for(0)->finished);
    check_record_owners();
    reset(); mode = OVERFLOW;
    for (unsigned j = 0; j < REUSE_OBSERVATIONS; ++j) { call(); xv_query_reuse_epoch(); }
    assert(counts.abandoned == 1 && (counts.memory_reasons & XV_QM_OVERFLOW));
    assert(counts.max_blocks == XV_QUERY_MEMORY_BLOCKS && counts.max_mappings == 1);
    assert(counts.total_blocks == XV_QUERY_MEMORY_BLOCKS && counts.total_mappings == 1);
    check_record_owners();
}
int main(void)
{
    arena_a = malloc(0x41000); arena_b = malloc(0x41000);
    pages_a = malloc((1u << 20) * 4); pages_b = malloc((1u << 20) * 4);
    assert(arena_a && arena_b && pages_a && pages_b);
    promotion_replay(); rejection_cooldown(); admission_roots_keys(); capture_failures(); throttle_eviction_epoch(); retained_records();
    free(arena_a); free(arena_b); free(pages_a); free(pages_b);
    printf("PASS query reuse: promotion/replay, atomic reject, cooldown/throttle, root/config/alias guards, callback reentry, abandonment, epoch wrap, joined report (%zu-byte entries)\n", sizeof entries + sizeof records);
    return 0;
}
