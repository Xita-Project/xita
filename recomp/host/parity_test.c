/* Guest PF depends only on the low byte, or explicit EFLAGS bit 2.
 * No game data or Vita hardware is required. */
#include "../xv_x86rt.h"
#include <assert.h>
#include <stdio.h>

static unsigned oracle(uint32_t value)
{
    unsigned ones = 0;
    for (unsigned bit = 0; bit < 8; bit++) ones += (value >> bit) & 1u;
    return ones % 2 == 0;
}

int main(void)
{
    const unsigned widths[] = {8, 16, 32};
    unsigned cases = 0;
    for (unsigned kind = XK_LOGIC; kind <= XK_EXPLICIT; kind++)
    for (unsigned width = 0; width < 3; width++)
    for (unsigned low = 0; low < 256; low++)
    for (unsigned high = 0; high < 28; high++) {
        uint32_t upper = high < 24 ? 1u << (high + 8) :
                         high == 24 ? 0 : high == 25 ? 0xffffff00u :
                         high == 26 ? 0xaaaaaa00u : 0x55555500u;
        xctx c = {0};
        c.f_kind = kind; c.f_res = upper | low; c.f_bits = widths[width];
        c.f_op1 = 0x87654321u; c.f_op2 = 0x12345678u;
        c.f_cf_override = c.f_of_override = 1; c.f_cf = 1; c.f_of = 0;
        xctx before = c;
        unsigned expected = kind == XK_EXPLICIT ? (low >> 2) & 1u : oracle(low);
        assert(XF_P(&c) == expected);
        assert(((xf_eflags(&c) >> 2) & 1u) == expected);
        assert(!memcmp(&before, &c, sizeof c));
        cases++;
    }
    /* MSVC's x87 compare / fnstsw / test ah,mask / jp idiom, including
     * unordered operands, must retain both PF and all other context state. */
    const double operands[] = {0, -0.0, 1, -1, INFINITY, -INFINITY, NAN};
    const unsigned masks[] = {0x05, 0x41, 0x44};
    for (unsigned top = 0; top < 8; top++)
    for (unsigned a = 0; a < 7; a++)
    for (unsigned b = 0; b < 7; b++) {
        xctx state = {0}; state.fsp = top; state.fsw = 0xabcd; state.f_bits = 32;
        x87_compare(&state, operands[a], operands[b], 0);
        unsigned ah = state.fsw >> 8;
        for (unsigned mask = 0; mask < 3; mask++) {
            xctx copy = state, *c = &copy;
            X_FLAGS(XK_LOGIC, 0, 0, ah & masks[mask], 8);
            xctx before = copy;
            assert(XF_P(c) == oracle(ah & masks[mask]));
            assert(!memcmp(&before, c, sizeof copy));
            cases++;
        }
        x87_compare(&state, operands[a], operands[b], 1);
        assert(XF_P(&state) == (unsigned)(isnan(operands[a]) || isnan(operands[b])));
        cases++;
    }
    printf("PASS: %u parity cases, all low bytes/kinds/widths, upper-bit isolation, x87 comparisons and unchanged context\n", cases);
    return 0;
}
