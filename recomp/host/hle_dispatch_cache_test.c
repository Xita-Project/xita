/* Exercise the actual dispatcher, including nested callbacks and collisions.
 * Include its translation unit to check that real cache hits were exercised. */
#define _POSIX_C_SOURCE 200809L
#define XV_HLE_DISPATCH_CACHE 1
#include <assert.h>
#include <stdarg.h>
#include "../xv_x86rt.c"

uint8_t *g_xram;
uint32_t *g_xpt;
#ifdef TEST_UNTRACED
const unsigned xv_guest_trace_enabled = 0;
#else
const unsigned xv_guest_trace_enabled = 1;
#endif
volatile uint32_t xv_cur_fn;
static unsigned callbacks, hles, magic_calls;
static unsigned reports;

/* The Vita's saved-log hook must receive the complete interval before reset. */
void xv_logf(const char *format, ...)
{
    assert(strstr(format, "[hle-dispatch]"));
    va_list ap; va_start(ap, format);
    assert(va_arg(ap, unsigned) == 60);
    assert(va_arg(ap, unsigned) == hle_dispatch_guest_calls);
    assert(va_arg(ap, unsigned) == hle_dispatch_hle_calls);
    assert(va_arg(ap, unsigned) == hle_dispatch_lookups);
    assert(va_arg(ap, unsigned) == hle_dispatch_hits);
    assert(va_arg(ap, unsigned) == hle_dispatch_stores);
    va_end(ap); reports++;
}

static void marker(uint32_t expected)
{ if (xv_guest_trace_enabled) assert(xv_cur_fn == expected); }
static void guest_b(xctx *c)
{
    if (xv_guest_trace_enabled) xv_cur_fn = 0x22000;
    c->r[0] += 17; X_M32(0x100) ^= c->r[0]; callbacks++;
}
static void hle_main(xctx *c)
{
    marker(0x80033000); c->r[0] ^= 0x12345678;
    xv_call(c, 0x22000); marker(0x80033000);
    c->r[4] += 4; hles++;
}
static void hle_collision(xctx *c)
{ marker(0x80033400); c->r[1] += 5; X_M32(0x104) += 9; hles++; }
static void hle_unaligned(xctx *c)
{ marker(0x80033001); c->r[2] ^= 0x87654321; hles++; }
static void hle_extra(xctx *c)
{
    marker(0x80033004); xv_call(c, 0x33000); marker(0x80033004);
    c->r[3] += 31; hles++;
}
static void hle_magic_fallback(xctx *c)
{ marker(0xfe000002); c->r[5] += 3; hles++; }
static void wrong_priority(xctx *c) { (void)c; abort(); }
static void guest_a(xctx *c)
{
    if (xv_guest_trace_enabled) xv_cur_fn = 0x11000;
    xv_call(c, 0x33004); marker(0x11000);
    xv_call(c, 0x22000); marker(0x11000);
}
const xv_fn_entry_t xv_fn_table[] = {{0x11000,guest_a},{0x22000,guest_b}};
const unsigned xv_fn_table_count = 2;
const xv_fn_entry_t xv_hle_table[] = {
    {0x22000,wrong_priority}, {0x33000,hle_main}, {0x33001,hle_unaligned},
    {0x33004,NULL}, {0x33400,hle_collision}, {0xfe000002,hle_magic_fallback}
};
const unsigned xv_hle_table_count = sizeof xv_hle_table / sizeof *xv_hle_table;
const xv_fn_entry_t xv_hle_extra[] = {
    {0x33000,wrong_priority}, {0x33004,hle_extra}, {0,0}
};
int xk_dispatch_magic(xctx *c, uint32_t target)
{
    marker(target); magic_calls++;
    if (target == 0xfe000002) return 0;
    assert(target == 0xfe000001);
    xv_call(c, 0x33000); marker(target); return 1;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    unsetenv("XV_HLE_DISPATCH_CACHE");
    int configured = !strcmp(argv[1], "on");
    if (strcmp(argv[1], "unset")) setenv("XV_HLE_DISPATCH_CACHE", configured ? "1" : "0", 1);
    g_xram = calloc(1,4096); g_xpt = calloc(1u<<20,4); assert(g_xram && g_xpt);
    const uint32_t targets[] = {0x11000,0x22000,0x33000,0x33400,0x33001,0x33004,0xfe000001,0xfe000002};
    uint8_t initial[4096], expected[4096];
    for (unsigned n = 0; n < 4096; n++) {
        xctx input, baseline, candidate;
        memset(&input, n & 255, sizeof input);
        input.r[4] = 0x800; input.fsp = n & 7;
        memset(initial, (n * 37) & 255, sizeof initial);
        unsigned initial_cb = callbacks, initial_hc = hles, initial_mc = magic_calls;
        memcpy(g_xram, initial, sizeof initial); baseline = input;
        xv_hle_dispatch_override(0); xv_cur_fn = 0x1234;
        xv_call(&baseline, targets[n % 8]); marker(0x1234);
        memcpy(expected, g_xram, sizeof expected);
        unsigned cb = callbacks, hc = hles, mc = magic_calls;
        memcpy(g_xram, initial, sizeof initial); candidate = input;
        xv_hle_dispatch_override(1); xv_cur_fn = 0x1234;
        xv_call(&candidate, targets[n % 8]); marker(0x1234);
        assert(!memcmp(&baseline, &candidate, sizeof baseline));
        assert(!memcmp(expected, g_xram, sizeof expected));
        /* Every cached invocation must still execute the actual callee. */
        assert(callbacks - cb == cb - initial_cb);
        assert(hles - hc == hc - initial_hc && magic_calls - mc == mc - initial_mc);
    }
    assert(hle_dispatch_hits > 1000 && hle_dispatch_stores > 1000);
    /* Restoration obeys the configured mode, including previously warm keys. */
    xv_hle_dispatch_override(-1);
    unsigned before = hle_dispatch_lookups;
    xctx c = {0}; c.r[4] = 0x800; xv_cur_fn = 0x1234;
    xv_call(&c, 0x33000); marker(0x1234);
    assert((hle_dispatch_lookups != before) == configured);
    /* Unknown targets are never cached; lenient behavior is unchanged. */
    setenv("XV_LENIENT", "1", 1); xv_hle_dispatch_override(1);
    unsigned stores = hle_dispatch_stores;
    for (unsigned i = 0; i < 2; i++) {
        c.r[0] = 0; unsigned sp = c.r[4]; xv_call(&c, 0);
        assert(c.r[0] == 0 && c.r[4] == sp + 4);
    }
    assert(hle_dispatch_stores == stores);
    unsigned hits = hle_dispatch_hits;
    assert(hle_dispatch_guest_calls && hle_dispatch_hle_calls);
    xv_hle_dispatch_report(60);
    assert(reports == 1);
    assert(!hle_dispatch_lookups && !hle_dispatch_hits && !hle_dispatch_stores);
    assert(!hle_dispatch_guest_calls && !hle_dispatch_hle_calls);
    printf("PASS HLE cache %s trace=%u: 4096 full-context/arena pairs; %u hits; callbacks, priority, magic, collisions, restoration\n",
           argv[1], xv_guest_trace_enabled, hits);
    free(g_xram); free(g_xpt);
}
