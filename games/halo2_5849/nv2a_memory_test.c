#include "nv2a_regs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    h2_nv2a d, before;
    h2_nv2a_reset(&d, 0x4000000); d.instance_bytes = 0x5000;
    uint32_t mapped = 0xDEADBEEF;
    const uint32_t positions[][2] = {{0x10000,0x83FEFFC0},{0x1003C,0x83FEFFFC},
        {0x10040,0x83FEFF80},{0x1007C,0x83FEFFBC},{0x14FC0,0x83FEB000},{0x14FFC,0x83FEB03C}};
    for (unsigned i = 0; i < sizeof positions / sizeof *positions; ++i) {
        assert(h2_nv2a_pramin_address(&d, positions[i][0], 4, &mapped) == H2_NV2A_OK);
        assert(mapped == positions[i][1]);
    }
    unsigned char seen[0x5000 / 4] = {0};
    for (unsigned i = 0; i < 0x5000; i += 4) {
        assert(h2_nv2a_pramin_address(&d, 0x10000 + i, 4, &mapped) == H2_NV2A_OK);
        assert(mapped >= 0x83FEB000 && mapped < 0x83FF0000 && !(mapped & 3));
        assert(!seen[(mapped - 0x83FEB000) / 4]++); /* exactly one mapping per claimed word */
    }
    assert(h2_nv2a_pramin_address(&d, 0x14FFF, 1, &mapped) == H2_NV2A_OK && mapped == 0x83FEB03F);
    assert(h2_nv2a_pramin_address(&d, 0x14FFE, 2, &mapped) == H2_NV2A_OK && mapped == 0x83FEB03E);
    const uint32_t invalid[][2] = {{0xFFFF,1},{0x15000,1},{0x100000,4},{0x10001,2},
        {0x10002,4},{0x10000,0},{0x10000,8}};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        mapped = 0xDEADBEEF;
        assert(h2_nv2a_pramin_address(&d, invalid[i][0], invalid[i][1], &mapped) != H2_NV2A_OK);
        assert(mapped == 0xDEADBEEF);
    }
    assert(h2_nv2a_write32(&d, 0x200, 0xFFFFFFFF) == H2_NV2A_OK);
    assert(h2_nv2a_write32(&d, 0x2210, 0x03000100) == H2_NV2A_OK); /* 4K at10000, search128 */
    assert(h2_nv2a_write32(&d, 0x2214, 0x00890110) == H2_NV2A_OK);
    assert(h2_nv2a_read32(&d, 0x2210, &mapped) == H2_NV2A_OK && mapped == 0x03000100);
    assert(h2_nv2a_read32(&d, 0x2214, &mapped) == H2_NV2A_OK && mapped == 0x00890110);
    before = d;
    const uint32_t bad_tables[][2] = {{0x2210,0x030000F0},{0x2210,0x03030100},
        {0x2210,0x07000100},{0x2214,0x008900FC},{0x2214,0x00F90110},
        {0x2214,0x01890110},{0x100214,1}};
    for (unsigned i = 0; i < sizeof bad_tables / sizeof *bad_tables; ++i) {
        assert(h2_nv2a_write32(&d, bad_tables[i][0], bad_tables[i][1]) == H2_NV2A_UNSUPPORTED_OPERATION);
        assert(memcmp(&d, &before, sizeof d) == 0);
    }
    assert(h2_nv2a_read32(&d, 0x100214, &mapped) == H2_NV2A_OK && mapped == 0);
    assert(h2_nv2a_write32(&d, 0x100214, 0) == H2_NV2A_OK);
    /* Disabling FIFO clears its configuration, without discarding instance RAM. */
    assert(h2_nv2a_write32(&d, 0x200, H2_ENGINE_FB) == H2_NV2A_OK);
    assert(d.fifo_ramht == 0 && d.fifo_ramfc == 0);
    assert(h2_nv2a_read32(&d, 0x2210, &mapped) == H2_NV2A_UNSUPPORTED_OPERATION);
    assert(h2_nv2a_pramin_address(&d, 0x10000, 4, &mapped) == H2_NV2A_OK);
    assert(h2_nv2a_write32(&d, 0x200, 0) == H2_NV2A_OK);
    mapped = 0xDEADBEEF;
    assert(h2_nv2a_pramin_address(&d, 0x10000, 4, &mapped) == H2_NV2A_UNSUPPORTED_OPERATION);
    assert(mapped == 0xDEADBEEF);
    assert(h2_nv2a_write32(&d, 0x200, H2_ENGINE_FB) == H2_NV2A_OK);
    assert(h2_nv2a_write32(&d, 0x1804, 0) == H2_NV2A_OK);
    assert(h2_nv2a_pramin_address(&d, 0x10000, 4, &mapped) == H2_NV2A_UNSUPPORTED_OPERATION);
    puts("NV2A instance aperture: group reversal, bijection, byte lanes, table bounds, disable/reset and rejection passed");
    return 0;
}
