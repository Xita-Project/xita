#include "nv2a_regs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_value(const h2_nv2a *device, uint32_t offset)
{
    uint32_t value;
    assert(h2_nv2a_read32(device, offset, &value) == H2_NV2A_OK);
    return value;
}

int main(void)
{
    h2_nv2a device, before;
    h2_nv2a_reset(&device, 0x4000000);
    assert(read_value(&device, 0x1800) == 0x02A010DE);
    assert(read_value(&device, 0x1808) == 0x030000A1);
    assert(read_value(&device, 0x10020C) == 0x4000000);
    assert(read_value(&device, 0x1804) == 2);
    assert(h2_nv2a_write32(&device, 0x1804, read_value(&device, 0x1804) | 4) == H2_NV2A_OK);
    assert(read_value(&device, 0x1804) == 6);
    assert(h2_nv2a_write32(&device, 0x1804, 2) == H2_NV2A_OK);
    assert(read_value(&device, 0x1804) == 2);
    assert(h2_nv2a_write32(&device, 0x180C, 0xF800) == H2_NV2A_OK);
    assert(read_value(&device, 0x180C) == 0xF800);
    assert(h2_nv2a_write32(&device, 0x1830, 0) == H2_NV2A_OK);
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t address = i ? 0x9140 : 0x600140;
        assert(h2_nv2a_write32(&device, address, 0) == H2_NV2A_OK);
        assert(read_value(&device, address) == 0);
        assert(h2_nv2a_write32(&device, address, 1) == H2_NV2A_UNSUPPORTED_OPERATION);
        assert(read_value(&device, address) == 0);
    }
    const uint32_t writes[][2] = {
        {0x1804, 0xFFFF0006}, {0x180C, 0xFFFF0000}, {0x1830, 1},
        {0x1800, 0}, {0x1808, 0}, {0x10020C, 0},
        {0x100, 0}, {0x1805, 4}, {0x1000000, 0}, {0xFFFFFFFF, 0}
    };
    memcpy(&before, &device, sizeof before);
    for (unsigned i = 0; i < sizeof writes / sizeof *writes; ++i) {
        assert(h2_nv2a_write32(&device, writes[i][0], writes[i][1]) != H2_NV2A_OK);
        assert(memcmp(&before, &device, sizeof before) == 0);
    }
    const uint32_t reads[] = {0, 0x1801, 0x1FFF, 0x1000000, 0xFFFFFFFF};
    for (unsigned i = 0; i < sizeof reads / sizeof *reads; ++i) {
        uint32_t value = 0xDEADBEEF;
        assert(h2_nv2a_read32(&device, reads[i], &value) != H2_NV2A_OK);
        assert(value == 0xDEADBEEF);
        assert(memcmp(&before, &device, sizeof before) == 0);
    }
    assert(h2_nv2a_read32(&device, 0x1804, NULL) == H2_NV2A_INVALID_ACCESS);
    /* Clearing PCI memory enable removes the BAR, including its PCI mirror.
     * Re-enabling needs a host PCI-config path; it cannot use disabled MMIO. */
    assert(h2_nv2a_write32(&device, 0x1804, 0) == H2_NV2A_OK);
    uint32_t unavailable = 0xCAFEBABE;
    assert(h2_nv2a_read32(&device, 0x1800, &unavailable) == H2_NV2A_UNSUPPORTED_OPERATION);
    assert(unavailable == 0xCAFEBABE);
    assert(h2_nv2a_write32(&device, 0x1804, 2) == H2_NV2A_UNSUPPORTED_OPERATION);
    h2_nv2a_reset(&device, 0x4000000);
    assert(read_value(&device, 0x180C) == 0 && read_value(&device, 0x1804) == 2);
    /* Master disable removes modeled unit MMIO and resets its state, while
     * PMC/PCI/PLL access remains available for bringing the units back. */
    device.timer_interrupt_enable = 0xA5; device.crtc_interrupt_enable = 0x5A;
    assert(h2_nv2a_write32(&device, 0x200, 0) == H2_NV2A_OK);
    assert(device.timer_interrupt_enable == 0 && device.crtc_interrupt_enable == 0);
    assert(read_value(&device, 0x200) == 0 && read_value(&device, 0x140) == 0);
    const uint32_t gated[] = {0x9140, 0x600140, 0x10020C};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t value = 0xFEEDABCD;
        assert(h2_nv2a_read32(&device, gated[i], &value) == H2_NV2A_UNSUPPORTED_OPERATION);
        assert(value == 0xFEEDABCD);
        assert(h2_nv2a_write32(&device, gated[i], 0) == H2_NV2A_UNSUPPORTED_OPERATION);
    }
    assert(h2_nv2a_write32(&device, 0x200, 0xFFFFFFFF) == H2_NV2A_OK);
    assert(read_value(&device, 0x200) == 0xFFFFFFFF);
    assert(read_value(&device, 0x9140) == 0 && read_value(&device, 0x600140) == 0);
    assert(read_value(&device, 0x10020C) == 0x4000000);
    assert(h2_nv2a_write32(&device, 0x140, 1) == H2_NV2A_UNSUPPORTED_OPERATION);
    assert(read_value(&device, 0x140) == 0);
    /* Enabling an unimplemented engine never turns unknown registers into 0. */
    unavailable = 0xCAFEBABE;
    assert(h2_nv2a_read32(&device, 0x400700, &unavailable) == H2_NV2A_UNKNOWN_REGISTER);
    assert(unavailable == 0xCAFEBABE);
    assert(h2_nv2a_pll_hz(read_value(&device, 0x680500)) == 233333324);
    assert(h2_nv2a_pll_hz(read_value(&device, 0x680504)) == 199999992);
    assert(h2_nv2a_pll_hz(read_value(&device, 0x680508)) == 31089742);
    assert(h2_nv2a_pll_hz(0xFFFF00) == 0); /* zero M is not a divide fault */
    assert(h2_nv2a_pll_hz(0x070101) == 130208); /* P=7 */
    assert(h2_nv2a_write32(&device, 0x680500, 0x00010E01) == H2_NV2A_UNSUPPORTED_OPERATION);
    assert(read_value(&device, 0x680500) == 0x00011C01);
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t value = 0xFFFFFFFF;
        assert(h2_nv2a_read(&device, 0x680508 + i, 1, &value) == H2_NV2A_OK);
        const uint32_t expected[] = {0x0D, 0xC2, 3, 0};
        assert(value == expected[i]);
    }
    uint32_t value = 0xAAAAAAAA;
    assert(h2_nv2a_read(&device, 0x680508, 2, &value) == H2_NV2A_OK && value == 0xC20D);
    assert(h2_nv2a_read(&device, 0x68050A, 2, &value) == H2_NV2A_OK && value == 3);
    value = 0xAAAAAAAA;
    assert(h2_nv2a_read(&device, 0x68050B, 2, &value) == H2_NV2A_INVALID_ACCESS);
    assert(h2_nv2a_read(&device, 0x680508, 0, &value) == H2_NV2A_INVALID_ACCESS);
    assert(h2_nv2a_read(&device, 0x680508, 8, &value) == H2_NV2A_INVALID_ACCESS);
    assert(value == 0xAAAAAAAA);
    puts("NV2A bootstrap: identity, PCI state, interrupt-disable state and rejected-access isolation passed");
    return 0;
}
