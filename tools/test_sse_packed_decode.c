/* Synthetic data only. Intel SDM Vol.2 CVTPI2PS/RSQRTPS are the oracles. */
#include <assert.h>
#include <stdio.h>
#include <xmmintrin.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static uint8_t arena[65536];
static uint32_t pages[16];
void x_guest_read_pages(void *dst, uint32_t a, size_t n) {
    for (size_t i = 0; i < n; ++i) ((uint8_t *)dst)[i] = g_xram[g_xpt[(a+i) >> 12] + ((a+i) & 4095)];
}
void x_guest_write_pages(uint32_t a, const void *src, size_t n) {
    for (size_t i = 0; i < n; ++i) g_xram[g_xpt[(a+i) >> 12] + ((a+i) & 4095)] = ((const uint8_t *)src)[i];
}
typedef struct { void (*run)(xctx *); unsigned dst, src, convert; } test_case;
static const test_case cases[] = {
#include "cases.h"
};
static const uint32_t patterns[] = {
    0, 0x80000000, 0x7F800000, 0xFF800000, 0x7F800001, 0xFF800001,
    0x7FC12345, 0xFFC54321, 1, 0x007FFFFF, 0x80000001, 0x807FFFFF,
    0x00800000, 0x7F7FFFFF, 0x3F800000, 0x40800000, 0x41000000,
    0xFFFFFFFE, 0x01000001, 0xFEFFFFFF, 0x7FFFFFFF, 32767, 0xFFFF8000
};
static void oracle_convert(uint32_t destination[4], uint64_t source) {
    __m128 v; memcpy(&v, destination, 16);
    __asm__ volatile("cvtpi2ps %1,%0\n\temms" : "+x"(v) : "m"(source) : "memory");
    memcpy(destination, &v, 16);
}
static void oracle_rsqrt(uint32_t destination[4], const uint32_t source[4]) {
    __m128 v; memcpy(&v, source, 16);
    __asm__ volatile("rsqrtps %0,%0" : "+x"(v) :: "memory");
    memcpy(destination, &v, 16);
}
static void check_vectors(void) {
    unsigned count = 0, saved = _mm_getcsr();
    for (unsigned n = 0; n < sizeof(cases)/sizeof(cases[0]); ++n) {
        const test_case *t = &cases[n];
        for (unsigned seed = 0; seed < 23; ++seed) for (unsigned control = 0; control < 16; ++control) {
            xctx c, want; memset(&c, 0xA5, sizeof c);
            c.r[4] = 0x8000; c.r[0] = seed & 1 ? 0x2FFC : 0x2011;
            uint32_t regs[8][4];
            for (unsigned r = 0; r < 8; ++r) for (unsigned l = 0; l < 4; ++l)
                regs[r][l] = patterns[(r * 4 + l + seed) % 23];
            memcpy(c.xmm, regs, sizeof regs);
            for (unsigned r = 0; r < 8; ++r)
                c.mm[r] = ((uint64_t)patterns[(r + seed + 9) % 23] << 32) | patterns[(r + seed) % 23];
            uint32_t input[4]; memcpy(input, regs[t->src % 8], sizeof input);
            uint64_t integers = c.mm[t->src % 8];
            if (t->convert) memcpy(input, &integers, 8);
            if (t->src == 8) x_guest_write(c.r[0], input, t->convert ? 8 : 16);
            uint8_t memory_before[sizeof arena]; memcpy(memory_before, arena, sizeof arena);
            memcpy(&want, &c, sizeof c); want.r[4] += 4;
            uint32_t expected[4]; memcpy(expected, regs[t->dst], sizeof expected);
            /* All masks, all rounding modes, DAZ/FTZ combinations and sticky flags. */
            unsigned csr = 0x1F80 | ((control & 3) << 13) | ((control & 4) << 4)
                           | ((control & 8) << 12) | (seed & 0x3F);
            _mm_setcsr(csr);
            if (t->convert) oracle_convert(expected, integers);
            else oracle_rsqrt(expected, input);
            unsigned native_after = _mm_getcsr();
            if (!t->convert) {
                for (unsigned l = 0; l < 4; ++l) {
                    unsigned e = (input[l] >> 23) & 255;
                    if (!e || e == 255 || (input[l] >> 31))
                        assert(x_rsqrt_bits(input[l]) == expected[l]);
                    expected[l] = x_rsqrt_bits(input[l]);
                }
            }
            memcpy(want.xmm[t->dst], expected, sizeof expected);
            _mm_setcsr(csr); t->run(&c); unsigned after = _mm_getcsr();
            if (after != native_after) {
                fprintf(stderr, "CSR case=%u seed=%u control=%u got=%08X native=%08X\n", n,seed,control,after,native_after);
                assert(0);
            }
            assert(memcmp(&c, &want, sizeof c) == 0);
            assert(memcmp(arena, memory_before, sizeof arena) == 0); ++count;
        }
    }
    _mm_setcsr(saved); printf("%u emitted full-state/memory/native-FP comparisons\n", count);
}
static void check_accuracy(void) {
    double worst = 0; uint32_t worst_bits = 0;
    /* Every mantissa, both exponent parities; exponent scaling is exact. */
    for (uint32_t bits = 0x3F800000; bits < 0x40800000; ++bits) {
        uint32_t result = x_rsqrt_bits(bits); float a, b;
        memcpy(&a, &bits, 4); memcpy(&b, &result, 4);
        double error = fabs((double)b * sqrt((double)a) - 1);
        assert(error <= 1.5 / 4096);
        if (error > worst) { worst = error; worst_bits = bits; }
    }
    /* All finite exponents and bin boundaries, including extreme normals. */
    for (unsigned e = 1; e < 255; ++e) for (unsigned m = 0; m <= 512; ++m) {
        uint32_t bits = (e << 23) | (m == 512 ? 0x7FFFFF : m * 16384);
        uint32_t result = x_rsqrt_bits(bits); float a, b;
        memcpy(&a, &bits, 4); memcpy(&b, &result, 4);
        assert(fabs((double)b * sqrt((double)a) - 1) <= 1.5 / 4096);
    }
    printf("16,777,216 exhaustive mantissas + 130,302 exponent cases; max relative error %.12g at %08X\n", worst, worst_bits);
}
int main(void) {
    g_xram = arena; g_img_base = arena; g_xpt = pages;
    for (unsigned i = 0; i < 16; ++i) pages[i] = (i ^ 1) * 4096;
    memset(arena, 0xDB, sizeof arena); check_vectors(); check_accuracy(); return 0;
}
