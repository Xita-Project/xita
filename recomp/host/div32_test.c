/* Integer-magnitude oracle: no signed division or overflowing signed negation. */
#include "xv_x86rt.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>

static unsigned traps, cases, accepted[2], rejected[2];
static uint32_t trapped_eip;
static xctx *trapped_context;
void xv_trap(xctx *c, uint32_t eip)
{
    traps++;
    trapped_eip = eip;
    trapped_context = c;
}

static uint32_t seed = 0xA7156B93u;
static uint32_t random_word(void)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

static int reference(uint32_t lo, uint32_t hi, uint32_t d, int sign,
                     uint32_t *quotient, uint32_t *remainder)
{
    uint64_t bits = ((uint64_t)hi << 32) | lo;
    if (!d) return 0;
    unsigned negative_n = sign && (hi >> 31);
    unsigned negative_d = sign && (d >> 31);
    uint64_t magnitude_n = negative_n ? 0u - bits : bits;
    uint32_t magnitude_d = negative_d ? 0u - d : d;
    uint64_t q = magnitude_n / magnitude_d;
    uint32_t r = (uint32_t)(magnitude_n % magnitude_d);
    unsigned negative_q = negative_n != negative_d;
    uint64_t limit = !sign ? UINT32_MAX :
                     negative_q ? UINT64_C(0x80000000) : INT32_MAX;
    if (q > limit) return 0;
    *quotient = negative_q ? 0u - (uint32_t)q : (uint32_t)q;
    *remainder = negative_n ? 0u - r : r;
    return 1;
}

static void check(uint32_t lo, uint32_t hi, uint32_t d, int sign)
{
    xctx actual, expected;
    unsigned char *bytes = (unsigned char *)&actual;
    for (unsigned i = 0; i < sizeof actual; i++) bytes[i] = (unsigned char)random_word();
    actual.fiber = NULL;
    actual.r[0] = lo;
    actual.r[2] = hi;
    memcpy(&expected, &actual, sizeof actual);
    uint32_t q = 0, r = 0, eip = random_word();
    int success = reference(lo, hi, d, sign, &q, &r);
    if (success) { expected.r[0] = q; expected.r[2] = r; accepted[sign]++; }
    else rejected[sign]++;
    traps = 0;
    trapped_context = NULL;
    int fp_before = fetestexcept(FE_ALL_EXCEPT);
    if (sign) x_idiv_32(&actual, d, eip);
    else x_div_32(&actual, d, eip);
    assert(fetestexcept(FE_ALL_EXCEPT) == fp_before);
    assert(traps == (unsigned)!success);
    if (!success) assert(trapped_context == &actual && trapped_eip == eip);
    assert(!memcmp(&actual, &expected, sizeof actual));
    cases++;
}

int main(void)
{
    const uint32_t edges[] = {0, 1, 2, 3, 7, 31, 0xFFFF, 0x10000,
        0x3FFFFFFF, 0x40000000, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000,
        0x80000001, 0xFFFFFFFE, 0xFFFFFFFF};
    feraiseexcept(FE_INVALID | FE_INEXACT);
    for (unsigned sign = 0; sign < 2; sign++) {
        for (unsigned l = 0; l < sizeof edges / sizeof *edges; l++)
            for (unsigned h = 0; h < sizeof edges / sizeof *edges; h++)
                for (unsigned d = 0; d < sizeof edges / sizeof *edges; d++)
                    check(edges[l], edges[h], edges[d], sign);
        for (unsigned i = 0; i < 50000; i++) {
            uint32_t lo = random_word(), d = random_word();
            check(lo, random_word(), d, sign);
            check(lo, sign && (lo >> 31) ? UINT32_MAX : 0, d, sign);
            check(lo, d, d, sign);
            check(lo, d - 1, d, sign);
        }
    }
    /* Wide INT64_MIN / -1 must trap before C division; INT32_MIN / -1 also traps. */
    check(0, 0x80000000, UINT32_MAX, 1);
    check(0x80000000, UINT32_MAX, UINT32_MAX, 1);
    assert(accepted[0] && accepted[1] && rejected[0] && rejected[1]);
    printf("PASS: %u div32 context/trap/FP checks; unsigned %u accepted/%u trapped; signed %u accepted/%u trapped\n",
           cases, accepted[0], rejected[0], accepted[1], rejected[1]);
    return 0;
}
