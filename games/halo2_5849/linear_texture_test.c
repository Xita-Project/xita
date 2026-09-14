#include "linear_texture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct {
    uint32_t instance[8];
    uint8_t ram[8192];
    int fail_read, fail_map, overflow_map;
} f;
static h2_command_state s;
static h2_kelvin_clear c;
static unsigned reads, maps;
static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    assert(opaque == &f); ++reads;
    if (f.fail_read || offset < 0x13000 || offset >= 0x13020 || (offset & 3)) return 0;
    *word = f.instance[(offset - 0x13000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t offset, uint32_t bytes)
{
    assert(opaque == &f); ++maps;
    if (f.fail_map || offset > sizeof f.ram || bytes > sizeof f.ram - offset) return NULL;
    if (f.overflow_map) return (void *)(UINTPTR_MAX - 8);
    return f.ram + offset;
}
static void set(unsigned unit, unsigned offset, uint32_t value)
{
    unsigned method = 0x1B00 + unit * 64 + offset;
    s.setup[method / 4] = value;
    s.setup_valid[method / 128] |= 1u << ((method / 4) % 32);
}
static void init(unsigned unit, unsigned selector)
{
    memset(&f, 0, sizeof f); memset(&s, 0, sizeof s); memset(&c, 0, sizeof c);
    for (unsigned i = 0; i < sizeof f.ram; ++i) f.ram[i] = i ^ (i >> 5);
    f.instance[0] = 0xB002 | (512u << 20); f.instance[1] = 47;
    f.instance[2] = f.instance[3] = 3;
    f.instance[4] = 0x2B03D; f.instance[5] = 47;
    f.instance[6] = f.instance[7] = 4099;
    s.dma[1] = 0x13000; s.dma[2] = 0x13010; s.dma_valid = 6;
    c.read_instance = read_word; c.map_physical = map_ram; c.opaque = &f;
    c.physical_bytes = sizeof f.ram;
    set(unit, 0, 16); set(unit, 4, 0x11E28 | selector);
    set(unit, 0xC, 0x40000000); set(unit, 0x10, 16 << 16); set(unit, 0x1C, (3 << 16) | 2);
    reads = maps = 0;
}
static void rejected(unsigned unit)
{
    h2_linear_texture out, before; memset(&out, 0xA5, sizeof out); before = out;
    h2_command_state old = s; h2_kelvin_clear memory = c; uint8_t ram[sizeof f.ram];
    memcpy(ram, f.ram, sizeof ram);
    assert(!h2_linear_texture_read(&s, &c, unit, &out));
    assert(!memcmp(&out, &before, sizeof out) && !memcmp(&old, &s, sizeof s));
    assert(!memcmp(&memory, &c, sizeof c) && !memcmp(ram, f.ram, sizeof ram));
}
int main(void)
{
    for (unsigned unit = 0; unit < 4; ++unit) for (unsigned selector = 1; selector <= 2; ++selector) {
        init(unit, selector); h2_command_state old = s; h2_kelvin_clear memory = c;
        uint8_t ram[sizeof f.ram]; memcpy(ram, f.ram, sizeof ram);
        h2_linear_texture out;
        assert(h2_linear_texture_read(&s, &c, unit, &out));
        assert(reads == 4 && maps == 1 && out.physical == (selector == 1 ? 528 : 4112));
        assert(out.pixels == f.ram + out.physical && out.bytes == 32);
        assert(out.width == 3 && out.height == 2 && out.pitch == 16 && out.method_format == (0x11E28 | selector));
        assert(!memcmp(&s, &old, sizeof s) && !memcmp(&c, &memory, sizeof c));
        assert(!memcmp(ram, f.ram, sizeof ram));
    }
    const unsigned fields[] = {0, 4, 0xC, 0x10, 0x1C};
    for (unsigned i = 0; i < 5; ++i) {
        init(0, 1); unsigned m = 0x1B00 + fields[i];
        s.setup_valid[m / 128] &= ~(1u << ((m / 4) % 32)); rejected(0); assert(!reads && !maps);
    }
    const uint32_t formats[] = {0x11E28, 0x11E2B, 0x11E2D, 0x11E21, 0x11E19, 0x11229, 0x21E29, 0x111E29};
    for (unsigned i = 0; i < sizeof formats / sizeof *formats; ++i) {
        init(0, 1); set(0, 4, formats[i]); rejected(0); assert(!reads && !maps);
    }
    const uint32_t rects[] = {0, 3 << 16, 2, (4097u << 16) | 2, (3 << 16) | 4097};
    for (unsigned i = 0; i < sizeof rects / sizeof *rects; ++i) {
        init(0, 1); set(0, 0x1C, rects[i]); rejected(0); assert(!reads && !maps);
    }
    init(0, 1); set(0, 0x10, 11 << 16); rejected(0);
    init(0, 1); set(0, 0x10, (16 << 16) | 1); rejected(0);
    init(0, 1); set(0, 0xC, 0); rejected(0);
    init(0, 1); s.dma_valid &= ~2u; rejected(0);
    init(0, 1); s.dma[1]++; rejected(0);
    init(0, 1); f.fail_read = 1; rejected(0); assert(!maps);
    init(0, 1); f.instance[0]++; rejected(0); assert(!maps); /* write-only object */
    init(0, 1); f.instance[0] |= 0x10000; rejected(0); assert(!maps); /* unsupported target */
    init(0, 1); f.instance[3] ^= 0x1000; rejected(0); assert(!maps);
    init(0, 1); f.instance[1]--; rejected(0); assert(!maps); /* inclusive DMA limit */
    init(0, 1); c.physical_bytes = 559; rejected(0); assert(!maps);
    init(0, 1); set(0, 0, UINT32_MAX - 15); rejected(0); assert(!maps);
    init(0, 1); f.fail_map = 1; rejected(0); assert(maps == 1);
    init(0, 1); f.overflow_map = 1; rejected(0); assert(maps == 1);
    init(0, 1); rejected(4); rejected(UINT32_MAX);
    c.read_instance = NULL; rejected(0); c.read_instance = read_word; c.map_physical = NULL; rejected(0);
    assert(!h2_linear_texture_read(NULL, &c, 0, NULL));
    assert(!h2_linear_texture_read(&s, NULL, 0, NULL));
    puts("Linear texture view: four units, DMA permissions/extents, format bounds and read-only rejection passed");
}
