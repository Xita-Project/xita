/* Register meanings checked against the pinned primary xemu NV2A device,
 * PBUS, PFB, PCRTC and PTIMER implementations. This is an independent limited
 * model: unsupported addresses/operations are errors, never generic zero/OK.
 */
#include "nv2a_regs.h"
#include <string.h>

#define TIMER_TICK_MASK ((1ull << 56) - 1)
#define TIMER_LOW_MASK ((1u << 27) - 1)

static void reset_timer(h2_nv2a *device)
{
    device->timer_ticks = device->timer_source_fraction = 0;
    device->timer_divider = device->timer_multiplier = 1;
    device->timer_ratio_fraction = device->timer_alarm = device->timer_pending = 0;
    device->timer_interrupt_enable = 0;
}

void h2_nv2a_reset(h2_nv2a *device, uint32_t memory_bytes)
{
    memset(device, 0, sizeof *device);
    device->memory_bytes = memory_bytes;
    reset_timer(device);
    /* The HLE loader exposes the MMIO BAR before entering the guest driver.
     * Bus mastering remains disabled until the guest requests PCI command bit 2.
     * This is virtual boot configuration, not a captured retail power-on value. */
    device->pci_command = 2;
    /* HLE boot configuration exposes these units before the title's driver.
     * This is not a retail reset snapshot. Other engines have no operations. */
    device->master_enable = H2_ENGINE_TIMER | H2_ENGINE_FB | H2_ENGINE_CRTC;
    device->core_pll = 0x00011C01; /* 233333324 Hz at the 16666666 Hz crystal */
    device->memory_pll = 0x00000C01; /* virtual 199999992 Hz, not a captured PLL */
    device->video_pll = 0x0003C20D; /* 31089742 Hz; same boot coefficient as xemu */
}

uint32_t h2_nv2a_pll_hz(uint32_t coefficient)
{
    uint32_t m = coefficient & 255, n = (coefficient >> 8) & 255;
    uint32_t p = (coefficient >> 16) & 7;
    return m ? (uint32_t)((16666666ull * n) / (1u << p) / m) : 0;
}

enum h2_nv2a_result h2_nv2a_advance_us(h2_nv2a *device, uint64_t elapsed_us)
{
    if (!(device->master_enable & H2_ENGINE_TIMER)) return H2_NV2A_OK;
    uint64_t hz = h2_nv2a_pll_hz(device->core_pll);
    uint64_t seconds = elapsed_us / 1000000;
    uint64_t fraction = (elapsed_us % 1000000) * hz + device->timer_source_fraction;
    /* Split multiplication so ordinary uptime cannot overflow on 32-bit ARM.
     * Reject impossible elapsed intervals before touching any timer state. */
    uint64_t extra_cycles = fraction / 1000000;
    if (hz && seconds > (UINT64_MAX - extra_cycles) / hz)
        return H2_NV2A_INVALID_ACCESS;
    uint64_t cycles = seconds * hz + extra_cycles;
    uint32_t divider = device->timer_divider, multiplier = device->timer_multiplier;
    if (!divider || multiplier > divider) return H2_NV2A_UNSUPPORTED_OPERATION;
    uint64_t remainder = (cycles % divider) * multiplier + device->timer_ratio_fraction;
    uint64_t increment = (cycles / divider) * multiplier + remainder / divider;
    uint32_t distance = ((device->timer_alarm >> 5) - (uint32_t)device->timer_ticks) & TIMER_LOW_MASK;
    uint64_t next_match = distance ? distance : 1ull << 27;
    if (increment >= next_match) device->timer_pending |= 1;
    device->timer_ticks = (device->timer_ticks + increment) & TIMER_TICK_MASK;
    device->timer_source_fraction = fraction % 1000000;
    device->timer_ratio_fraction = (uint32_t)(remainder % divider);
    return H2_NV2A_OK;
}

static int unit_enabled(const h2_nv2a *device, uint32_t offset)
{
    uint32_t gate = offset >= 0x009000 && offset < 0x00A000 ? H2_ENGINE_TIMER :
                    offset >= 0x100000 && offset < 0x101000 ? H2_ENGINE_FB :
                    offset >= 0x600000 && offset < 0x601000 ? H2_ENGINE_CRTC : 0;
    return !gate || (device->master_enable & gate);
}

enum h2_nv2a_result h2_nv2a_read(const h2_nv2a *device, uint32_t offset,
                               unsigned width, uint32_t *value)
{
    if (!value || (width != 1 && width != 2 && width != 4) ||
        (offset & (width - 1)) || offset >= 0x1000000u)
        return H2_NV2A_INVALID_ACCESS;
    uint32_t whole;
    enum h2_nv2a_result result = h2_nv2a_read32(device, offset & ~3u, &whole);
    if (result == H2_NV2A_OK)
        *value = (whole >> (8 * (offset & 3))) & (0xFFFFFFFFu >> (8 * (4 - width)));
    return result;
}

enum h2_nv2a_result h2_nv2a_read32(const h2_nv2a *device, uint32_t offset, uint32_t *value)
{
    if ((offset & 3) || offset >= 0x1000000u || !value) return H2_NV2A_INVALID_ACCESS;
    if (!(device->pci_command & 2) || !unit_enabled(device, offset)) return H2_NV2A_UNSUPPORTED_OPERATION;
    uint32_t result;
    switch (offset) {
    case 0x000200: result = device->master_enable; break;
    case 0x000140: result = device->master_interrupt_enable; break;
    case 0x680500: result = device->core_pll; break;
    case 0x680504: result = device->memory_pll; break;
    case 0x680508: result = device->video_pll; break;
    case 0x001800: result = 0x02A010DEu; break; /* NV2A device/vendor */
    case 0x001804: result = device->pci_command; break;
    case 0x001808: result = 0x030000A1u; break; /* VGA class, NV2A revision A1 */
    case 0x00180C: result = device->pci_latency; break;
    case 0x001830: result = device->pci_rom; break;
    case 0x10020C: result = device->memory_bytes; break; /* PFB CSTATUS */
    case 0x600140: result = device->crtc_interrupt_enable; break;
    case 0x009100: result = device->timer_pending; break;
    case 0x009200: result = device->timer_divider; break;
    case 0x009210: result = device->timer_multiplier; break;
    case 0x009400: result = (uint32_t)(device->timer_ticks << 5); break;
    case 0x009410: result = (uint32_t)(device->timer_ticks >> 27); break;
    case 0x009420: result = device->timer_alarm; break;
    case 0x009140: result = device->timer_interrupt_enable; break;
    default: return H2_NV2A_UNKNOWN_REGISTER;
    }
    *value = result;
    return H2_NV2A_OK;
}

enum h2_nv2a_result h2_nv2a_write32(h2_nv2a *device, uint32_t offset, uint32_t value)
{
    if ((offset & 3) || offset >= 0x1000000u) return H2_NV2A_INVALID_ACCESS;
    if (!(device->pci_command & 2) || !unit_enabled(device, offset)) return H2_NV2A_UNSUPPORTED_OPERATION;
    switch (offset) {
    case 0x000200:
        /* Engine gates, not submission or completion. Disabled modeled units
         * reset and disappear. Bits for other units are retained, but none of
         * their unknown registers become accessible by enabling a bit. */
        if (!(value & H2_ENGINE_TIMER)) reset_timer(device);
        if (!(value & H2_ENGINE_CRTC)) device->crtc_interrupt_enable = 0;
        device->master_enable = value;
        return H2_NV2A_OK;
    case 0x000140:
        if (value) return H2_NV2A_UNSUPPORTED_OPERATION; /* no IRQ delivery yet */
        device->master_interrupt_enable = 0;
        return H2_NV2A_OK;
    case 0x680500: case 0x680504: case 0x680508:
        return H2_NV2A_UNSUPPORTED_OPERATION; /* clock reprogramming not modeled */
    case 0x001804:
        /* No PCI error/status events are modeled. Unknown command requests
         * are rejected before mutation; the observed guest enables bus master. */
        if (value & ~7u) return H2_NV2A_UNSUPPORTED_OPERATION;
        device->pci_command = value;
        return H2_NV2A_OK;
    case 0x00180C:
        if (value & 0xFFFF0000u) return H2_NV2A_UNSUPPORTED_OPERATION;
        device->pci_latency = value; /* cache-line size and latency timer */
        return H2_NV2A_OK;
    case 0x001830:
        if (value) return H2_NV2A_UNSUPPORTED_OPERATION; /* no expansion ROM */
        device->pci_rom = 0;
        return H2_NV2A_OK;
    case 0x600140:
        if (value) return H2_NV2A_UNSUPPORTED_OPERATION; /* no vblank source yet */
        device->crtc_interrupt_enable = 0;
        return H2_NV2A_OK;
    case 0x009200:
        if (!value || value > 0xFFFF || value < device->timer_multiplier)
            return H2_NV2A_UNSUPPORTED_OPERATION;
        device->timer_divider = value;
        device->timer_ratio_fraction = 0;
        return H2_NV2A_OK;
    case 0x009210:
        if (value > 0xFFFF || value > device->timer_divider)
            return H2_NV2A_UNSUPPORTED_OPERATION;
        device->timer_multiplier = value;
        device->timer_ratio_fraction = 0;
        return H2_NV2A_OK;
    case 0x009420:
        device->timer_alarm = value & 0xFFFFFFE0u;
        return H2_NV2A_OK;
    case 0x009100:
        device->timer_pending &= ~(value & 1u); /* write-one-to-clear */
        return H2_NV2A_OK;
    case 0x009400: case 0x009410:
        return H2_NV2A_UNSUPPORTED_OPERATION; /* time-counter writes not yet modeled */
    case 0x009140:
        if (value) return H2_NV2A_UNSUPPORTED_OPERATION; /* no timer/alarm source yet */
        device->timer_interrupt_enable = 0;
        return H2_NV2A_OK;
    case 0x001800: case 0x001808: case 0x10020C:
        return H2_NV2A_UNSUPPORTED_OPERATION; /* read-only identity/memory size */
    default: return H2_NV2A_UNKNOWN_REGISTER;
    }
}
