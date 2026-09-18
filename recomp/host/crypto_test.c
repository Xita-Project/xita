/* Standard SHA-1 vectors through the real guest ABI, including split pages. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"
uint8_t *g_xram;
uint32_t *g_xpt;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
uint32_t xk_kalloc(uint32_t n) { static uint32_t p = 0x5000; uint32_t a = p; p += n; return a; }
extern void xk_XcSHAInit(xctx *), xk_XcSHAUpdate(xctx *), xk_XcSHAFinal(xctx *);
extern void xk_crypto_init(uint32_t, uint32_t);
extern uint32_t xk_crypto_export(unsigned);
static void call(void (*fn)(xctx *), unsigned argc, uint32_t a, uint32_t b, uint32_t d)
{
    xctx c = {0}; c.r[4] = 0x100;
    X_M32(0x104) = a; X_M32(0x108) = b; X_M32(0x10c) = d;
    fn(&c); assert(c.r[4] == 0x104 + argc * 4);
}
static void check_hex(uint32_t addr, unsigned n, const char *expected)
{
    char out[41]; assert(n <= 20);
    for (unsigned i = 0; i < n; ++i) snprintf(out + i*2, 3, "%02x", X_M8(addr + i));
    assert(!strcmp(out, expected));
}
static void hash(const unsigned char *p, unsigned n, unsigned step, const char *expected)
{
    const uint32_t ctx = 0x1fd1, input = 0x3fd3, digest = 0x6ff7;
    X_M8(ctx - 1) = 0xa5; X_M8(ctx + 116) = 0x5a;
    X_M8(digest - 1) = 0xa5; X_M8(digest + 20) = 0x5a;
    call(xk_XcSHAInit, 1, ctx, 0, 0);
    for (unsigned i = 0; i < n;) {
        unsigned take = n - i; if (take > step) take = step;
        x_guest_write(input, p + i, take);
        call(xk_XcSHAUpdate, 3, ctx, input, take); i += take;
    }
    call(xk_XcSHAUpdate, 3, ctx, 0xffffffff, 0);
    call(xk_XcSHAFinal, 2, ctx, digest, 0);
    check_hex(digest, 20, expected);
    assert(X_M8(ctx - 1) == 0xa5 && X_M8(ctx + 116) == 0x5a);
    assert(X_M8(digest - 1) == 0xa5 && X_M8(digest + 20) == 0x5a);
}
int main(void)
{
    g_xram = calloc(16, 4096); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 8; ++i) g_xpt[i] = (i * 7 % 16) * 4096;
    hash((const unsigned char *)"", 0, 1, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    hash((const unsigned char *)"abc", 3, 1, "a9993e364706816aba3e25717850c26c9cd0d89d");
    unsigned char *p = malloc(1000000); assert(p); memset(p, 'a', 1000000);
    hash(p, 1000000, 4097, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    for (unsigned i = 0; i < 4097; ++i) p[i] = (unsigned char)(i * 17 + i / 256);
    const unsigned sizes[] = {55,56,63,64,65,4097};
    const char *expected[] = {"269c80fcf9763ee568282c62781665e2e8df7a21", "fcd3c5221e2151db1d281d84ff51eb1cff44cf79",
        "0eaab11ad2a8ba9c258723d6c067708fcae1974f", "6b4aa4dab4769f44bb1cc802080be01b6dd23652",
        "38a920a8678c1f529de35c6ff3885816e58be40f", "d0f31b774de618e4380a30c806b7179e607540b7"};
    for (unsigned i = 0; i < 6; ++i) {
        hash(p, sizes[i], 1, expected[i]); hash(p, sizes[i], 4097, expected[i]);
    }
    X_M32(0x1118) = 0x1200;
    for (unsigned i = 0; i < 16; ++i) X_M8(0x12c0 + i) = i;
    xk_crypto_init(0x1000, 0x1000);
    check_hex(xk_crypto_export(325), 16, "c9d2566d9f87751e7260d19ec62b8026");
#ifdef XV_NONZERO_HD_KEY
    check_hex(xk_crypto_export(323), 16, "786974612d7669727475616c2d68646b");
#else
    check_hex(xk_crypto_export(323), 16, "00000000000000000000000000000000");
#endif
    assert(!xk_crypto_export(999));
    free(p); free(g_xpt); free(g_xram);
    puts("PASS: SHA-1 vectors, incremental/padding boundaries, fragmented context/input/output, signing-key derivation and ABI");
}
