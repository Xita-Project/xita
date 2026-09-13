#include "host_channel_runtime.h"
#include "gpu_bus.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf fault;
static uint32_t last_ip, last_address, calls;
uint32_t h2_instance_bytes(void) { return 0x5000; }
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
{ assert(c->r[6] == 0x406C08); ++calls; X_RET(0); }
void f_00401D96(xctx *c)
{
    assert(c->r[6] == 0x406C08); ++calls;
    X_M32(c->r[6] + 0x130) = 0x710000;
    X_M32(c->r[6] + 0x128) = 0x711000;
    X_M32(c->r[6] + 0x140) = 0x110A;
    X_RET(0);
}
static void instance_word(xctx *c, uint32_t offset, uint32_t word)
{ h2_bus_write32(c, 0, 0xFD700000 + offset, word); }
static void packet(uint32_t *put, uint16_t method, uint32_t value)
{ X_M32(0x80000000u + *put) = 0x40000u | method; X_M32(0x80000004u + *put) = value; *put += 8; }
int main(void)
{
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
    X_M32(0x404FE0 + 0x24) = 0x80010000; X_M32(0x404FE0 + 0x28) = 0x80011000;
    X_M32(0x406C08 + 0x10C) = 0x111D; X_M32(0x61000C) = 0x1112;
    X_M32(0x406C08 + 0x160) = 0x149C;
    instance_word(&c, 0x11120, 0xB002); instance_word(&c, 0x11124, 0x3FFFFFF);
    instance_word(&c, 0x11128, 3); instance_word(&c, 0x1112C, 3);
    c.r[0] = 8; c.r[2] = 0x406C08;
    X_M32(c.r[4] + 4) = 128; X_M32(c.r[4] + 8) = 128; X_M32(c.r[4] + 12) = 0x610000;
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
    instance_word(&c, 0x10068, 13); instance_word(&c, 0x1006C, 0x800114A0);
    instance_word(&c, 0x14A00, 0x97);
    instance_word(&c, 0x10018, 3); instance_word(&c, 0x1001C, 0x80001113);
    instance_word(&c, 0x11130, 0xB003); instance_word(&c, 0x11134, 15);
    instance_word(&c, 0x11138, 0x20003); instance_word(&c, 0x1113C, 0x20003);
    static uint32_t put; put = 0x10000;
    packet(&put, 0, 13); packet(&put, 0x194, 3);
    packet(&put, 0x200, 2 << 16); packet(&put, 0x204, 2 << 16);
    packet(&put, 0x208, 0x128); packet(&put, 0x20C, 8); packet(&put, 0x210, 0);
    packet(&put, 0x1D98, 1 << 16); packet(&put, 0x1D9C, 1 << 16);
    packet(&put, 0x1D90, 0x12345678); packet(&put, 0x1D94, 0xF0);
    h2_bus_write32(&c, 0x1234, 0xFD800040, put);
    for (unsigned i = 0; i < 16; i += 4) assert(X_M32(0x80020000 + i) == 0x12345678);
    packet(&put, 0x1D90, 0x87654321); packet(&put, 0x1D94, 0xF0);
    g_xpt[0x80020] = 0x30000;
    if (!setjmp(fault)) { h2_bus_write32(&c, 0x1234, 0xFD800040, put); assert(0); }
    assert(h2_bus_read32(&c, 0, 0xFD800044) == put - 4);
    for (unsigned i = 0; i < 16; i += 4) assert(*(uint32_t *)(g_xram + 0x20000 + i) == 0x12345678);
    puts("Host-channel runtime: LTCG contracts, instance state, bootstrap, pixels and strict MMIO/map rejection pass.");
    free(g_xpt); free(g_xram); return 0;
}
