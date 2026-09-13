#include "gpu_bus.h"
#include "nv2a_regs.h"

static h2_nv2a device;
static unsigned accesses;
static uint64_t last_time_us;
extern void xv_logf(const char *format, ...);

void h2_gpu_bus_reset(uint32_t physical_memory_bytes)
{
    h2_nv2a_reset(&device, physical_memory_bytes);
    accesses = 0;
    last_time_us = h2_graphics_time_us();
}

static void advance_time(xctx *context, uint32_t instruction, uint32_t address, uint32_t value, int write)
{
    uint64_t now = h2_graphics_time_us();
    if (now < last_time_us)
        h2_graphics_stop(context, instruction, address, value, write, H2_NV2A_INVALID_ACCESS);
    enum h2_nv2a_result result = h2_nv2a_advance_us(&device, now - last_time_us);
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, value, write, result);
    last_time_us = now;
}

static int is_mmio(uint32_t address, unsigned width)
{
    /* Include scalar accesses that would straddle the BAR's lower boundary. */
    return address >= 0xFD000000u - (width - 1) && address < 0xFE000000u;
}

static uint32_t bus_read(xctx *context, uint32_t instruction, uint32_t address, unsigned width)
{
    uint32_t value = 0;
    if (!is_mmio(address, width)) {
        x_guest_read(&value, address, width);
        return value;
    }
    advance_time(context, instruction, address, 0, 0);
    enum h2_nv2a_result result = h2_nv2a_read(&device, address - 0xFD000000u, width, &value);
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, 0, 0, result);
    if (++accesses <= 128) xv_logf("[h2/mmio] read%u eip=%08X address=%08X value=%08X\n", width * 8, instruction, address, value);
    return value;
}

uint32_t h2_bus_read8(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 1); }
uint32_t h2_bus_read16(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 2); }
uint32_t h2_bus_read32(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 4); }

void h2_bus_write32(xctx *context, uint32_t instruction, uint32_t address, uint32_t value)
{
    if (!is_mmio(address, 4)) {
        x_guest_write(address, &value, sizeof value);
        return;
    }
    advance_time(context, instruction, address, value, 1);
    enum h2_nv2a_result result = h2_nv2a_write32(&device, address - 0xFD000000u, value);
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, value, 1, result);
    if (++accesses <= 128) xv_logf("[h2/mmio] write eip=%08X address=%08X value=%08X\n", instruction, address, value);
}
