/* Synthetic fixture: no guest executable or game data required. */
#include <stddef.h>
#include "xv_x86rt.h"

const unsigned compare_layout[] = {
    sizeof(xctx), offsetof(xctx, f_kind), offsetof(xctx, f_bits),
    offsetof(xctx, fsp), offsetof(xctx, fsw)
};

/* Preserve the preceding portable expression as the compiled ARM reference. */
static inline void reference(xctx *c, double a, double b, int eflags)
{
    uint16_t cc;
    if (isnan(a) || isnan(b)) cc = 0x4500;
    else if (a < b) cc = 0x0100;
    else if (a == b) cc = 0x4000;
    else cc = 0;
    c->fsw = (uint16_t)((c->fsw & ~0x4700u) | cc | ((c->fsp & 7u) << 11));
    if (eflags) {
        uint32_t f = ((cc & 0x4000) ? 0x40u : 0) |
                     ((cc & 0x0400) ? 0x04u : 0) |
                     ((cc & 0x0100) ? 0x01u : 0);
        xf_set_eflags(c, (xf_eflags(c) & ~0x45u) | f);
    }
}

void original_compare(xctx *c, const uint64_t *bits, int eflags)
{
    double a, b;
    memcpy(&a, bits, 8);
    memcpy(&b, bits + 1, 8);
    reference(c, a, b, eflags);
}

void candidate_compare(xctx *c, const uint64_t *bits, int eflags)
{
    double a, b;
    memcpy(&a, bits, 8);
    memcpy(&b, bits + 1, 8);
    x87_compare(c, a, b, eflags);
}
