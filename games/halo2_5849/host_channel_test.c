#include "host_channel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture { uint32_t ram[1024], instance[0x5000 / 4]; int fail_read; } fixture;
static int read_ram(void *opaque, uint32_t address, uint32_t *word)
{
    fixture *f = opaque;
    if (f->fail_read || (address & 3) || address >= sizeof f->ram) return 0;
    *word = f->ram[address / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t address, uint32_t bytes)
{
    fixture *f = opaque;
    return address <= sizeof f->ram && bytes <= sizeof f->ram - address ? (uint8_t *)f->ram + address : NULL;
}
static int read_instance(void *opaque, uint32_t offset, uint32_t *word)
{
    fixture *f = opaque;
    if ((offset & 3) || offset < 0x10000 || offset >= 0x15000) return 0;
    *word = f->instance[(offset - 0x10000) / 4]; return 1;
}
static void setup(fixture *f, h2_host_channel *c)
{
    memset(f, 0, sizeof *f);
    f->ram[0] = 0x101;
    f->instance[13 * 2] = 13; f->instance[13 * 2 + 1] = 0x80011200;
    f->instance[0x2000 / 4] = 0x97;
    h2_dma_object dma = {0, sizeof f->ram - 1, 2, 0};
    assert(h2_host_channel_init(c, &dma, 0x100, 0x100, read_ram, read_instance, map_ram, f, sizeof f->ram));
}
int main(void)
{
    fixture f; h2_host_channel c, before; h2_push_fault fault;
    setup(&f, &c); assert(h2_host_channel_get(&c) == 0);
    before = c;
    assert(h2_host_channel_submit(&c, 0x102, 8, &fault) == H2_PUSH_BAD_STATE);
    assert(!memcmp(&before, &c, sizeof c));
    assert(h2_host_channel_submit(&c, 0x100, 0, &fault) == H2_PUSH_BUDGET_EXHAUSTED);
    assert(h2_host_channel_get(&c) == 0);
    f.fail_read = 1;
    assert(h2_host_channel_submit(&c, 0x100, 8, &fault) == H2_PUSH_MEMORY_FAULT);
    f.fail_read = 0; f.ram[0] = 0x201;
    assert(h2_host_channel_submit(&c, 0x100, 8, &fault) == H2_PUSH_UNSUPPORTED_COMMAND);
    assert(fault.address == 0 && fault.word == 0x201 && h2_host_channel_get(&c) == 0);
    f.ram[0] = 0x101;
    assert(h2_host_channel_submit(&c, 0x100, 1, &fault) == H2_PUSH_COMPLETE);
    assert(h2_host_channel_get(&c) == 0x100 && !c.bootstrap);
    f.ram[0] = 0xDEADBEEF; /* bootstrap is never replayed */
    f.ram[0x100 / 4] = 0x40000; f.ram[0x104 / 4] = 13;
    assert(h2_host_channel_submit(&c, 0x104, 8, &fault) == H2_PUSH_NEED_DATA);
    assert(c.stream.remaining == 1 && !c.clear.has_object);
    assert(h2_host_channel_submit(&c, 0x108, 8, &fault) == H2_PUSH_COMPLETE);
    assert(c.clear.has_object && c.clear.object_instance == 0x12000);
    f.ram[0x108 / 4] = 0x40000 | 0x1800; f.ram[0x10C / 4] = 1;
    assert(h2_host_channel_submit(&c, 0x110, 8, &fault) == H2_PUSH_METHOD_REJECTED);
    assert(h2_host_channel_get(&c) == 0x10C && fault.address == 0x10C && fault.method == 0x1800);
    assert(!c.clear.completed_clears);
    setup(&f, &c); before = c;
    h2_dma_object invalid = {0, 0x1FE, 2, 0};
    assert(!h2_host_channel_init(&c, &invalid, 0x100, 0x100, read_ram, read_instance, map_ram, &f, sizeof f.ram));
    assert(!memcmp(&before, &c, sizeof c));
    invalid.limit = sizeof f.ram - 1; invalid.object_class = 3;
    assert(!h2_host_channel_init(&c, &invalid, 0x100, 0x100, read_ram, read_instance, map_ram, &f, sizeof f.ram));
    invalid.object_class = 2; invalid.address = 4;
    assert(!h2_host_channel_init(&c, &invalid, 0x100, 0x100, read_ram, read_instance, map_ram, &f, sizeof f.ram));
    setup(&f, &c); f.ram[0x100 / 4] = 0x101;
    assert(h2_host_channel_submit(&c, 0x104, 20, &fault) == H2_PUSH_BUDGET_EXHAUSTED);
    assert(h2_host_channel_get(&c) == 0x100);
    /* A split release packet writes only when its value arrives. A later
     * unsupported method cannot replay the completed semaphore. */
    setup(&f, &c);
    f.instance[14 * 2] = 14; f.instance[14 * 2 + 1] = 0x80001300;
    f.instance[0x3000 / 4] = 0x8000B003;
    f.instance[0x3004 / 4] = 3;
    f.instance[0x3008 / 4] = f.instance[0x300C / 4] = 3;
    uint32_t program[] = {0x40000, 13, 0x401A4, 14, 0x41D6C, 0, 0x41D70, 0x12345678, 0x41800, 1};
    memcpy((uint8_t *)f.ram + 0x100, program, sizeof program);
    assert(h2_host_channel_submit(&c, 0x11C, 100, &fault) == H2_PUSH_NEED_DATA);
    assert(!c.commands.semaphore_releases && !f.ram[0x800 / 4]);
    assert(h2_host_channel_submit(&c, 0x120, 100, &fault) == H2_PUSH_COMPLETE);
    assert(c.commands.semaphore_releases == 1 && f.ram[0x800 / 4] == 0x12345678);
    assert(h2_host_channel_submit(&c, 0x128, 100, &fault) == H2_PUSH_METHOD_REJECTED);
    assert(c.stream.get == 0x124 && c.commands.semaphore_releases == 1);
    assert(h2_host_channel_submit(&c, 0x128, 100, &fault) == H2_PUSH_METHOD_REJECTED);
    assert(c.commands.semaphore_releases == 1);
    puts("Host channel: checked bootstrap, DMA bounds, partial packets and honest rejection/progress pass.");
    return 0;
}
