/* Register meanings checked against the pinned primary xemu NV2A device,
 * PBUS, PFB, PCRTC and PTIMER implementations. This is an independent limited
 * model: unsupported addresses/operations are errors, never generic zero/OK.
 */
#include "nv2a_regs.h"
#include <string.h>

void h2_nv2a_reset(h2_nv2a *device, uint32_t memory_bytes)
{
    memset(device, 0, sizeof *device);
    device->memory_bytes = memory_bytes;
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
        if (!(value & H2_ENGINE_TIMER)) device->timer_interrupt_enable = 0;
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
    case 0x009140:
        if (value) return H2_NV2A_UNSUPPORTED_OPERATION; /* no timer/alarm source yet */
        device->timer_interrupt_enable = 0;
        return H2_NV2A_OK;
    case 0x001800: case 0x001808: case 0x10020C:
        return H2_NV2A_UNSUPPORTED_OPERATION; /* read-only identity/memory size */
    default: return H2_NV2A_UNKNOWN_REGISTER;
    }
}
