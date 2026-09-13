#include "host_channel_runtime.h"
#include "gpu_bus.h"
#include "scanout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
static jmp_buf fault;
static uint32_t last_ip, last_address, calls;
static unsigned software_calls;
static int timed_test;
static unsigned vblank_calls, event_calls, wait_calls;
static int vblank_result;
static uint32_t vblank_delta = 1;
static unsigned present_calls;
static int present_result;
static int blank_result, blank_value;
static unsigned blank_calls;
int h2_platform_blank(int blank)
{ ++blank_calls; blank_value = blank; return blank_result; }
int h2_platform_present(const uint8_t *pixels, size_t bytes, const uint8_t *rgb_gamma, uint32_t *vcount)
{
    assert(pixels == g_xram + 0x300000 && bytes == H2_SCANOUT_BYTES);
    for (unsigned i = 0; i < 768; ++i) assert(rgb_gamma[i] == i / 3);
    ++present_calls; *vcount = 102; return present_result;
}
int h2_platform_wait_vblank(uint32_t *before, uint32_t *after)
{ ++vblank_calls; *before = 100; *after = 100 + vblank_delta; return vblank_result; }
void xk_KeSetEvent(xctx *c)
{
    assert(X_ARG(0) == 0x406D9C && X_ARG(1) == 1 && !X_ARG(2));
    ++event_calls; X_M32(X_ARG(0) + 4) = 1;
    uint32_t stack = c->r[4]; memset(c, 0x5A, sizeof *c); c->r[4] = stack; X_RET(3);
}
void __real_xk_KeWaitForSingleObject(xctx *c)
{
    ++wait_calls;
    if (X_ARG(0) == 0x406D9C) assert(X_M32(X_ARG(0) + 4) == 1);
    c->r[0] = 0; X_RET(5);
}
uint32_t h2_instance_bytes(void) { return 0x5000; }
uint32_t xk_mem_arena_size(void) { return 0x4001000; }
uint64_t h2_graphics_time_us(void) { return 0; }
void xv_logf(const char *format, ...) { (void)format; }
void h2_graphics_stop(xctx *c, uint32_t ip, uint32_t address, uint32_t value, int write, int reason)
{ (void)c; (void)value; (void)write; (void)reason; last_ip = ip; last_address = address; longjmp(fault, 1); }
/* Synthetic helper contracts exercise register/stack bridging. The native run
 * separately executes the pinned original helper bodies. */
void f_003FE165(xctx *c)
{ assert(c->r[0] == 0x406C08); ++calls; X_M32(c->r[0]) = 0xFD000000; c->r[0] = 1; X_RET(0); }
void f_003FE190(xctx *c)
{ assert(c->r[0] == 0x406C08); ++calls; c->r[0] = 1; X_RET(0); }
void f_00401C33(xctx *c)
{
    assert(c->r[6] == 0x406C08); ++calls;
    X_M32(c->r[6] + 0x154) = h2_bus_read32(c, 0, 0xFD000200);
    X_M32(c->r[6] + 0x158) = h2_bus_read32(c, 0, 0xFD000140);
    h2_bus_write32(c, 0, 0xFD000200, UINT32_MAX);
    X_RET(0);
}
void f_00401D96(xctx *c)
{
    assert(c->r[6] == 0x406C08); ++calls;
    X_M32(c->r[6] + 0x130) = 0x710000;
    X_M32(c->r[6] + 0x128) = 0x711000;
    X_M32(c->r[6] + 0x140) = 0x110A;
    X_M32(c->r[6] + 0x14C) = 3;
    X_M32(c->r[6] + 0x150) = 0x01039000;
    X_RET(0);
}
void f_003FF240(xctx *c)
{
    assert(c->r[0] == ((0x300000u | (timed_test ? 1 : 8)) << 5 | 1) && c->r[1] == 0x406C08);
    ++software_calls;
    xv_cur_fn = 0x3FF240;
    if (timed_test) {
        X_M32(0x406C08 + 0x174) = X_M32(0x406C08 + 0x178) = 1;
        X_M32(0x406C08 + 0x17C) = 0x300000;
        X_M32(0x406C08 + 0x1C4) = 2;
        X_M32(0x406C08 + 0x1C8) = X_M32(0x406C08 + 0x1CC) = 1;
        uint32_t stack = c->r[4]; memset(c, 0xA5, sizeof *c); c->r[4] = stack; X_RET(0);
        return;
    }
    assert(h2_bus_read32(c, 0x3FE5E5, 0xFD003240) != 0);
    h2_bus_write8(c, 0x3FF5CE, 0xFD6813C8, 0);
    for (unsigned i = 0; i < 768; ++i) h2_bus_write8(c, 0x3FF5F1, 0xFD6813C9, i / 3);
    assert(h2_bus_read32(c, 0x3FED43, 0xFD40071C) == 0);
    h2_bus_write32(c, 0x3FED4C, 0xFD40071C, 2);
    X_M32(0x408650) = X_M32(0x406C08 + 0x17C) = 0x300000;
    X_M32(0x406C08 + 0x1BC) = X_M32(0x406C08 + 0x1CC) = 1;
    X_M32(0x406C08 + 0x7DC) = 0;
    uint32_t stack = c->r[4];
    memset(c, 0xA5, sizeof *c); /* no interrupt CPU/FP/control state may leak */
    c->r[4] = stack; X_RET(0);
}
static void instance_word(xctx *c, uint32_t offset, uint32_t word)
{ h2_bus_write32(c, 0, 0xFD700000 + offset, word); }
static void packet(uint32_t *put, uint16_t method, uint32_t value)
{ X_M32(0x80000000u + *put) = 0x40000u | method; X_M32(0x80000004u + *put) = value; *put += 8; }
int main(int argc, char **argv)
{
    assert(argc == 1 || (argc == 2 && !strcmp(argv[1], "--timed")));
    timed_test = argc == 2;
    g_xram = calloc(1, 0x4000000); g_img_base = g_xram;
    g_xpt = calloc(1 << 20, 4); assert(g_xram && g_xpt);
    for (unsigned page = 0; page < 0x4000; ++page) {
        g_xpt[page] = page * 4096; g_xpt[0x80000 + page] = page * 4096;
    }
    static xctx c; c.r[4] = 0x600000;
    if (!setjmp(fault)) { h2_host_miniport_init(&c); assert(0); }
    assert(last_ip == 0x3FE005 && calls == 0);
    c.r[0] = 0x406C08; X_M32(0x407488) = 0x404FE0;
    for (unsigned i = 1; i < 8; ++i) if (i != 4) c.r[i] = 0xABC00000 + i;
    h2_gpu_bus_reset(0x4000000); h2_host_miniport_init(&c);
    assert(calls == 4 && c.r[0] == 1 && c.r[4] == 0x600004);
    for (unsigned i = 1; i < 8; ++i) if (i != 4) assert(c.r[i] == 0xABC00000 + i);
    assert(X_M32(0x406C08 + 0x1AC) == 0x406C08 + 0x1AC);
    assert(X_M32(0x406C08 + 0x138) == 0 && X_M32(0x406C08 + 0xA0) == 1);
    for (unsigned table = 0; table < 6; ++table)
        for (unsigned i = 0; i < 256; ++i) assert(X_M8(0x406C08 + 0x1DC + table * 256 + i) == (uint8_t)i);
    c.r[4] = 0x600000; c.r[0] = 0x406C08;
    if (!setjmp(fault)) { h2_host_miniport_init(&c); assert(0); }
    assert(calls == 4);
    c.r[0] = 0xABCDEF01;
    X_M32(c.r[4] + 4) = 0; X_M32(c.r[4] + 8) = 6;
    X_M32(c.r[4] + 12) = 0; X_M32(c.r[4] + 16) = 0x610000;
    __wrap_xk_AvSendTVEncoderOption(&c);
    assert(X_M32(0x610000) == 0x00480104 && c.r[0] == 0xABCDEF01 && c.r[4] == 0x600014);
    c.r[4] = 0x600000; X_M32(c.r[4] + 8) = 7;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    assert(X_M32(0x610000) == 0x00480104 && c.r[4] == 0x600000);
    if (!setjmp(fault)) { __wrap_xk_AvSetDisplayMode(&c); assert(0); }
    assert(c.r[4] == 0x600000);
    assert(!h2_host_av_configuration().has_flicker && !h2_host_av_configuration().has_luma);
    X_M32(c.r[4] + 4) = 0xFD000000; X_M32(c.r[4] + 8) = 11;
    X_M32(c.r[4] + 12) = 5; X_M32(c.r[4] + 16) = 0;
    __wrap_xk_AvSendTVEncoderOption(&c);
    assert(h2_host_av_configuration().has_flicker && h2_host_av_configuration().flicker_filter == 5);
    assert(c.r[0] == 0xABCDEF01 && c.r[4] == 0x600014);
    c.r[4] = 0x600000; X_M32(c.r[4] + 12) = 4;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    assert(h2_host_av_configuration().flicker_filter == 5 && c.r[4] == 0x600000);
    X_M32(c.r[4] + 8) = 14; X_M32(c.r[4] + 12) = 0; X_M32(c.r[4] + 16) = 0x610000;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    assert(!h2_host_av_configuration().has_luma && X_M32(0x610000) == 0x00480104);
    X_M32(c.r[4] + 16) = 0; __wrap_xk_AvSendTVEncoderOption(&c);
    assert(h2_host_av_configuration().has_luma && h2_host_av_configuration().luma_filter == 0);
    assert(c.r[4] == 0x600014 && c.r[0] == 0xABCDEF01);
    c.r[4] = 0x600000; X_M32(c.r[4] + 4) = 0xDEADBEEF;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    if (!setjmp(fault)) { __wrap_xk_AvSetDisplayMode(&c); assert(0); }
    assert(c.r[4] == 0x600000); /* deferred settings still cannot apply a display */
    X_M32(0x404FE0 + 0x24) = 0x80010000; X_M32(0x404FE0 + 0x28) = 0x80011000;
    X_M32(0x406C08 + 0x10C) = 0x111D; X_M32(0x61000C) = 0x1112;
    X_M32(0x406C08 + 0x160) = 0x149C;
    instance_word(&c, 0x11120, 0xB002); instance_word(&c, 0x11124, 0x3FFFFFF);
    instance_word(&c, 0x11128, 3); instance_word(&c, 0x1112C, 3);
    c.r[0] = 8; c.r[2] = 0x406C08;
    X_M32(c.r[4] + 4) = 128; X_M32(c.r[4] + 8) = 128; X_M32(c.r[4] + 12) = 0x610000;
    g_xpt[0x610] = 0x4000000; /* descriptor on the trash page must stop */
    if (!setjmp(fault)) { h2_host_channel_configure(&c); assert(0); }
    assert(last_address == 0x610000 && c.r[4] == 0x600000);
    g_xpt[0x610] = 0x610000;
    g_xpt[0x80010] = 0x4000000;
    if (!setjmp(fault)) { h2_host_channel_configure(&c); assert(0); }
    assert(X_M32(0x406C08 + 0x138) == 0);
    g_xpt[0x80010] = 0x10000;
    g_xpt[0x83FEF] = 0x4000000;
    if (!setjmp(fault)) { h2_host_channel_configure(&c); assert(0); }
    assert(X_M32(0x406C08 + 0x138) == 0);
    g_xpt[0x83FEF] = 0x3FEF000;
    h2_host_channel_configure(&c);
    assert(c.r[4] == 0x600010 && c.r[3] == 0xABC00003 && c.r[5] == 0xABC00005 && c.r[6] == 0xABC00006 && c.r[7] == 0xABC00007);
    assert(X_M32(0x406C08 + 0x138) == 0x111D && X_M32(0x406C08 + 0x104) == 1 && X_M32(0x406C08 + 0x108) == 1);
    assert(h2_bus_read32(&c, 0, 0xFD711014) == 0x86078);
    assert(h2_bus_read32(&c, 0, 0xFD7110A0) == 0x111D);
    assert(h2_bus_read32(&c, 0, 0xFD800044) == 0);
    X_M32(0x80000000) = 0x10001;
    h2_bus_write32(&c, 0x1234, 0xFD800040, 0x10000);
    assert(h2_bus_read32(&c, 0, 0xFD800044) == 0x10000);
    assert(h2_bus_read32(&c, 0, 0xFD400700) == 0);
    if (!setjmp(fault)) { h2_bus_read16(&c, 0x1234, 0xFD800044); assert(0); }
    assert(last_ip == 0x1234 && last_address == 0xFD800044);
    if (!setjmp(fault)) { h2_bus_read32(&c, 0x1234, 0xFD008088); assert(0); }
    assert(last_address == 0xFD008088);
    c.r[4] = 0x600000; c.r[0] = 0;
    X_M32(c.r[4] + 4) = 0x406C08; X_M32(c.r[4] + 8) = 0x20000;
    X_M32(c.r[4] + 12) = 0x4000; X_M32(c.r[4] + 16) = 64;
    X_M32(c.r[4] + 20) = 0x80000000; X_M32(c.r[4] + 24) = X_M32(c.r[4] + 28) = 0;
    if (!setjmp(fault)) { h2_host_tile_configure(&c); assert(0); }
    assert(last_ip == 0x3FE67F && c.r[4] == 0x600000);
    X_M32(c.r[4] + 20) = 0;
    g_xpt[0x80023] = 0x4000000; /* last region page invalid, not just the first */
    if (!setjmp(fault)) { h2_host_tile_configure(&c); assert(0); }
    assert(c.r[4] == 0x600000);
    g_xpt[0x80023] = 0x23000;
    h2_host_tile_configure(&c);
    assert(c.r[0] == 1 && c.r[4] == 0x600020 && c.r[3] == 0xABC00003 && c.r[6] == 0xABC00006 && c.r[7] == 0xABC00007);
    c.r[4] = 0x600000; c.r[3] = 0;
    X_M32(c.r[4] + 4) = 0x406C08; X_M32(c.r[4] + 8) = 1;
    h2_host_tile_remove(&c);
    assert(c.r[0] == 1 && c.r[3] == 0 && c.r[4] == 0x60000C);
    c.r[4] = 0x600000; c.r[0] = 0;
    X_M32(c.r[4] + 8) = 0x20000;
    h2_host_tile_configure(&c); /* the following real clear is inside this tile */
    uint32_t good_stack = c.r[4];
    c.r[4] = 0x6FFFF0; g_xpt[0x700] = 0x4000000;
    if (!setjmp(fault)) { h2_host_tile_configure(&c); assert(0); }
    assert(last_address == 0x6FFFF0);
    c.r[4] = good_stack; g_xpt[0x700] = 0x700000;
    c.r[4] = 0x600000; c.r[0] = 1;
    X_M32(c.r[4] + 8) = 0x24000; X_M32(c.r[4] + 20) = 0x84000001;
    h2_host_tile_configure(&c);
    assert(c.r[0] == 1 && c.r[4] == 0x600020 && c.r[5] == 0xABC00005);
    instance_word(&c, 0x10068, 13); instance_word(&c, 0x1006C, 0x800114A0);
    instance_word(&c, 0x14A00, 0x97);
    instance_word(&c, 0x10018, 3); instance_word(&c, 0x1001C, 0x80001113);
    instance_word(&c, 0x11130, 0xB003); instance_word(&c, 0x11134, 71);
    instance_word(&c, 0x11138, 0x20003); instance_word(&c, 0x1113C, 0x20003);
    instance_word(&c, 0x10020, 4); instance_word(&c, 0x10024, 0x80001114);
    instance_word(&c, 0x11140, 0xB003); instance_word(&c, 0x11144, 71);
    instance_word(&c, 0x11148, 0x24003); instance_word(&c, 0x1114C, 0x24003);
    static uint32_t put; put = 0x10000;
    packet(&put, 0, 13); packet(&put, 0x194, 3); packet(&put, 0x198, 4);
    packet(&put, 0x200, 2 << 16); packet(&put, 0x204, 2 << 16);
    packet(&put, 0x208, 0x128); packet(&put, 0x20C, 64 | (64 << 16)); packet(&put, 0x210, 0);
    packet(&put, 0x1D98, 1 << 16); packet(&put, 0x1D9C, 1 << 16);
    packet(&put, 0x1D90, 0x12345678); packet(&put, 0x1D8C, 0xA1B2C3D4); packet(&put, 0x1D94, 0xF3);
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    for (unsigned i = 0; i < 4; ++i) {
        unsigned offset = (i / 2) * 64 + (i % 2) * 4;
        assert(X_M32(0x80020000 + offset) == 0x12345678);
        assert(X_M32(0x80024000 + offset) == 0xA1B2C3D4);
    }
    assert(X_M32(0x80024008) == 0 && X_M32(0x8002403C) == 0);
    packet(&put, 0x1D8C, 0x11223344); packet(&put, 0x1D94, 1);
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    assert(X_M32(0x80024000) == 0x112233D4);
    packet(&put, 0x1D8C, 0x55667788); packet(&put, 0x1D94, 2);
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    assert(X_M32(0x80024000) == 0x11223388);
    packet(&put, 0x194, 4); /* a canonical depth region cannot be a color target */
    packet(&put, 0x1D94, 0xF0);
    if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
    assert(h2_bus_read32(&c, 0, 0xFD800044) == put - 4 && X_M32(0x80024000) == 0x11223388);
    X_M32(0x80000000u + put - 4) = 0; /* replace the rejected request with a clear no-op */
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    packet(&put, 0x194, 3);
    packet(&put, 0x1D90, 0x87654321); packet(&put, 0x1D94, 0xF0);
    g_xpt[0x80020] = 0x30000;
    if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
    assert(h2_bus_read32(&c, 0, 0xFD800044) == put - 4);
    for (unsigned i = 0; i < 4; ++i)
        assert(*(uint32_t *)(g_xram + 0x20000 + (i / 2) * 64 + (i % 2) * 4) == 0x12345678);
    g_xpt[0x80020] = 0x20000;
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    c.r[4] = 0x600000; c.r[0] = 0;
    X_M32(c.r[4] + 4) = 0x406C08; X_M32(c.r[4] + 8) = 0x300000;
    X_M32(c.r[4] + 12) = 0x12C000; X_M32(c.r[4] + 16) = 2560;
    X_M32(c.r[4] + 20) = X_M32(c.r[4] + 24) = X_M32(c.r[4] + 28) = 0;
    h2_host_tile_configure(&c);
    packet(&put, 0x200, 640u << 16); packet(&put, 0x204, 480u << 16);
    packet(&put, 0x20C, 0x0A000A00); packet(&put, 0x120, 0);
    packet(&put, 0x124, 1); packet(&put, 0x128, 2);
    packet(&put, 0x100, ((0x300000u | (timed_test ? 1 : 8)) << 5) | 1);
    X_M32(0x406C08 + 0x1B8) = 1; X_M32(0x406C08 + 0x7DC) = 1;
    xctx interrupted = c;
    xv_cur_fn = 0x1234;
    g_xpt[0x80000 + (0x42B000 >> 12)] = 0x4000000; /* last framebuffer page */
    if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
    assert(!software_calls && !memcmp(&c, &interrupted, sizeof c) && X_M32(0x406C08 + 0x7DC) == 1);
    g_xpt[0x8042B] = 0x42B000;
    X_M32(0x406C08 + 0x18C) = 0x1000; /* client callback remains unsupported */
    if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
    assert(!software_calls && !memcmp(&c, &interrupted, sizeof c));
    X_M32(0x406C08 + 0x18C) = 0;
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    assert(software_calls == 1 && !memcmp(&c, &interrupted, sizeof c) && xv_cur_fn == 0x1234);
    if (timed_test) {
        assert(X_M32(0x408650) == 0 && X_M32(0x406C08 + 0x1BC) == 0);
        assert(X_M32(0x406C08 + 0x174) == 1 && X_M32(0x406C08 + 0x178) == 1);
        assert(X_M32(0x406C08 + 0x17C) == 0x300000 && X_M32(0x406C08 + 0x1CC) == 1);
        packet(&put, 0x12C, 0); packet(&put, 0x130, 0);
        if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
        assert(h2_bus_read32(&c, 0, 0xFD800044) == put - 4);
        assert(!vblank_calls && !event_calls && !present_calls);
        assert(X_M32(0x406C08 + 0x1C0) == 0 && X_M32(0x406C08 + 0x7DC) == 1);
        assert(!memcmp(&c, &interrupted, sizeof c));
        puts("Host-channel timed initialization: original queue contract and strict uncompleted flip stall pass.");
        free(g_xpt); free(g_xram); return 0;
    }
    assert(X_M32(0x408650) == 0x300000 && X_M32(0x406C08 + 0x1BC) == 1);
    packet(&put, 0x12C, 0); packet(&put, 0x130, 0);
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    assert(h2_bus_read32(&c, 0, 0xFD800044) == put);
    c.r[4] = 0x600000; X_M32(c.r[4]) = 0x3F9BF7;
    X_M32(c.r[4] + 4) = 0x406D9C; X_M32(c.r[4] + 8) = 6;
    X_M32(c.r[4] + 12) = 1; X_M32(c.r[4] + 16) = X_M32(c.r[4] + 20) = 0;
    X_M32(0x406DA0) = 0; X_M32(0x406C10) = 0x88070701;
    interrupted = c; vblank_result = -1;
    if (!setjmp(fault)) { __wrap_xk_KeWaitForSingleObject(&c); assert(0); }
    assert(vblank_calls == 1 && !event_calls && !wait_calls && !memcmp(&c, &interrupted, sizeof c));
    assert(!X_M32(0x406DA0) && !X_M32(0x406DC8));
    vblank_result = 0; vblank_delta = 0;
    if (!setjmp(fault)) { __wrap_xk_KeWaitForSingleObject(&c); assert(0); }
    assert(vblank_calls == 2 && !event_calls && !wait_calls);
    vblank_delta = 1; X_M32(0x406C08 + 0x190) = 0x1000;
    if (!setjmp(fault)) { __wrap_xk_KeWaitForSingleObject(&c); assert(0); }
    assert(vblank_calls == 2 && !event_calls); X_M32(0x406C08 + 0x190) = 0;
    __wrap_xk_KeWaitForSingleObject(&c);
    interrupted.r[0] = 0; interrupted.r[4] += 24;
    assert(vblank_calls == 3 && event_calls == 1 && wait_calls == 1 && !memcmp(&c, &interrupted, sizeof c));
    assert(X_M32(0x406DC8) == 1 && X_M32(0x406DA0) == 1);
    c.r[4] = 0x600000; X_M32(c.r[4] + 4) = 0x7000;
    __wrap_xk_KeWaitForSingleObject(&c);
    assert(wait_calls == 2 && vblank_calls == 3); /* unrelated wait passes through */
    c.r[4] = 0x600000; X_M32(c.r[4]) = 0x3F9C0F;
    X_M32(c.r[4] + 4) = 0xFD000000; X_M32(c.r[4] + 8) = 0;
    X_M32(c.r[4] + 12) = 0x88070701; X_M32(c.r[4] + 16) = 0x12;
    X_M32(c.r[4] + 20) = 2560; X_M32(c.r[4] + 24) = 0x300000;
    interrupted = c;
    g_xpt[0x8042B] = 0x4000000;
    if (!setjmp(fault)) { __wrap_xk_AvSetDisplayMode(&c); assert(0); }
    assert(!present_calls && !memcmp(&c, &interrupted, sizeof c));
    g_xpt[0x8042B] = 0x42B000;
    X_M32(c.r[4] + 16) = 6;
    if (!setjmp(fault)) { __wrap_xk_AvSetDisplayMode(&c); assert(0); }
    assert(!present_calls); X_M32(c.r[4] + 16) = 0x12;
    present_result = -1;
    if (!setjmp(fault)) { __wrap_xk_AvSetDisplayMode(&c); assert(0); }
    assert(present_calls == 1 && !memcmp(&c, &interrupted, sizeof c));
    present_result = 0; __wrap_xk_AvSetDisplayMode(&c);
    interrupted.r[0] = 0; interrupted.r[4] += 28;
    assert(present_calls == 2 && !memcmp(&c, &interrupted, sizeof c));
    c.r[4] = 0x600000; c.r[0] = 0xABCDEFAA;
    X_M32(c.r[4] + 8) = 15; X_M32(c.r[4] + 12) = 0; X_M32(c.r[4] + 16) = 0x610000;
    X_M32(0x610000) = 0xAABBCCDD;
    __wrap_xk_AvSendTVEncoderOption(&c);
    assert(X_M32(0x610000) == 0 && c.r[0] == 0xABCDEFAA && c.r[4] == 0x600014);
    c.r[4] = 0x600000; X_M32(c.r[4] + 16) = 0x610001;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    assert(X_M32(0x610000) == 0 && c.r[4] == 0x600000);
    X_M32(c.r[4] + 8) = 9; X_M32(c.r[4] + 16) = 0;
    __wrap_xk_AvSendTVEncoderOption(&c);
    assert(present_calls == 2 && c.r[0] == 0xABCDEFAA && c.r[4] == 0x600014);
    c.r[4] = 0x600000; X_M32(c.r[4] + 12) = 1;
    blank_result = -1;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    assert(blank_calls == 1 && blank_value == 1 && c.r[4] == 0x600000);
    blank_result = 0; __wrap_xk_AvSendTVEncoderOption(&c);
    assert(blank_calls == 2 && c.r[4] == 0x600014);
    c.r[4] = 0x600000; __wrap_xk_AvSendTVEncoderOption(&c); /* repeated blank is idempotent */
    assert(blank_calls == 2);
    c.r[4] = 0x600000; X_M32(c.r[4] + 12) = 0; __wrap_xk_AvSendTVEncoderOption(&c);
    assert(blank_calls == 3 && blank_value == 0);
    c.r[4] = 0x600000; X_M32(c.r[4] + 12) = 2;
    if (!setjmp(fault)) { __wrap_xk_AvSendTVEncoderOption(&c); assert(0); }
    if (!setjmp(fault)) { h2_bus_write32(&c, 0, 0xFD40071C, 2); assert(0); }
    assert(software_calls == 1); /* no arbitrary external increment or DAC access */
    c.r[0] = 0x406C08; c.r[4] = 0x600000;
    uint32_t old_ramfc = h2_bus_read32(&c, 0, 0xFD711000);
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); } /* display still active */
    assert(h2_bus_read32(&c, 0, 0xFD711000) == old_ramfc);
    X_M32(c.r[4] + 12) = 1; __wrap_xk_AvSendTVEncoderOption(&c);
    c.r[0] = 0x406C08; c.r[4] = 0x600000;
    X_M32(0x406C08 + 0x190) = 0x12345678;
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); }
    X_M32(0x406C08 + 0x190) = 0;
    g_xpt[0x407] = 0x4000000;
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); }
    g_xpt[0x407] = 0x407000;
    g_xpt[0x83FEF] = 0x4000000;
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); }
    g_xpt[0x83FEF] = 0x3FEF000;
    uint32_t final_put = h2_bus_read32(&c, 0, 0xFD800044);
    X_M32(0x406C08 + 0x154) ^= 1;
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); }
    assert(h2_bus_read32(&c, 0, 0xFD000200) == UINT32_MAX);
    X_M32(0x406C08 + 0x154) ^= 1;
    interrupted = c; interrupted.r[4] += 4;
    h2_host_miniport_shutdown(&c);
    assert(!memcmp(&c, &interrupted, sizeof c)); /* complete CPU/FP/control state */
    assert(h2_bus_read32(&c, 0, 0xFD711000) == final_put);
    assert(h2_bus_read32(&c, 0, 0xFD711004) == final_put);
    assert(h2_bus_read32(&c, 0, 0xFD711010) == 0);
    assert(h2_bus_read32(&c, 0, 0xFD000200) == 0x01110000);
    assert(h2_bus_read32(&c, 0, 0xFD000140) == 0);
    if (!setjmp(fault)) { h2_bus_read32(&c, 0, 0xFD800044); assert(0); }
    c.r[0] = 0x406C08; c.r[4] = 0x600000;
    if (!setjmp(fault)) { h2_host_miniport_shutdown(&c); assert(0); } /* double shutdown */
    memset(X_G(0x406C08), 0, 0x81C); /* original caller clears its device */
    h2_host_miniport_init(&c);
    assert(calls == 8 && c.r[0] == 1 && c.r[4] == 0x600004);
    assert(X_M32(0x406C08 + 0x154) == 0x01110000);
    puts("Host-channel runtime: LTCG contracts, mapped resources, canonical color/depth/stencil pixels and strict rejection pass.");
    free(g_xpt); free(g_xram); return 0;
}
