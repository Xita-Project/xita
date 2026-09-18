#include "gpu_bus.h"
#include "nv2a_regs.h"
#include "instance_memory.h"
#include <string.h>

static h2_nv2a device;
static unsigned accesses;
static uint64_t last_time_us;
static uint32_t pramin_reads, pramin_writes, pramin_unique;
static uint8_t pramin_touched[0x10000 / 4 / 8];
extern void xv_logf(const char *format, ...);
/* Linked only by the separate experimental host-channel target. */
extern int h2_host_channel_bus(xctx *, uint32_t, uint32_t, unsigned, uint32_t *, int)
    __attribute__((weak));

void h2_gpu_bus_reset(uint32_t physical_memory_bytes)
{
    h2_nv2a_reset(&device, physical_memory_bytes);
    accesses = 0;
    pramin_reads = pramin_writes = pramin_unique = 0;
    memset(pramin_touched, 0, sizeof pramin_touched);
    last_time_us = h2_graphics_time_us();
}

void h2_gpu_bus_report(void)
{
    xv_logf("[h2/pramin] reads=%u writes=%u unique_words=%u claimed=%08X ramht=%08X ramfc=%08X\n",
            pramin_reads, pramin_writes, pramin_unique, h2_instance_bytes(), device.fifo_ramht, device.fifo_ramfc);
}

static void advance_time(xctx *context, uint32_t instruction, uint32_t address, uint32_t value, int write)
{
    device.instance_bytes = h2_instance_bytes();
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
    if (h2_host_channel_bus && h2_host_channel_bus(context, instruction, address, width, &value, 0))
        return value;
    advance_time(context, instruction, address, 0, 0);
    enum h2_nv2a_result result;
    if (address >= 0xFD700000u && address < 0xFD800000u) {
        uint32_t mapped;
        result = h2_nv2a_pramin_address(&device, address - 0xFD700000u, width, &mapped);
        if (result == H2_NV2A_OK) { x_guest_read(&value, mapped, width); ++pramin_reads; }
    } else {
        result = h2_nv2a_read(&device, address - 0xFD000000u, width, &value);
    }
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, 0, 0, result);
    if (!(address >= 0xFD700000u && address < 0xFD800000u) && ++accesses <= 128) xv_logf("[h2/mmio] read%u eip=%08X address=%08X value=%08X\n", width * 8, instruction, address, value);
    return value;
}

uint32_t h2_bus_read8(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 1); }
uint32_t h2_bus_read16(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 2); }
uint32_t h2_bus_read32(xctx *context, uint32_t instruction, uint32_t address)
{ return bus_read(context, instruction, address, 4); }

static void bus_write(xctx *context, uint32_t instruction, uint32_t address, uint32_t value, unsigned width)
{
    value &= 0xFFFFFFFFu >> (8 * (4 - width));
    if (!is_mmio(address, width)) {
        x_guest_write(address, &value, width);
        return;
    }
    if (h2_host_channel_bus && h2_host_channel_bus(context, instruction, address, width, &value, 1))
        return;
    advance_time(context, instruction, address, value, 1);
    enum h2_nv2a_result result;
    if (address >= 0xFD700000u && address < 0xFD800000u) {
        uint32_t mapped;
        result = h2_nv2a_pramin_address(&device, address - 0xFD700000u, width, &mapped);
        if (result == H2_NV2A_OK) {
            x_guest_write(mapped, &value, width);
            ++pramin_writes;
            uint32_t index = (address - 0xFD710000u) / 4;
            if (!(pramin_touched[index / 8] & (1u << (index & 7)))) {
                pramin_touched[index / 8] |= 1u << (index & 7);
                ++pramin_unique;
            }
        }
    } else {
        result = h2_nv2a_write(&device, address - 0xFD000000u, width, value);
    }
    if (result != H2_NV2A_OK) h2_graphics_stop(context, instruction, address, value, 1, result);
    if (!(address >= 0xFD700000u && address < 0xFD800000u) && ++accesses <= 128) xv_logf("[h2/mmio] write%u eip=%08X address=%08X value=%08X\n", width * 8, instruction, address, value);
}

void h2_bus_write8(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{ bus_write(c, ip, address, value, 1); }
void h2_bus_write16(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{ bus_write(c, ip, address, value, 2); }
void h2_bus_write32(xctx *c, uint32_t ip, uint32_t address, uint32_t value)
{ bus_write(c, ip, address, value, 4); }
