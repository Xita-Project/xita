/* Host regressions for guest-page translation and the kernel pool. No game data needed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"

uint8_t *g_xram;
extern void xk_NtAllocateVirtualMemory(xctx *c);
void xk_os_log(const char *fmt, ...) { (void)fmt; }

static uint32_t allocate(uint32_t base, uint32_t size, uint32_t type)
{
    uint32_t args = xk_kalloc(64);
    xctx c = {0}; c.r[4] = args;
    X_M32(args + 4) = args + 32;
    X_M32(args + 12) = args + 36;
    X_M32(args + 16) = type;
    X_M32(args + 20) = 4;
    X_M32(args + 32) = base; X_M32(args + 36) = size;
    xk_NtAllocateVirtualMemory(&c);
    assert(c.r[0] == STATUS_SUCCESS);
    uint32_t result = X_M32(args + 32);
    xk_kfree(args);
    return result;
}

static void string_copies(uint32_t va)
{
    const int deltas[] = {-3, 0, 1, 4, 80};
    uint32_t base = va + 4048;
    for (unsigned sz = 1; sz <= 4; sz *= 2) for (unsigned df = 0; df < 2; ++df) {
        for (unsigned d = 0; d < sizeof deltas / sizeof deltas[0]; ++d) {
            unsigned char expected[192];
            for (unsigned i = 0; i < sizeof expected; ++i) X_M8(base + i) = expected[i] = (unsigned char)i;
            int src = 32 + (df ? 18 * sz : 0), dst = src + deltas[d];
            xctx c = {0}; c.df = df; c.r[1] = 19; c.r[6] = base + src; c.r[7] = base + dst;
            for (unsigned i = 0; i < 19; ++i) {
                unsigned char element[4];
                memcpy(element, expected + src, sz); memcpy(expected + dst, element, sz);
                src += df ? -(int)sz : (int)sz; dst += df ? -(int)sz : (int)sz;
            }
            x_str_movs(&c, sz, X_STR_REP);
            assert(c.r[1] == 0 && c.r[6] == base + src && c.r[7] == base + dst);
            for (unsigned i = 0; i < sizeof expected; ++i) assert(X_M8(base + i) == expected[i]);
        }
        uint32_t a = va + 4093;
        xctx c = {0}; c.df = df; c.r[0] = 0x12345678; c.r[1] = 19;
        c.r[7] = a + (df ? 18 * sz : 0);
        X_M8(a - 1) = 0x42; X_M8(a + 19 * sz) = 0x24;
        x_str_stos(&c, sz, X_STR_REP);
        assert(c.r[1] == 0 && c.r[7] == (df ? a - sz : a + 19 * sz));
        for (unsigned i = 0; i < 19 * sz; ++i) assert(X_M8(a + i) == (unsigned char)(0x12345678u >> (8 * (i % sz))));
        assert(X_M8(a - 1) == 0x42 && X_M8(a + 19 * sz) == 0x24);
    }
}

static void page_copies(void)
{
    uint32_t va = allocate(0, 8192, 0x2000);
    assert(allocate(va, 4096, 0x1000) == va);
    assert(allocate(va + 4096, 4096, 0x1000) == va + 4096);
    assert(g_xpt[(va >> 12) + 1] != g_xpt[va >> 12] + 4096);
    for (unsigned offset = 4079; offset <= 4097; ++offset) {
        uint32_t a = va + offset;
        float expected[4], actual[4];
        unsigned char *bytes = (void *)expected;
        for (unsigned i = 0; i < 16; ++i) bytes[i] = (unsigned char)(i + 1);
        for (unsigned i = 0; i < 16; ++i) X_M8(a + i) = bytes[i];
        x_load128(NULL, actual, a);
        assert(!memcmp(actual, expected, 16));
        memset(expected, 0xA5, sizeof expected);
        X_M8(a - 1) = 0x42; X_M8(a + 16) = 0x24;
        x_store128(NULL, a, expected);
        for (unsigned i = 0; i < 16; ++i) assert(X_M8(a + i) == 0xA5);
        assert(X_M8(a - 1) == 0x42 && X_M8(a + 16) == 0x24);
        x87_store_f32(NULL, a, -12.5); assert(x87_load_f32(NULL, a) == -12.5);
        x87_store_f64(NULL, a, -12.5); assert(x87_load_f64(NULL, a) == -12.5);
        x87_store_f80(NULL, a, -12.5); assert(x87_load_f80(NULL, a) == -12.5);
        x87_store_i16(NULL, a, -123); assert(x87_load_i16(NULL, a) == -123);
        x87_store_i32(NULL, a, -123456); assert(x87_load_i32(NULL, a) == -123456);
        x87_store_i64(NULL, a, -12345678901.0); assert(x87_load_i64(NULL, a) == -12345678901.0);
    }
    string_copies(va);
    assert(xk_mem_free(va) == 0);
}

static void kernel_pool(void)
{
    for (unsigned i = 0; i < 100000; ++i) {
        uint32_t p = xk_kalloc(64); assert(p);
        assert(X_M32(p) == 0); X_M32(p) = 0xDEADBEEF;
        xk_kfree(p);
    }
    uint32_t a = xk_kalloc(64), b = xk_kalloc(128), c = xk_kalloc(64);
    assert(a && b && c); X_M32(c) = 0x12345678;
    xk_kfree(b + 64); /* aligned interior address must not free a live block */
    xk_kfree(a); xk_kfree(b); xk_kfree(b);
    uint32_t merged = xk_kalloc(192); assert(merged == a);
    assert(X_M32(c) == 0x12345678);
    xk_kfree(merged); xk_kfree(c);
    uint32_t *blocks = calloc(49152, sizeof *blocks); assert(blocks);
    for (unsigned i = 0; i < 49152; ++i) { blocks[i] = xk_kalloc(64); assert(blocks[i]); }
    assert(xk_kalloc(64) == 0);
    for (unsigned i = 0; i < 49152; i += 2) xk_kfree(blocks[i]);
    assert(xk_kalloc(128) == 0); /* fragmented free space cannot overwrite live blocks */
    for (unsigned i = 1; i < 49152; i += 2) xk_kfree(blocks[i]);
    uint32_t whole = xk_kalloc(3u << 20); assert(whole);
    assert(xk_kalloc(1) == 0); xk_kfree(whole);
    assert(xk_kalloc(UINT32_MAX) == 0);
    uint32_t zero = xk_kalloc(0); assert(zero); xk_kfree(zero);
    free(blocks);
}

static void fixed_kernel_pool(void)
{
    const uint32_t base = 0x03FE0000, size = 0x10000;
    assert(xk_kreserve_fixed(base, size) == 0);
    X_M32(base) = 0xCAFE1234;
    xk_kfree(base); /* fixed ownership cannot be released by ordinary free */
    assert(xk_kreserve_fixed(base, size) == -1);
    assert(xk_kreserve_fixed(base + 1, size) == -1);
    assert(xk_kreserve_fixed(base, 1) == -1);
    assert(xk_kreserve_fixed(0x3D00000, UINT32_MAX) == -1);
    assert(xk_kreserve_fixed(0x4000000, size) == -1);
    assert(xk_krelease_fixed(base - 64, size + 64) == -1); /* atomic ownership check */
    assert(xk_kalloc(3u << 20) == 0);
    uint32_t lower = xk_kalloc(0x2E0000), upper = xk_kalloc(0x10000);
    assert(lower == 0x3D00000 && upper == 0x3FF0000 && xk_kalloc(1) == 0);
    assert(X_M32(base) == 0xCAFE1234);
    assert(xk_kreserve_fixed(lower, 64) == -1); /* cannot steal live allocations */
    xk_kfree(lower); xk_kfree(upper);
    assert(xk_krelease_fixed(base, 0xB000) == 0); /* GPU claim shrinks lower end */
    assert(xk_krelease_fixed(base, size) == -1); /* mixed ownership: no partial release */
    lower = xk_kalloc(0x2EB000); assert(lower == 0x3D00000);
    upper = xk_kalloc(0x10000); assert(upper == 0x3FF0000);
    assert(xk_kalloc(1) == 0);
    xk_kfree(lower); xk_kfree(upper);
    assert(xk_krelease_fixed(base + 0xB000, 0x5000) == 0);
    lower = xk_kalloc(3u << 20); assert(lower == 0x3D00000); xk_kfree(lower);
}

int main(void)
{
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena();
    page_copies(); kernel_pool(); fixed_kernel_pool();
    free(g_xram); free(g_xpt);
    puts("PASS: SSE/x87/string page crossings and overlap; kernel pool reuse, merging, fragmentation and exhaustion");
    return 0;
}
