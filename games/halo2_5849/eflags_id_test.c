/* Synthetic PUSHFD/POPFD feature probing and independent lazy-flag state. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "xv_x86rt.h"
uint8_t *g_xram;
uint32_t *g_xpt;
static unsigned observed_id(unsigned input)
{
#if defined(XV_EFLAGS_ID) && XV_EFLAGS_ID
    return input & 0x200000;
#else
    (void)input; return 0;
#endif
}
static unsigned bit_flags(unsigned bits)
{
    const unsigned positions[] = {0, 2, 6, 7, 10, 11};
    unsigned value = 0x202;
    for (unsigned i = 0; i < 6; ++i) if (bits & (1u << i)) value |= 1u << positions[i];
    return value;
}
int main(void)
{
    g_xram = calloc(1, 4096); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    unsigned cases = 0;
    for (unsigned flags = 0; flags < 64; ++flags) for (unsigned id = 0; id < 2; ++id) {
        xctx context; memset(&context, 0xa5, sizeof context); xctx *c = &context;
        c->r[4] = 0x800; uint32_t value = bit_flags(flags) | (id << 21);
        xf_set_eflags(c, value); assert(xf_eflags(c) == (bit_flags(flags) | observed_id(value)));
        xctx before = *c;
        /* Feature detection toggles ID through the real emitted stack helpers,
         * tests the round trip, then restores the original flags. */
        X_PUSH32(xf_eflags(c)); uint32_t original = X_POP32();
        X_PUSH32(original ^ 0x200000); xf_set_eflags(c, X_POP32());
        X_PUSH32(xf_eflags(c)); uint32_t toggled = X_POP32();
        assert((original ^ toggled) == observed_id(0x200000));
        X_PUSH32(original); xf_set_eflags(c, X_POP32());
        before.f_res = original; /* reserved/unrepresented bits are not claimed */
        assert(!memcmp(c, &before, sizeof *c));
        /* Arithmetic flag replacement, shifts and x87 comparisons must not
         * overwrite the independently stored architectural ID bit. */
        X_FLAGS(XK_ADD, 0xffffffffu, 1, 0, 32);
        assert((xf_eflags(c) & 0x200000) == observed_id(value));
        assert(XF_C(c) && XF_Z(c));
        (void)x_shl32(c, 0x80000000, 1);
        assert((xf_eflags(c) & 0x200000) == observed_id(value) && XF_C(c));
        x87_compare(c, 1.0, 2.0, 1);
        assert((xf_eflags(c) & 0x200000) == observed_id(value) && XF_C(c) && !XF_Z(c));
        xctx copy = *c; memset(c, 0, sizeof *c); *c = copy;
        assert((xf_eflags(c) & 0x200000) == observed_id(value));
        ++cases;
    }
    /* A POPFD cannot expose other unsupported high bits via the new field. */
    xctx context = {0}; xf_set_eflags(&context, 0xffffffff);
    assert(xf_eflags(&context) == (0xEC7u | observed_id(0x200000)));
    free(g_xpt); free(g_xram);
    printf("EFLAGS.ID: %u feature-probe/flags/context cases pass; enabled=%u.\n", cases, observed_id(0x200000) != 0);
    return 0;
}
