#include "command_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint32_t instance[0x5000 / 4], ram[1024];
    uint32_t failed_instance;
    int fail_map, misalign_map;
} fixture;
static fixture f;
static h2_command_state s;
static h2_kelvin_clear c;
static int read_instance(void *opaque, uint32_t offset, uint32_t *word)
{
    fixture *v = opaque;
    if ((offset & 3) || offset < 0x10000 || offset >= 0x15000 || offset == v->failed_instance) return 0;
    *word = v->instance[(offset - 0x10000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t address, uint32_t bytes)
{
    fixture *v = opaque;
    if (v->fail_map || address > sizeof v->ram || bytes > sizeof v->ram - address) return NULL;
    return (uint8_t *)v->ram + address + v->misalign_map;
}
static void graph(unsigned handle, unsigned klass)
{
    f.instance[handle * 2] = handle;
    f.instance[handle * 2 + 1] = 0x80010000 | ((0x12000 + handle * 16) >> 4);
    f.instance[(0x2000 + handle * 16) / 4] = klass;
}
static void dma(unsigned handle, unsigned klass, uint32_t base, uint32_t limit)
{
    f.instance[handle * 2] = handle;
    f.instance[handle * 2 + 1] = 0x80000000 | ((0x13000 + handle * 16) >> 4);
    unsigned i = (0x3000 + handle * 16) / 4;
    f.instance[i] = 0xB000 | klass | ((base & 0xFFF) << 20);
    f.instance[i + 1] = limit;
    f.instance[i + 2] = f.instance[i + 3] = (base & ~0xFFFu) | 3;
}
static int emit(unsigned sub, unsigned method, uint32_t value)
{ return h2_command_method(&s, &c, sub, method, value, 0x100); }
static void reject(unsigned sub, unsigned method, uint32_t value)
{
    h2_command_state old = s;
    h2_kelvin_clear clear = c;
    uint32_t ram[1024]; memcpy(ram, f.ram, sizeof ram);
    assert(!emit(sub, method, value));
    assert(!memcmp(&s, &old, sizeof s) && !memcmp(&c, &clear, sizeof c));
    assert(!memcmp(ram, f.ram, sizeof ram));
}
int main(void)
{
    assert(h2_kelvin_clear_init(&c, read_instance, map_ram, &f, sizeof f.ram, 0));
    graph(1, 0x97); graph(2, 0x39); graph(3, 0x9F); graph(4, 0x62); graph(5, 0x44);
    graph(6, 0x30); graph(7, 0x97); graph(8, 0x99);
    dma(10, 3, 0x800, 7); dma(11, 2, 0x800, 7);
    reject(0, 0x1D70, 1); reject(8, 0, 1); reject(0, 0, 8);
    f.failed_instance = 0x1201C; reject(0, 0, 1); f.failed_instance = 0;
    for (unsigned i = 0; i < 5; ++i) assert(emit(i, 0, i + 1));
    assert(emit(7, 0, 1) && s.bound[7] == 1);
    reject(0, 0, 7); /* second object of same class */
    reject(0, 0x180, 6); reject(0, 0x18C, 10); reject(0, 0x1A5, 10);
    assert(emit(1, 0x180, 10) && s.m2mf_notifier == 0x130A0);
    reject(1, 0x328, 1); /* M2MF copy remains unsupported */
    assert(emit(2, 0x2FC, 3)); reject(2, 0x2FC, 2); reject(2, 0x308, 0x10001);
    for (unsigned method = 0x184; method <= 0x198; method += 4) assert(emit(2, method, 6));
    assert(emit(2, 0x19C, 4)); reject(2, 0x194, 4); reject(2, 0x19C, 6);
    assert(emit(3, 0x184, 11) && emit(3, 0x188, 10)); reject(3, 0x304, 16);
    assert(emit(4, 0x310, 0x12345678) && s.pattern_color == 0x12345678);
    reject(4, 0x314, 0);
    assert(emit(0, 0, 5)); reject(0, 0x1D90, 42); /* old Kelvin binding must not leak */
    assert(emit(0, 0, 1));
    for (unsigned method = 0x180; method <= 0x1A8; method += 4)
        if (method != 0x18C) assert(emit(0, method, 10));
    assert(c.has_color_dma && c.has_zeta_dma && s.dma_valid == 0x7F7);
    reject(0, 0x9FC, 2); reject(0, 0x16BC, 2); reject(0, 0x1D80, 2);
    assert(emit(0, 0x9FC, 1) && emit(0, 0x16BC, 1) && emit(0, 0x1D80, 1));
    assert(s.provoking_vertex == 1 && s.edge_flag == 1 && s.compress_depth == 1);
    assert(emit(0, 0x120, 6) && emit(0, 0x124, 7) && emit(0, 0x128, 0));
    reject(0, 0x120, 8); reject(0, 0x124, 8); reject(0, 0x128, 8);
    reject(0, 0x12C, 0); reject(0, 0x130, 0); /* no fake flip or vblank */
    assert(emit(0, 0x1E78, 0x01234000)); reject(0, 0x1E78, 1);
    assert(emit(0, 0x1E68, 0x7FC12345) && s.shadow_slope == 0x7FC12345);
    assert(emit(0, 0xA54, 0x80000000) && s.constants[0x38][1] == 0x80000000);
    assert(emit(0, 0x9DC, 0x7FC54321) && s.constants[0x39][3] == 0x7FC54321);
    for (unsigned i = 0; i < 64; ++i) assert(emit(0, 0x840 + i * 4, 0xABC00000 + i));
    assert(s.constants[0x40][0] == 0xABC00000 && s.constants[0x5B][3] == 0xABC0003F);
    assert(emit(0, 0x1EA4, 0x38) && emit(0, 0xB84, 0x7FC00001));
    assert(s.constants[0x38][1] == 0x7FC00001); /* same storage as eye position */
    assert(emit(0, 0x1EA4, 190));
    for (unsigned i = 0; i < 8; ++i) assert(emit(0, 0xB80 + i * 4, i + 0x80000000));
    assert(s.constant_load == 192 && s.constants[190][3] == 0x80000003 && s.constants[191][3] == 0x80000007);
    reject(0, 0xBA0, 0); reject(0, 0x1EA4, 192); reject(0, 0xC00, 0);
    assert(emit(0, 0x1D6C, 4)); reject(0, 0x1D6C, 3);
    f.ram[0x800 / 4] = 0xAABBCCDD;
    assert(emit(0, 0x1D70, 0x11223344));
    assert(f.ram[0x804 / 4] == 0x11223344 && f.ram[0x800 / 4] == 0xAABBCCDD);
    assert(s.semaphore_releases == 1 && s.last_semaphore_address == 0x804 && s.last_semaphore_value == 0x11223344);
    assert(emit(0, 0x1D6C, 8)); reject(0, 0x1D70, 9); /* full dword must fit inclusive limit */
    assert(emit(0, 0x1D6C, 4) && emit(0, 0x1A4, 11)); reject(0, 0x1D70, 9); /* read-only DMA */
    assert(emit(0, 0x1A4, 10));
    f.fail_map = 1; reject(0, 0x1D70, 9); f.fail_map = 0;
    f.misalign_map = 1; reject(0, 0x1D70, 9); f.misalign_map = 0;
    f.failed_instance = 0x130A8; reject(0, 0x1D70, 9); f.failed_instance = 0;
    dma(10, 3, 0xFFF, 7); reject(0, 0x1D70, 9); /* physical end/alignment */
    reject(0, 0x1800, 3); reject(0, 0x1810, 0); /* draws stay fatal */
    assert(emit(0, 0x1E9C, 134));
    for (unsigned i = 0; i < 8; ++i) assert(emit(0, 0xB00 + i * 4, 0xAABB0000 + i));
    assert(s.program[134][3] == 0xAABB0003 && s.program[135][3] == 0xAABB0007 && s.program_load == 136);
    reject(0, 0xB20, 1); reject(0, 0x1E9C, 136); reject(0, 0x1EA0, 136);
    assert(emit(0, 0x1EA0, 135) && s.program_start == 135);
    assert(emit(0, 0x1E94, 6) && emit(0, 0x1E98, 0));
    reject(0, 0x1E94, 7); reject(0, 0x1E94, 8); reject(0, 0x1E98, 2);
    reject(0, 0x1E90, 0); /* executing a program remains unsupported */
    assert(emit(0, 0x194C, 0x11223344) && s.vertex4ub[3] == 0x11223344);
    reject(0, 0x1940, 1); /* position attribute would emit a vertex */
    assert(emit(0, 0xA20, 0x7FC12345) && s.constants[0x3B][0] == 0x7FC12345);
    assert(emit(0, 0xAF8, 0x80000000) && s.constants[0x3A][2] == 0x80000000);
    assert(emit(0, 0xA7C, 0xDEADBEEF) && s.setup[0xA7C / 4] == 0xDEADBEEF);
    assert(emit(0, 0x1BFC, 0x80000000) && s.setup[0x1BFC / 4] == 0x80000000);
    reject(0, 0x1B00, 1); reject(0, 0x1B04, 1); /* resources need another implementation */
    assert(emit(0, 0x300, 1) && emit(0, 0x328, 6));
    reject(0, 0x300, 2); reject(0, 0x328, 7); reject(0, 0x33C, 0x208);
    reject(0, 0x358, 2); reject(0, 0x370, 0x1234); reject(0, 0x380, 0x200);
    reject(0, 0x3C0, 0x1234); reject(0, 0x2C0, 0xF0000000);
    assert(emit(0, 0x100, 0) && emit(0, 0x110, 0));
    reject(0, 0x100, 9); reject(0, 0x110, 1);
    assert(emit(0, 0x100, 0x28) && s.dxt1_noise == 1 && s.software_valid == 1);
    assert(emit(0, 0x100, 8) && !s.dxt1_noise && s.software_updates == 2);
    reject(0, 0x100, 0x48); reject(0, 0x100, 0x29);
    for (unsigned selector = 1; selector < 32; ++selector)
        if (selector != 8 && selector != 9) reject(0, 0x100, selector);
    assert(emit(0, 0x1D8C, 0x400094) && emit(0, 0x1D90, 0));
    h2_kelvin_clear parameters = c;
    assert(emit(0, 0x100, 9) && s.software_valid == 3 && s.software_updates == 3);
    assert(!memcmp(&parameters, &c, sizeof c)); /* original leaves parameters intact */
    assert(emit(0, 0x1D8C, 0x400B80) && emit(0, 0x100, 9));
    assert(s.software_valid == 7 && !s.zcull_debug5 && !s.rop_control && s.software_updates == 4);
    assert(emit(0, 0x1D90, 1)); reject(0, 0x100, 9);
    assert(emit(0, 0x1D90, 0) && emit(0, 0x1D8C, 0x400B84)); reject(0, 0x100, 9);
    assert(emit(0, 0x1D8C, 0xFFFFFFFF)); reject(0, 0x100, 9);
    /* The draw-state representation must not bypass clear-sensitive settings. */
    dma(10, 3, 0x800, 7);
    assert(emit(0, 0x200, 2u << 16) && emit(0, 0x204, 1u << 16));
    assert(emit(0, 0x208, 0x128) && emit(0, 0x20C, 0x00080008));
    assert(emit(0, 0x1D98, 1u << 16) && emit(0, 0x1D9C, 0));
    assert(emit(0, 0x2B4, 0) && emit(0, 0x2C0, 2u << 16) && emit(0, 0x2E0, 1u << 16));
    assert(emit(0, 0x290, 0x100001) && emit(0, 0x1D7C, 0xFFFF0000));
    assert(emit(0, 0x1D8C, 0x11223344) && emit(0, 0x1D94, 3));
    assert(f.ram[0x800 / 4] == 0x11223344 && f.ram[0x804 / 4] == 0x11223344);
    assert(c.completed_clears == 1 && c.written_pixels == 2);
    assert(emit(0, 0x290, 0x101001)); reject(0, 0x1D94, 3);
    assert(emit(0, 0x290, 0x100001) && emit(0, 0x1D7C, 1)); reject(0, 0x1D94, 3);
    assert(emit(0, 0x1D7C, 0) && emit(0, 0x2B4, 1)); reject(0, 0x1D94, 3);
    assert(emit(0, 0x2B4, 0) && emit(0, 0x2C0, 1u << 16)); reject(0, 0x1D94, 3);
    assert(emit(0, 0x2C0, 2u << 16) && emit(0, 0x310, 1)); reject(0, 0x1D94, 0xF0);
    assert(emit(0, 0x310, 0) && emit(0, 0x1D90, 0xAABBCCDD) && emit(0, 0x1D94, 0xF0));
    assert(f.ram[0x800 / 4] == 0xAABBCCDD && f.ram[0x804 / 4] == 0xAABBCCDD);
    assert(c.completed_clears == 2 && c.written_pixels == 4);
    /* Flip write wraps modulo the configured queue size; a stall may pass only
     * when a read completion has already made the counters differ. */
    for (unsigned modulo = 2; modulo <= 7; ++modulo) {
        assert(emit(0, 0x128, modulo));
        for (unsigned write = 0; write < modulo; ++write) {
            assert(emit(0, 0x120, write) && emit(0, 0x124, write));
            reject(0, 0x130, 0); reject(0, 0x12C, 1);
            assert(emit(0, 0x12C, 0) && s.flip_write == (write + 1) % modulo);
            assert(emit(0, 0x130, 0)); reject(0, 0x130, 1);
        }
    }
    assert(emit(0, 0x128, 0)); reject(0, 0x12C, 0); reject(0, 0x130, 0);
    assert(emit(0, 0x128, 1)); reject(0, 0x12C, 0); reject(0, 0x130, 0);
    assert(emit(0, 0x128, 2) && emit(0, 0x120, 2)); reject(0, 0x12C, 0); reject(0, 0x130, 0);
    puts("Command state: class bindings, context aliases, exact state, semaphore writes/bounds and rejection isolation pass.");
    return 0;
}
