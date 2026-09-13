#include "kelvin_clear.h"
#include "push_stream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint32_t instance[0x5000 / 4], push[64], fail_map;
    unsigned push_count;
    uint32_t alias_map;
    uint8_t ram[4096];
    h2_kelvin_clear consumer;
} fixture;
static int instance_read(void *opaque, uint32_t address, uint32_t *word)
{
    fixture *f = opaque;
    if ((address & 3) || address < 0x10000 || address >= 0x15000) return 0;
    *word = f->instance[(address - 0x10000) / 4]; return 1;
}
static void *map_physical(void *opaque, uint32_t address, uint32_t bytes)
{
    fixture *f = opaque;
    if (address == f->fail_map || address > sizeof f->ram || bytes > sizeof f->ram - address) return NULL;
    if (address == 0x300 && f->alias_map) return f->ram + f->alias_map;
    return f->ram + address;
}
static int push_read(void *opaque, uint32_t address, uint32_t *word)
{
    fixture *f = opaque;
    if ((address & 3) || address < 0x20000 || address >= 0x20000 + sizeof f->push) return 0;
    *word = f->push[(address - 0x20000) / 4]; return 1;
}
static int consume(void *opaque, uint8_t sub, uint16_t method, uint32_t value, uint32_t address)
{ return h2_kelvin_clear_method(&((fixture *)opaque)->consumer, sub, method, value, address); }
static void inst(fixture *f, uint32_t offset, uint32_t value)
{ f->instance[(offset - 0x10000) / 4] = value; }
static void dma(fixture *f, unsigned handle, uint32_t instance, uint32_t base, uint32_t limit)
{
    inst(f, 0x10000 + handle * 8, handle); inst(f, 0x10004 + handle * 8, 0x80000000u | (instance >> 4));
    inst(f, instance, (base << 20) | 0xB03D); inst(f, instance + 4, limit);
    inst(f, instance + 8, base | 3); inst(f, instance + 12, base | 3);
}
static void setup(fixture *f)
{
    memset(f, 0, sizeof *f); memset(f->ram, 0xA5, sizeof f->ram);
    inst(f, 0x10068, 13); inst(f, 0x1006C, 0x80011200); inst(f, 0x12000, 0x97);
    dma(f, 3, 0x11120, 0x100, 63); dma(f, 4, 0x11130, 0x300, 63);
    assert(h2_kelvin_clear_init(&f->consumer, instance_read, map_physical, f, sizeof f->ram, 0));
}
static void packet(fixture *f, uint16_t method, uint32_t value)
{ assert(f->push_count + 2 <= 64); f->push[f->push_count++] = 0x40000 | method; f->push[f->push_count++] = value; }
static uint32_t pixel(const fixture *f, uint32_t address)
{
    const uint8_t *p = f->ram + address;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void state_packets(fixture *f)
{
    packet(f, 0, 13); packet(f, 0x194, 3); packet(f, 0x198, 4);
    packet(f, 0x200, 4u << 16); packet(f, 0x204, 3u << 16);
    packet(f, 0x208, 0x128); packet(f, 0x20C, 24u | (24u << 16));
    packet(f, 0x210, 0); packet(f, 0x214, 0);
    packet(f, 0x1D90, 0x12345678); packet(f, 0x1D8C, 0x87654321);
    packet(f, 0x1D98, 1u | (2u << 16)); packet(f, 0x1D9C, 1u | (2u << 16));
}
static void rejected(fixture *f, uint16_t method, uint32_t value)
{
    h2_kelvin_clear before = f->consumer; uint8_t ram[sizeof f->ram]; memcpy(ram, f->ram, sizeof ram);
    assert(!consume(f, 0, method, value, 0));
    assert(memcmp(&before, &f->consumer, sizeof before) == 0 && memcmp(ram, f->ram, sizeof ram) == 0);
}
int main(void)
{
    fixture f; setup(&f); state_packets(&f);
    packet(&f, 0x1D94, 0xF3); /* both attachments, all component lanes */
    h2_push_stream parser; h2_push_fault fault;
    assert(h2_push_init(&parser, 0x20000, sizeof f.push));
    assert(h2_push_run(&parser, 0x20000 + f.push_count * 4, 64, push_read, consume, &f, &fault) == H2_PUSH_COMPLETE);
    assert(f.consumer.completed_clears == 1 && f.consumer.written_pixels == 8);
    for (unsigned a = 0; a < sizeof f.ram; a += 4) {
        uint32_t expected = 0xA5A5A5A5;
        for (unsigned y = 1; y <= 2; ++y) for (unsigned x = 1; x <= 2; ++x) {
            if (a == 0x100 + y * 24 + x * 4) expected = 0x12345678;
            if (a == 0x300 + y * 24 + x * 4) expected = 0x87654321;
        }
        assert(pixel(&f, a) == expected); /* includes untouched pixels, row padding and guards */
    }
    /* Partial color lanes and separate depth/stencil preserve other bits. */
    assert(consume(&f, 0, 0x1D90, 0xEEDDCCBB, 0));
    assert(consume(&f, 0, 0x1D8C, 0x11223344, 0));
    assert(consume(&f, 0, 0x1D94, 0x52, 0));
    assert(pixel(&f, 0x11C) == 0x12DD56BB && pixel(&f, 0x31C) == 0x87654344);
    assert(consume(&f, 0, 0x1D94, 1, 0));
    assert(pixel(&f, 0x31C) == 0x11223344 && pixel(&f, 0x11C) == 0x12DD56BB);
    /* Validate both targets before writing either, even when the second mapping fails. */
    f.fail_map = 0x300; rejected(&f, 0x1D94, 0xF3); f.fail_map = 0;
    f.alias_map = 0x110; rejected(&f, 0x1D94, 0xF3); /* distinct physical, overlapping host spans */
    f.alias_map = 0x100; rejected(&f, 0x1D94, 0xF3); /* exact host alias */
    f.alias_map = 0;
    inst(&f, 0x11134, 62); rejected(&f, 0x1D94, 0xF3); inst(&f, 0x11134, 63);
    dma(&f, 4, 0x11130, 0x100, 63); rejected(&f, 0x1D94, 0xF3); dma(&f, 4, 0x11130, 0x300, 63);
    rejected(&f, 0x1D94, 4); rejected(&f, 0x1D98, 0xF0000000); rejected(&f, 0x1810, 1);
    inst(&f, 0x11120, 0x1000B002); rejected(&f, 0x1D94, 0xF0); inst(&f, 0x11120, 0x1000B03D);
    inst(&f, 0x10070, 13); inst(&f, 0x10074, 0x80011210); inst(&f, 0x12100, 0x97);
    rejected(&f, 0, 13); /* ambiguous handle */
    inst(&f, 0x10070, 14); rejected(&f, 0, 14); /* different context unsupported */
    inst(&f, 0x10074, 0);
    rejected(&f, 0x194, 55); rejected(&f, 0, 3); /* DMA cannot be bound as a graphics class */
    h2_kelvin_clear before = f.consumer;
    assert(!consume(&f, 1, 0x1D94, 0xF0, 0) && memcmp(&before, &f.consumer, sizeof before) == 0);
    assert(consume(&f, 1, 0, 13, 0)); assert(consume(&f, 1, 0x1D94, 0xF0, 0));
    /* Unsupported surface layouts are retained as state but cannot clear. */
    assert(consume(&f, 0, 0x208, 0x228, 0)); rejected(&f, 0x1D94, 0xF0);
    assert(consume(&f, 0, 0x208, 0x1128, 0)); rejected(&f, 0x1D94, 0xF0);
    assert(consume(&f, 0, 0x208, 0x128, 0));
    assert(consume(&f, 0, 0x200, (4u << 16) | 1, 0)); rejected(&f, 0x1D94, 0xF0);
    assert(consume(&f, 0, 0x200, 4u << 16, 0));
    assert(consume(&f, 0, 0x1D98, 4u << 16, 0)); rejected(&f, 0x1D94, 0xF0);
    assert(consume(&f, 0, 0x1D98, (1u << 16) | 2, 0)); rejected(&f, 0x1D94, 0xF0);
    assert(consume(&f, 0, 0x1D98, 2u << 16, 0));
    assert(consume(&f, 0, 0x20C, 24u << 16 | 12, 0)); rejected(&f, 0x1D94, 0xF0);
    /* A rejected method keeps the parser at the exact offending data word. */
    f.push_count = 0; packet(&f, 0x1810, 1); h2_push_init(&parser, 0x20000, sizeof f.push);
    assert(h2_push_run(&parser, 0x20008, 2, push_read, consume, &f, &fault) == H2_PUSH_METHOD_REJECTED);
    assert(parser.get == 0x20004 && fault.method == 0x1810 && fault.word == 1);
    puts("Halo 2 clear consumer: parser-to-pixels, ARGB/depth/stencil masks, rectangle/pitch guards and atomic rejection passed");
    return 0;
}
