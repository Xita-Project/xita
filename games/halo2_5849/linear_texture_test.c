#include "linear_texture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct {
    uint32_t instance[8];
    uint8_t ram[32768];
    int fail_read, fail_map, overflow_map;
} f;
static h2_command_state s;
static h2_kelvin_clear c;
static unsigned reads, maps, mapped_offset, mapped_bytes;
static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    assert(opaque == &f); ++reads;
    if (f.fail_read || offset < 0x13000 || offset >= 0x13020 || (offset & 3)) return 0;
    *word = f.instance[(offset - 0x13000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t offset, uint32_t bytes)
{
    assert(opaque == &f); ++maps;
    mapped_offset = offset; mapped_bytes = bytes;
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
    reads = maps = mapped_offset = mapped_bytes = 0;
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
static uint32_t block_format, block_bytes;
static int (*block_read)(const h2_command_state *, const h2_kelvin_clear *, unsigned, h2_block_texture *);
static void block_init(unsigned unit, unsigned selector)
{
    init(unit, selector); set(unit, 4, block_format | selector);
    f.instance[1] = f.instance[5] = 16 + block_bytes - 1; /* inclusive full-block extent */
}
static void block_rejected(unsigned unit)
{
    h2_block_texture out, before; memset(&out, 0xA5, sizeof out); before = out;
    h2_command_state old = s; h2_kelvin_clear memory = c; uint8_t ram[sizeof f.ram];
    memcpy(ram, f.ram, sizeof ram);
    assert(!block_read(&s, &c, unit, &out));
    assert(!memcmp(&out, &before, sizeof out) && !memcmp(&old, &s, sizeof s));
    assert(!memcmp(&memory, &c, sizeof c) && !memcmp(ram, f.ram, sizeof ram));
}
static void block_tests(void)
{
    for (unsigned unit = 0; unit < 4; ++unit) for (unsigned selector = 1; selector <= 2; ++selector) {
        block_init(unit, selector); h2_command_state old = s; h2_kelvin_clear memory = c;
        uint8_t ram[sizeof f.ram]; memcpy(ram, f.ram, sizeof ram); h2_block_texture out;
        assert(block_read(&s, &c, unit, &out));
        assert(out.physical == (selector == 1 ? 528 : 4112) && out.blocks == f.ram + out.physical);
        assert(out.bytes == block_bytes && out.width == 8 && out.height == 8 && out.block_pitch == block_bytes / 2);
        assert(out.method_format == (block_format | selector) && reads == 4 && maps == 1);
        assert(!memcmp(&old, &s, sizeof s) && !memcmp(&memory, &c, sizeof c));
        assert(!memcmp(ram, f.ram, sizeof ram));
    }
    for (unsigned bit = 0; bit < 32; ++bit) {
        block_init(2, 1); set(2, 4, (block_format | 1u) ^ (1u << bit));
        block_rejected(2); assert(!reads && !maps);
    }
    for (unsigned i = 0; i < 3; ++i) {
        block_init(2, 1); unsigned m = 0x1B80 + (unsigned[]){0,4,12}[i];
        s.setup_valid[m / 128] &= ~(1u << ((m / 4) % 32));
        block_rejected(2); assert(!reads && !maps);
    }
    block_init(2, 1); set(2, 12, 0); block_rejected(2); assert(!reads && !maps);
    block_init(2, 1); s.dma_valid &= ~2u; block_rejected(2); assert(!reads && !maps);
    block_init(2, 1); s.dma[1]++; block_rejected(2); assert(!maps);
    block_init(2, 1); f.fail_read = 1; block_rejected(2); assert(!maps);
    block_init(2, 1); f.instance[0]++; block_rejected(2); assert(!maps);
    block_init(2, 1); f.instance[0] |= 0x10000; block_rejected(2); assert(!maps);
    block_init(2, 1); f.instance[3] ^= 4096; block_rejected(2); assert(!maps);
    block_init(2, 1); f.instance[1]--; block_rejected(2); assert(!maps);
    block_init(2, 1); c.physical_bytes = 528 + block_bytes - 1; block_rejected(2); assert(!maps);
    block_init(2, 1); set(2, 0, UINT32_MAX - 15); block_rejected(2); assert(!maps);
    block_init(2, 1); f.fail_map = 1; block_rejected(2); assert(maps == 1);
    block_init(2, 1); f.overflow_map = 1; block_rejected(2); assert(maps == 1);
    block_init(2, 1); block_rejected(4); block_rejected(UINT32_MAX);
    c.read_instance = NULL; block_rejected(2); c.read_instance = read_word; c.map_physical = NULL; block_rejected(2);
    assert(!block_read(NULL, &c, 0, NULL));
    assert(!block_read(&s, NULL, 0, NULL));
}
static void snapshot_tests(void)
{
    block_read = h2_dxt23_texture_snapshot_read;
    /* All admitted logical powers of two, both DMA selectors and all units.
     * Large views deliberately fail the real small host allocation; verify
     * the complete requested span rather than claiming backing it with RAM. */
    for (unsigned unit = 0; unit < 4; ++unit) for (unsigned selector = 1; selector <= 2; ++selector)
    for (unsigned x = 0; x <= 12; ++x) for (unsigned y = 0; y <= 12; ++y) {
        unsigned width = 1u << x, height = 1u << y;
        block_format = 0x10E28u | (x << 20) | (y << 24);
        block_bytes = ((width + 3) / 4) * ((height + 3) / 4) * 16;
        block_init(unit, selector); c.physical_bytes = 64 * 1024 * 1024;
        unsigned physical = selector == 1 ? 528 : 4112;
        h2_command_state old = s; h2_kelvin_clear memory = c;
        uint8_t ram[sizeof f.ram]; memcpy(ram, f.ram, sizeof ram);
        if (block_bytes > sizeof f.ram - physical) {
            block_rejected(unit);
        } else {
            h2_block_texture out;
            assert(block_read(&s, &c, unit, &out));
            assert(out.width == width && out.height == height && out.bytes == block_bytes);
            assert(out.block_pitch == ((width + 3) / 4) * 16);
            assert(out.physical == physical && out.blocks == f.ram + physical);
            assert(out.method_format == (block_format | selector));
        }
        assert(reads == 4 && maps == 1 && mapped_offset == physical && mapped_bytes == block_bytes);
        assert(!memcmp(&s, &old, sizeof s) && !memcmp(&c, &memory, sizeof c));
        assert(!memcmp(ram, f.ram, sizeof ram));
    }
    block_format = 0x03A10E28u; block_bytes = 8192; /* original 1024x8 shape */
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (bit >= 20 && bit < 28) continue;
        block_init(0, 1); set(0, 4, (block_format | 1u) ^ (1u << bit));
        block_rejected(0); assert(!reads && !maps);
    }
    for (unsigned axis = 0; axis < 2; ++axis) for (unsigned value = 13; value < 16; ++value) {
        block_init(0, 1);
        set(0, 4, 0x10E29u | (value << (20 + axis * 4)));
        block_rejected(0); assert(!reads && !maps);
    }
    for (unsigned i = 0; i < 3; ++i) {
        block_init(0, 1); unsigned m = 0x1B00 + (unsigned[]){0,4,12}[i];
        s.setup_valid[m / 128] &= ~(1u << ((m / 4) % 32));
        block_rejected(0); assert(!reads && !maps);
    }
    block_init(0, 1); f.instance[1]--; block_rejected(0); assert(!maps);
    block_init(0, 1); c.physical_bytes = 528 + block_bytes - 1; block_rejected(0); assert(!maps);
    block_init(0, 1); f.instance[0]++; block_rejected(0); assert(!maps);
    block_init(0, 1); f.overflow_map = 1; block_rejected(0); assert(maps == 1);
    block_init(0, 1); set(0, 0, UINT32_MAX - 15); block_rejected(0); assert(!maps);
    block_init(0, 1); block_rejected(4); block_rejected(UINT32_MAX);
    assert(!block_read(NULL, &c, 0, NULL));
    assert(!block_read(&s, NULL, 0, NULL));
    assert(!block_read(&s, &c, 0, NULL));
    /* Capturing the larger texture never admits it to existing draw readers. */
    block_init(0, 1); block_read = h2_dxt23_texture_read; block_rejected(0);
    block_read = h2_dxt1_texture_read; block_rejected(0); assert(!reads && !maps);
}
static void rectangle_tests(void)
{
    block_read = h2_dxt23_texture_rect_read;
    /* All admitted logical powers of two, both DMA selectors and all units.
     * Large views deliberately fail the real small host allocation; verify
     * the complete requested span rather than claiming backing it with RAM. */
    for (unsigned unit = 0; unit < 4; ++unit) for (unsigned selector = 1; selector <= 2; ++selector)
    for (unsigned x = 0; x <= 10; ++x) for (unsigned y = 0; y <= 10; ++y) {
        unsigned width = 1u << x, height = 1u << y;
        block_format = 0x10E28u | (x << 20) | (y << 24);
        block_bytes = ((width + 3) / 4) * ((height + 3) / 4) * 16;
        block_init(unit, selector); c.physical_bytes = 64 * 1024 * 1024;
        unsigned physical = selector == 1 ? 528 : 4112;
        h2_command_state old = s; h2_kelvin_clear memory = c;
        uint8_t ram[sizeof f.ram]; memcpy(ram, f.ram, sizeof ram);
        if (block_bytes > sizeof f.ram - physical) {
            block_rejected(unit);
        } else {
            h2_block_texture out;
            assert(block_read(&s, &c, unit, &out));
            assert(out.width == width && out.height == height && out.bytes == block_bytes);
            assert(out.block_pitch == ((width + 3) / 4) * 16);
            assert(out.physical == physical && out.blocks == f.ram + physical);
            assert(out.method_format == (block_format | selector));
        }
        assert(reads == 4 && maps == 1 && mapped_offset == physical && mapped_bytes == block_bytes);
        assert(!memcmp(&s, &old, sizeof s) && !memcmp(&c, &memory, sizeof c));
        assert(!memcmp(ram, f.ram, sizeof ram));
    }
    block_format = 0x03A10E28u; block_bytes = 8192; /* original 1024x8 shape */
    for (unsigned bit = 0; bit < 32; ++bit) {
        if (bit >= 20 && bit < 28) continue;
        block_init(0, 1); set(0, 4, (block_format | 1u) ^ (1u << bit));
        block_rejected(0); assert(!reads && !maps);
    }
    for (unsigned axis = 0; axis < 2; ++axis) for (unsigned value = 13; value < 16; ++value) {
        block_init(0, 1);
        set(0, 4, 0x10E29u | (value << (20 + axis * 4)));
        block_rejected(0); assert(!reads && !maps);
    }
    for (unsigned i = 0; i < 3; ++i) {
        block_init(0, 1); unsigned m = 0x1B00 + (unsigned[]){0,4,12}[i];
        s.setup_valid[m / 128] &= ~(1u << ((m / 4) % 32));
        block_rejected(0); assert(!reads && !maps);
    }
    block_init(0, 1); f.instance[1]--; block_rejected(0); assert(!maps);
    block_init(0, 1); c.physical_bytes = 528 + block_bytes - 1; block_rejected(0); assert(!maps);
    block_init(0, 1); f.instance[0]++; block_rejected(0); assert(!maps);
    block_init(0, 1); f.overflow_map = 1; block_rejected(0); assert(maps == 1);
    block_init(0, 1); set(0, 0, UINT32_MAX - 15); block_rejected(0); assert(!maps);
    block_init(0, 1); block_rejected(4); block_rejected(UINT32_MAX);
    assert(!block_read(NULL, &c, 0, NULL));
    assert(!block_read(&s, NULL, 0, NULL));
    assert(!block_read(&s, &c, 0, NULL));
    /* Capturing the larger texture never admits it to existing draw readers. */
    block_init(0, 1); block_read = h2_dxt23_texture_read; block_rejected(0);
    block_read = h2_dxt1_texture_read; block_rejected(0); assert(!reads && !maps);
}
int main(void)
{
    for (unsigned alpha = 0; alpha < 2; ++alpha)
    for (unsigned unit = 0; unit < 4; ++unit) for (unsigned selector = 1; selector <= 2; ++selector) {
        init(unit, selector);
        uint32_t format = (alpha ? 0x11228u : 0x11E28u) | selector; set(unit, 4, format);
        h2_command_state old = s; h2_kelvin_clear memory = c;
        uint8_t ram[sizeof f.ram]; memcpy(ram, f.ram, sizeof ram);
        h2_linear_texture out;
        assert(h2_linear_texture_read(&s, &c, unit, &out));
        assert(reads == 4 && maps == 1 && out.physical == (selector == 1 ? 528 : 4112));
        assert(out.pixels == f.ram + out.physical && out.bytes == 32);
        assert(out.width == 3 && out.height == 2 && out.pitch == 16 && out.method_format == format);
        assert(!memcmp(&s, &old, sizeof s) && !memcmp(&c, &memory, sizeof c));
        assert(!memcmp(ram, f.ram, sizeof ram));
    }
    const unsigned fields[] = {0, 4, 0xC, 0x10, 0x1C};
    for (unsigned i = 0; i < 5; ++i) {
        init(0, 1); unsigned m = 0x1B00 + fields[i];
        s.setup_valid[m / 128] &= ~(1u << ((m / 4) % 32)); rejected(0); assert(!reads && !maps);
    }
    const uint32_t formats[] = {0x11E28, 0x11E2B, 0x11E2D, 0x11E21, 0x11E19, 0x10629, 0x21E29, 0x111E29};
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
    for (unsigned bc1 = 0; bc1 < 2; ++bc1) {
        block_format = bc1 ? 0x03310C28u : 0x03310E28u;
        block_bytes = bc1 ? 32 : 64;
        block_read = bc1 ? h2_dxt1_texture_read : h2_dxt23_texture_read;
        block_tests();
    }
    snapshot_tests();
    rectangle_tests();
    puts("Linear ARGB/XRGB, exact 8x8 DXT1/DXT23 and stop-only BC2 views: four units, all bounded powers, complete DMA/host spans and read-only rejection passed");
}
