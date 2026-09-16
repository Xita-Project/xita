/* Production helper/HLE differential fixture; oracle generated from local XBE.
 * Deliberately include xd3d.c: do not replace its state/HLE implementations. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "kernel/xd3d.c"

uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
xk_thread *xk_cur;
int xv_trace_enabled, xv_trace_funcs, xv_watch_n;
#ifndef TEST_GUEST_TRACE
#define TEST_GUEST_TRACE 0
#endif
const unsigned xv_guest_trace_enabled = TEST_GUEST_TRACE;
volatile uint32_t xv_cur_fn;
void xk_os_log(const char *format, ...) { (void)format; }
/* ASan retains the unrelated production sound vtable. Any accidental path
 * into those services must fail, rather than providing a successful mock. */
uint64_t xk_os_monotonic_us(void) { abort(); }
int xk_audio_available(void) { abort(); }
uint64_t xk_audio_last_mix_us(void) { abort(); }
int xk_audio_stream_pop_consumed(int voice) { (void)voice; abort(); }
int xk_audio_stream_push(int voice, uint32_t guest, uint32_t size)
{ (void)voice; (void)guest; (void)size; abort(); }
void xk_audio_stream_flush(int voice) { (void)voice; abort(); }
xk_obj *xk_handle_get_type(uint32_t handle, xk_objtype type)
{ (void)handle; (void)type; abort(); }
void xk_signal_check(void) { abort(); }
void xv_call(xctx *c, uint32_t target) { (void)c; (void)target; abort(); }
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
const char xv_object_job_marker = 0;
#ifndef TEST_MISSING_WORKER_PREDICATE
static _Thread_local int actual_worker;
int xv_object_is_worker_thread(void) { return actual_worker; }
#endif
#endif
#define XV_HLE_CALL(address, fn) do { uint32_t save = xv_cur_fn; \
    xv_cur_fn = 0x80000000u | (uint32_t)(address); fn(c); xv_cur_fn = save; } while (0)
#include "material_packet_reference.inc"

static void (*const originals[5])(xctx *) = {material_reference_0, material_reference_1,
    material_reference_2, material_reference_3, material_reference_4};
static void (*const hooks[5])(xctx *) = {material_hooked_0, material_hooked_1,
    material_hooked_2, material_hooked_3, material_hooked_4};

enum { ARENA = 2 << 20 };
static uint8_t *before, *expected;
static uint32_t seed = 0x70110;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static xctx setup(unsigned k)
{
    memset(g_xram, k & 255, ARENA);
    for (unsigned p = 0; p < ARENA / 4096; ++p) g_xpt[p] = p * 4096;
    uint32_t sp = 0x504A0 + (k & 3);
    if (k & 4) g_xpt[0x50] = 0x18F000; /* stack aliases image mirrors */
    if (k & 8) sp = 0x501A8 + (k & 3); /* stack aliases texture state */
    if (k & 16) g_xpt[0x60] = g_xpt[0x50];
    if (k % 17 == 0) sp = 0x51004 + (k & 3); /* scratch crosses mapped pages */
    xctx c = {0};
    for (unsigned i = 0; i < 8; ++i) {
        c.r[i] = next(); c.st[i] = i + .5; c.mm[i] = next();
        for (unsigned j = 0; j < 4; ++j) c.xmm[i][j] = (float)(i + j) + .25f;
    }
    c.r[3] = (c.r[3] & ~255u) | (k & 255);
    c.r[4] = sp; c.r[5] = k & 32 ? sp - 0x2C : 0x60000 + (k & 0x3ff);
    c.f_kind = XK_SUB; c.f_bits = 32; c.f_op1 = next(); c.f_op2 = next(); c.f_res = c.f_op1 - c.f_op2;
    c.f_cf_override = k & 1; c.f_cf = (k >> 1) & 1; c.f_of_override = (k >> 2) & 1; c.f_of = (k >> 3) & 1;
    c.preempt = next(); c.eip_hint = next(); c.fs_base = next(); c.fsw = next(); c.fcw = next(); c.fsp = k & 7;
    memset(&xd3d_state, (k + 11) & 255, sizeof xd3d_state);
    return c;
}

static void check_decline(xctx *c, unsigned island)
{
    xctx copy = *c;
    xd3d_state_t state = xd3d_state;
    memcpy(before, g_xram, ARENA);
    assert(!xv_material_packet(c, island));
    assert(!memcmp(c, &copy, sizeof copy));
    assert(!memcmp(&xd3d_state, &state, sizeof state));
    assert(!memcmp(g_xram, before, ARENA));
}

#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
#ifndef TEST_MISSING_WORKER_PREDICATE
static void *worker_check(void *unused)
{
    (void)unused;
    actual_worker = 1;
    xctx c = setup(1); /* unmarked context on an actual native worker */
    check_decline(&c, 0);
    actual_worker = 0;
    return NULL;
}
#endif
#endif

int main(int argc, char **argv)
{
    (void)argv;
    g_xram = malloc(ARENA); g_img_base = g_xram; g_xpt = calloc(1 << 20, 4);
    before = malloc(ARENA); expected = malloc(ARENA);
    assert(g_xram && g_xpt && before && expected);
    xctx c = setup(0);
    if (argc > 1) {
        assert(!xv_material_packet_available());
        xv_material_packet_override(1);
        for (unsigned i = 0; i < 5; ++i) check_decline(&c, i);
        xv_material_packet_counters counts;
        xv_material_packet_read_counters(&counts, 0);
        xv_material_packet_counters zero = {{0}, {0}};
        assert(!memcmp(&counts, &zero, sizeof counts));
#ifndef XV_MATERIAL_PACKET
        for (unsigned i = 0; i < 5; ++i) {
            xctx a = setup(i + 3), b = a;
            xd3d_state_t initial = xd3d_state;
            memcpy(before, g_xram, ARENA); originals[i](&a);
            xd3d_state_t wanted = xd3d_state;
            memcpy(expected, g_xram, ARENA); memcpy(g_xram, before, ARENA); xd3d_state = initial;
            hooks[i](&b);
            assert(!memcmp(&a, &b, sizeof a) && !memcmp(&xd3d_state, &wanted, sizeof wanted));
            assert(!memcmp(g_xram, expected, ARENA));
        }
#endif
        puts("PASS: compiled-out / unsupported worker dependency / diagnostics decline without architectural or counter writes");
        goto done;
    }
#ifdef XV_MATERIAL_PACKET
    /* Configured off/on/unset, explicit override, and configured restoration. */
    assert(xv_material_packet_available());
    const char *e = getenv("XV_MATERIAL_PACKET");
    int configured = e && atoi(e) != 0;
    assert(xv_material_packet(&c, 1) == configured);
    xv_material_packet_override(0); check_decline(&c, 1);
    xv_material_packet_override(1); assert(xv_material_packet(&c, 1));
    xv_material_packet_override(-1); assert(xv_material_packet(&c, 1) == configured);
    xv_material_packet_override(1);
    check_decline(&c, 5); check_decline(&c, UINT32_MAX); assert(!xv_material_packet(NULL, 0));
    xv_material_packet_read_counters(NULL, 1);
    unsigned cases = 0;
    for (unsigned k = 0; k < 512; ++k) for (unsigned island = 0; island < 5; ++island) {
        xctx a = setup(k), b = a;
        xd3d_state_t initial = xd3d_state;
        memcpy(before, g_xram, ARENA); originals[island](&a);
        xd3d_state_t wanted = xd3d_state;
        memcpy(expected, g_xram, ARENA); memcpy(g_xram, before, ARENA); xd3d_state = initial;
        xv_cur_fn = 0x70110;
        hooks[island](&b);
        assert(xv_cur_fn == 0x70110);
        assert(!memcmp(&a, &b, sizeof a));
        assert(!memcmp(&xd3d_state, &wanted, sizeof wanted));
        assert(!memcmp(g_xram, expected, ARENA));
        cases++;
    }
    xv_material_packet_counters counts;
    xv_material_packet_read_counters(&counts, 1);
    for (unsigned i = 0; i < 5; ++i) assert(counts.eligible[i] == 512 && counts.accepted[i] == 512);
    /* Fallback integration with policy disabled uses the actual hooked span. */
    xv_material_packet_override(0);
    for (unsigned i = 0; i < 5; ++i) {
        xctx a = setup(i + 3), b = a;
        xd3d_state_t initial = xd3d_state;
        memcpy(before, g_xram, ARENA); originals[i](&a);
        xd3d_state_t wanted = xd3d_state;
        memcpy(expected, g_xram, ARENA); memcpy(g_xram, before, ARENA); xd3d_state = initial;
        hooks[i](&b);
        assert(!memcmp(&a, &b, sizeof a) && !memcmp(&xd3d_state, &wanted, sizeof wanted));
        assert(!memcmp(g_xram, expected, ARENA));
    }
    xv_material_packet_override(1);
    xv_material_packet_read_counters(NULL, 1);
    c = setup(2);
    xv_trace_enabled = 1; check_decline(&c, 0); xv_trace_enabled = 0;
    xv_trace_funcs = 1; check_decline(&c, 0); xv_trace_funcs = 0;
    xv_watch_n = 1; check_decline(&c, 0); xv_watch_n = 0;
    g_hist_frame = g_dev.frame + 1; check_decline(&c, 0); g_hist_frame = -1;
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    c.fiber = (void *)&xv_object_job_marker;
    check_decline(&c, 0); c.fiber = NULL;
#ifndef TEST_MISSING_WORKER_PREDICATE
    pthread_t thread; assert(!pthread_create(&thread, NULL, worker_check, NULL));
    assert(!pthread_join(thread, NULL));
#endif
#endif
    xv_material_packet_read_counters(&counts, 0);
    xv_material_packet_counters zero = {{0}, {0}};
    assert(!memcmp(&counts, &zero, sizeof counts));
    material_packet_counts.eligible[1] = UINT32_MAX;
    material_packet_counts.accepted[1] = UINT32_MAX;
    c = setup(3); assert(xv_material_packet(&c, 1));
    xv_material_packet_read_counters(&counts, 1);
    assert(counts.eligible[1] == UINT32_MAX && counts.accepted[1] == UINT32_MAX);
    assert(xv_material_packet(&c, 1));
    xd3d_prepare_report(60);
    xv_material_packet_read_counters(&counts, 0);
    assert(!memcmp(&counts, &zero, sizeof counts));
    printf("PASS: %u production helper/hook comparisons, mapped aliases, full context/memory/state; off/on/restore, unchanged fallback, diagnostics, workers and saturated counters\n", cases);
#else
    abort(); /* The compiled-out fixture must take the decline arm above. */
#endif
done:
    free(expected); free(before); free(g_xpt); free(g_xram);
    return 0;
}
