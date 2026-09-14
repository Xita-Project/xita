#include "quad_draw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define PIXELS (640u * 480u)
#define BYTES (PIXELS * 4)
static uint8_t ram[3 * BYTES + 4096], prior_ram[sizeof ram];
static uint32_t result[PIXELS];
static h2_command_state s;
static h2_kelvin_clear c;
static h2_quad_draw q;
static h2_quad_contract reference;
static unsigned calls, maps, reads;
static int fail_read, fail_map, alias_map, overflow_map, backend_failure, backend_alias, readonly_dma;

static int read_word(void *opaque, uint32_t offset, uint32_t *word)
{
    assert(!opaque); ++reads;
    if (fail_read || offset < 0x13000 || offset >= 0x13010 || (offset & 3)) return 0;
    const uint32_t object[] = {readonly_dma ? 0xB002u : 0xB03Du, sizeof ram - 1, 3, 3};
    *word = object[(offset - 0x13000) / 4]; return 1;
}
static void *map_ram(void *opaque, uint32_t address, uint32_t bytes)
{
    assert(!opaque); ++maps;
    if (fail_map == 1 || (fail_map == 2 && address == c.color_offset) ||
        address > sizeof ram || bytes > sizeof ram - address) return NULL;
    if (overflow_map) return (void *)(UINTPTR_MAX - 8);
    if (alias_map) return ram + 4096;
    return ram + address;
}
static const uint32_t *render(void *opaque, const h2_quad_request *r)
{
    assert(!opaque); ++calls;
    assert(r->texture.pixels == ram + 4096 && r->texture.bytes == BYTES);
    assert(r->constants == (const uint32_t (*)[4])(s.constants + 10));
    const float xy[4][2] = {{0,0},{640,0},{640,480},{0,480}};
    for (unsigned i = 0; i < 4; ++i) {
        assert(!memcmp(r->vertices[i].position, xy[i], 8));
        assert(!memcmp(r->vertices[i].texcoord, xy[i], 8));
        assert(r->vertices[i].position[2] == 0 && r->vertices[i].position[3] == 1);
        assert(r->vertices[i].texcoord[2] == 0 && r->vertices[i].texcoord[3] == 1);
        for (unsigned k = 0; k < 4; ++k) assert(r->vertices[i].diffuse[k] == 1);
    }
    if (backend_failure) return NULL;
    if (backend_alias == 1) return (const uint32_t *)(ram + c.color_offset);
    if (backend_alias == 2) return (const uint32_t *)(ram + 4096);
    if (backend_alias == 3) return (const uint32_t *)(UINTPTR_MAX - 8);
    if (backend_alias == 4) return (const uint32_t *)((const uint8_t *)result + 1);
    return result;
}
static void set(unsigned m, uint32_t value)
{
    s.setup[m / 4] = value;
    s.setup_valid[m / 128] |= 1u << ((m / 4) % 32);
}
static void init(void)
{
    memset(&s, 0, sizeof s); memset(&c, 0, sizeof c); memset(&q, 0, sizeof q);
    memset(&reference, 0, sizeof reference);
    for (unsigned i = 0; i < sizeof ram; ++i) ram[i] = (i * 13) ^ (i >> 11);
    for (unsigned i = 0; i < PIXELS; ++i) result[i] = (i * 0xA369u) ^ 0xAB23C400;
    s.bound[0] = 1; s.dma[1] = 0x13000; s.dma_valid = 2;
    s.execution_mode = 6; s.software_valid = 7;
    s.provoking_vertex = s.edge_flag = s.compress_depth = 1;
    s.shadow_slope = 0x7F800000;
    s.program[0][0] = 0x12345678; s.constants[10][2] = 0x87654321; /* synthetic contract */
    set(0x1B00, 4096); set(0x1B04, 0x11E29); set(0x1B0C, 0x40000000);
    set(0x1B10, 2560u << 16); set(0x1B1C, (640u << 16) | 480);
    memcpy(reference.setup, s.setup, sizeof reference.setup);
    memcpy(reference.setup_valid, s.setup_valid, sizeof reference.setup_valid);
    memcpy(reference.constants, s.constants + 10, sizeof reference.constants);
    memcpy(reference.program, s.program, sizeof reference.program);
    c.read_instance = read_word; c.map_physical = map_ram; c.physical_bytes = sizeof ram;
    c.has_color_dma = 1; c.dma_color = 0x13000; c.color_offset = BYTES + 4096;
    c.format = 0x128; c.clip_horizontal = 640u << 16; c.clip_vertical = 480u << 16;
    c.pitch = 0x0A000A00;
    q.contract = &reference; q.render = render;
    calls = maps = reads = 0;
    fail_read = fail_map = alias_map = overflow_map = backend_failure = backend_alias = readonly_dma = 0;
    memcpy(prior_ram, ram, sizeof ram);
}
static void reject(uint8_t sub, uint16_t method, uint32_t value)
{
    h2_quad_draw oldq = q; h2_command_state olds = s; h2_kelvin_clear oldc = c;
    assert(!h2_quad_method(&q, &s, &c, sub, method, value));
    assert(!memcmp(&oldq, &q, sizeof q) && !memcmp(&olds, &s, sizeof s));
    assert(!memcmp(&oldc, &c, sizeof c));
}
static void vertices(void)
{
    const uint32_t xy[4][2] = {{0,0},{0x44200000,0},{0x44200000,0x43F00000},{0,0x43F00000}};
    for (unsigned i = 0; i < 4; ++i) {
        assert(h2_quad_method(&q, &s, &c, 0, 0x1964, UINT32_MAX));
        assert(h2_quad_method(&q, &s, &c, 0, 0x1898, xy[i][0]));
        assert(h2_quad_method(&q, &s, &c, 0, 0x189C, xy[i][1]));
        assert(h2_quad_method(&q, &s, &c, 0, 0x1880, xy[i][0]));
        assert(h2_quad_method(&q, &s, &c, 0, 0x1884, xy[i][1]));
    }
}
int main(void)
{
    init();
    /* Every retained word is checked, including valid bits and program/constant
     * bank ends. These failures precede all mapping and renderer callbacks. */
    for (unsigned i = 0; i < 2048; ++i) if (i != 0x1B00 / 4) {
        s.setup[i] ^= 1; reject(0, 0x17FC, 7); s.setup[i] ^= 1;
    }
    for (unsigned i = 0; i < 64; ++i) {
        s.setup_valid[i] ^= UINT32_MAX; reject(0, 0x17FC, 7); s.setup_valid[i] ^= UINT32_MAX;
    }
    for (unsigned i = 0; i < 21; ++i) for (unsigned k = 0; k < 4; ++k) {
        s.program[i][k] ^= 1; reject(0, 0x17FC, 7); s.program[i][k] ^= 1;
    }
    for (unsigned i = 10; i < 188; ++i) for (unsigned k = 0; k < 4; ++k) {
        s.constants[i][k] ^= 1; reject(0, 0x17FC, 7); s.constants[i][k] ^= 1;
    }
    assert(!maps && !reads && !calls && !memcmp(ram, prior_ram, sizeof ram));
    reject(0, 0x17FC, 0); reject(0, 0x17FC, 5); reject(8, 0x17FC, 7);
    for (unsigned failure = 0; failure < 8; ++failure) {
        init();
        if (failure == 0) fail_read = 1;
        if (failure == 1) fail_map = 1;
        if (failure == 2) alias_map = 1;
        if (failure == 3) overflow_map = 1;
        if (failure == 4) c.color_offset = 4096;
        if (failure == 5) c.color_offset = sizeof ram - BYTES + 1;
        if (failure == 6) fail_map = 2;
        if (failure == 7) readonly_dma = 1;
        reject(0, 0x17FC, 7);
        assert(!calls && !memcmp(ram, prior_ram, sizeof ram));
    }
    init(); assert(h2_quad_method(&q, &s, &c, 0, 0x17FC, 7));
    reject(0, 0x17FC, 7); reject(0, 0x17FC, 0); reject(0, 0x1B00, 4096);
    reject(0, 0x1964, 0xFFFFFFFE); reject(0, 0x1898, 0);
    s.bound[1] = 1; reject(1, 0x1964, UINT32_MAX);
    assert(h2_quad_method(&q, &s, &c, 0, 0x1964, UINT32_MAX));
    reject(0, 0x1898, 0x80000000); reject(0, 0x1898, 0x7FC00000); /* signed zero / NaN outside fixed shape */
    reject(0, 0x1880, 0); q.phase = 0; /* restart this synthetic vertex */
    vertices(); reject(0, 0x1964, UINT32_MAX);
    backend_failure = 1; reject(0, 0x17FC, 0); backend_failure = 0;
    for (backend_alias = 1; backend_alias <= 4; ++backend_alias) reject(0, 0x17FC, 0);
    backend_alias = 0;
    fail_map = 1; unsigned before = calls; reject(0, 0x17FC, 0); assert(calls == before); fail_map = 0;
    s.constants[187][3] ^= 1; reject(0, 0x17FC, 0); s.constants[187][3] ^= 1;
    assert(!memcmp(ram, prior_ram, sizeof ram));
    assert(h2_quad_method(&q, &s, &c, 0, 0x17FC, 0));
    assert(!q.active && q.completed == 1 && s.vertex4ub[9] == UINT32_MAX);
    for (unsigned i = 0; i < sizeof ram; ++i) {
        uint8_t expected = prior_ram[i];
        if (i >= c.color_offset && i < c.color_offset + BYTES) {
            unsigned relative = i - c.color_offset;
            if ((relative & 3) != 3) expected = result[relative / 4] >> ((relative & 3) * 8);
        }
        assert(ram[i] == expected);
    }
    puts("Quad draw: strict pipeline/vertices, remapping/alias failures, staged RGB and all destination alpha bytes passed");
}
