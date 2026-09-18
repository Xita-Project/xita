#include "nv2a_regs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_reg(const h2_nv2a *d, uint32_t address)
{
    uint32_t value = 0xFFFFFFFF;
    assert(h2_nv2a_read32(d, address, &value) == H2_NV2A_OK);
    return value;
}
static void write_reg(h2_nv2a *d, uint32_t address, uint32_t value)
{ assert(h2_nv2a_write32(d, address, value) == H2_NV2A_OK); }
static void advance(h2_nv2a *d, uint64_t us)
{ assert(h2_nv2a_advance_us(d, us) == H2_NV2A_OK); }

int main(void)
{
    h2_nv2a d, split, before;
    h2_nv2a_reset(&d, 0x4000000);
    assert(read_reg(&d, 0x9200) == 1 && read_reg(&d, 0x9210) == 1);
    assert(read_reg(&d, 0x9400) == 0 && read_reg(&d, 0x9410) == 0);
    write_reg(&d, 0x9200, 0xDE86);
    write_reg(&d, 0x9210, 0x1DCD);
    write_reg(&d, 0x9420, 0xFFFFFFFF);
    assert(read_reg(&d, 0x9420) == 0xFFFFFFE0);
    split = d;
    advance(&d, 1000000);
    /* Exact rate is 233333324 * 7629 / 56966 ticks per second. */
    assert(d.timer_ticks == 31248462);
    for (unsigned i = 0; i < 1000000; ++i) advance(&split, 1);
    assert(memcmp(&d, &split, sizeof d) == 0); /* no accumulated truncation drift */
    uint64_t ticks = d.timer_ticks;
    write_reg(&d, 0x9210, 0); advance(&d, 100000000);
    assert(d.timer_ticks == ticks); /* zero multiplier stops counter */
    write_reg(&d, 0x9210, 0x1DCD); advance(&d, 1000000);
    assert(d.timer_ticks == ticks + 31248462);
    before = d;
    const uint32_t invalid[][2] = {{0x9200,0},{0x9200,0x10000},{0x9200,1},
        {0x9210,0xFFFF},{0x9210,0x10000},{0x9400,0},{0x9410,0},{0x9140,1}};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        assert(h2_nv2a_write32(&d, invalid[i][0], invalid[i][1]) == H2_NV2A_UNSUPPORTED_OPERATION);
        assert(memcmp(&before, &d, sizeof d) == 0);
    }
    assert(h2_nv2a_advance_us(&d, UINT64_MAX) == H2_NV2A_INVALID_ACCESS);
    assert(memcmp(&before, &d, sizeof d) == 0);

    /* Masked alarms still set status. Acknowledgment clears only written bits. */
    h2_nv2a_reset(&d, 0x4000000);
    write_reg(&d, 0x9420, 250u << 5);
    advance(&d, 1); assert(read_reg(&d, 0x9100) == 0); /* 233 ticks */
    advance(&d, 1); assert(read_reg(&d, 0x9100) == 1); /* crossed tick 250 */
    assert(read_reg(&d, 0x9140) == 0); /* no IRQ delivery was enabled */
    write_reg(&d, 0x9100, 0); assert(read_reg(&d, 0x9100) == 1);
    write_reg(&d, 0x9100, 1); assert(read_reg(&d, 0x9100) == 0);
    advance(&d, 1); assert(read_reg(&d, 0x9100) == 0); /* not a repeated match */
    /* Low-counter wrap, including an alarm across the wrap. */
    d.timer_ticks = (1ull << 27) - 100; d.timer_source_fraction = 0;
    write_reg(&d, 0x9420, 32u << 5); advance(&d, 1);
    assert(read_reg(&d, 0x9100) == 1);
    assert(read_reg(&d, 0x9400) == 133u << 5 && read_reg(&d, 0x9410) == 1);
    /* Full 56-bit wrap still advances the comparator correctly. */
    d.timer_ticks = (1ull << 56) - 100; d.timer_source_fraction = 0;
    write_reg(&d, 0x9100, 1); advance(&d, 1);
    assert(d.timer_ticks == 133 && read_reg(&d, 0x9410) == 0);
    assert(read_reg(&d, 0x9100) == 1);
    /* Large intervals can cross multiple low wraps; the sticky alarm remains. */
    write_reg(&d, 0x9100, 1); advance(&d, 100000000);
    assert(read_reg(&d, 0x9100) == 1);
    /* Disable/reset freezes the unit, re-enable starts its reset state. */
    write_reg(&d, 0x200, 0); before = d; advance(&d, 10000000);
    assert(memcmp(&d, &before, sizeof d) == 0);
    write_reg(&d, 0x200, H2_ENGINE_TIMER);
    assert(read_reg(&d, 0x9200) == 1 && read_reg(&d, 0x9210) == 1);
    assert(read_reg(&d, 0x9100) == 0 && read_reg(&d, 0x9420) == 0);
    assert(read_reg(&d, 0x9400) == 0 && read_reg(&d, 0x9410) == 0);
    advance(&d, 1000000);
    assert(read_reg(&d, 0x9400) == 0xBD0C4980 && read_reg(&d, 0x9410) == 1);
    puts("NV2A timer: fractional rate, freeze, alarms, rollover, reset and rejected writes passed");
    return 0;
}
