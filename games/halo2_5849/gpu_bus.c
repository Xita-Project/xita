#include "gpu_bus.h"
#include "nv2a_regs.h"

static h2_nv2a device;
static unsigned accesses;
extern void xv_logf(const char *format, ...);

void h2_gpu_bus_reset(uint32_t physical_memory_bytes)
{
    h2_nv2a_reset(&device, physical_memory_bytes);
    accesses = 0;
}

static int is_mmio(uint32_t address)
{
    /* Include scalar accesses that would straddle the BAR's lower boundary. */
    return address >= 0xFCFFFFFDu && address < 0xFE000000u;
}

uint32_t h2_bus_read32(xctx *context, uint32_t instruction, uint32_t address)
{
    uint32_t value;
    if (!is_mmio(address)) {
        x_guest_read(&value, address, sizeof value);
        return value;
    }
    enum h2_nv2a_result result = h2_nv2a_read32(&device, address - 0xFD000000u, &value);
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, 0, 0, result);
    if (++accesses <= 128) xv_logf("[h2/mmio] read eip=%08X address=%08X value=%08X\n", instruction, address, value);
    return value;
}

void h2_bus_write32(xctx *context, uint32_t instruction, uint32_t address, uint32_t value)
{
    if (!is_mmio(address)) {
        x_guest_write(address, &value, sizeof value);
        return;
    }
    enum h2_nv2a_result result = h2_nv2a_write32(&device, address - 0xFD000000u, value);
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, value, 1, result);
    if (++accesses <= 128) xv_logf("[h2/mmio] write eip=%08X address=%08X value=%08X\n", instruction, address, value);
}
