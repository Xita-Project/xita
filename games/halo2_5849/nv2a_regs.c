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
}

enum h2_nv2a_result h2_nv2a_read32(const h2_nv2a *device, uint32_t offset, uint32_t *value)
{
    if ((offset & 3) || offset >= 0x1000000u || !value) return H2_NV2A_INVALID_ACCESS;
    if (!(device->pci_command & 2)) return H2_NV2A_UNSUPPORTED_OPERATION;
    uint32_t result;
    switch (offset) {
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
    if (!(device->pci_command & 2)) return H2_NV2A_UNSUPPORTED_OPERATION;
    switch (offset) {
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
