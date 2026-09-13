#include "gpu_bus.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void h2_audio_unavailable(xctx *c);
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf fault;
static unsigned calls;
uint32_t xk_mem_arena_size(void) { return 0x6000; }
void xv_logf(const char *format, ...) { (void)format; ++calls; }
void h2_graphics_stop(xctx *c, uint32_t ip, uint32_t address, uint32_t value, int write, int reason)
{ (void)c; (void)ip; (void)address; (void)value; (void)write; (void)reason; longjmp(fault, 1); }
static void reject(xctx *c)
{
    xctx before = *c; unsigned logged = calls;
    if (!setjmp(fault)) { h2_audio_unavailable(c); assert(!"expected rejection"); }
    assert(!memcmp(c, &before, sizeof before) && calls == logged);
}
int main(void)
{
    g_xram = malloc(0x6000); g_img_base = g_xram; g_xpt = malloc((1u << 20) * 4);
    assert(g_xram && g_xpt); memset(g_xram, 0xCC, 0x6000);
    for (unsigned p = 0; p < 1u << 20; ++p) g_xpt[p] = 0x5000;
    g_xpt[1] = 0; g_xpt[2] = 0x2000; g_xpt[3] = 0x4000;
    xctx c; memset(&c, 0x5A, sizeof c); c.r[4] = 0x1FF4;
    X_M32(0x1FF4) = 0x12345678; X_M32(0x1FF8) = 0; X_M32(0x1FFC) = 0x2FFE; X_M32(0x2000) = 0;
    uint8_t memory[0x6000]; memcpy(memory, g_xram, sizeof memory);
    xctx expected = c; expected.r[0] = 0x88780078; expected.r[4] += 16;
    h2_audio_unavailable(&c);
    assert(!memcmp(&c, &expected, sizeof c) && !memcmp(memory, g_xram, sizeof memory) && calls == 1);
    c.r[4] = 0x1FF4; X_M32(0x1FF8) = 1; reject(&c);
    X_M32(0x1FF8) = 0; X_M32(0x2000) = 1; reject(&c);
    X_M32(0x2000) = 0; g_xpt[3] = 0x5000; reject(&c);
    g_xpt[3] = 0x4000; g_xpt[2] = 0x5000; reject(&c);
    c.r[4] = 0x1FF5; reject(&c);
    free(g_xpt); free(g_xram); puts("Halo 2 unavailable-audio diagnostic ABI tests passed"); return 0;
}
