#include "input.h"
#include "gpu_bus.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf fault;
static h2_pad_sample sample;
static unsigned platform_calls;
static int platform_result, initializing;
uint32_t xk_mem_arena_size(void) { return 0x501000; }
void xv_logf(const char *format, ...) { (void)format; }
void h2_graphics_stop(xctx *c, uint32_t ip, uint32_t address, uint32_t value, int write, int reason)
{ (void)c; (void)ip; (void)address; (void)value; (void)write; (void)reason; longjmp(fault, 1); }
int h2_platform_pad(h2_pad_sample *out, int initialize)
{ ++platform_calls; initializing = initialize; *out = sample; return platform_result; }
static xctx cpu;
static void args(uint32_t a, uint32_t b, uint32_t d, uint32_t e)
{
    memset(&cpu, 0xA6, sizeof cpu); cpu.r[4] = 0x1800;
    X_M32(0x1800) = 0x12345678;
    X_M32(0x1804) = a; X_M32(0x1808) = b; X_M32(0x180C) = d; X_M32(0x1810) = e;
}
static void call(void (*function)(xctx *), unsigned count, int returns, uint32_t value)
{
    xctx expected = cpu; expected.r[4] += 4 + count * 4;
    if (returns) expected.r[0] = value;
    function(&cpu); assert(!memcmp(&cpu, &expected, sizeof cpu));
}
static void reject(void (*function)(xctx *))
{
    xctx expected = cpu;
    if (!setjmp(fault)) { function(&cpu); assert(!"expected rejection"); }
    assert(!memcmp(&cpu, &expected, sizeof cpu));
}
int main(void)
{
    g_xram = calloc(1, 0x501000); g_img_base = g_xram;
    g_xpt = malloc((1u << 20) * 4); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 1u << 20; ++i) g_xpt[i] = 0x500000;
    for (unsigned i = 0; i < 0x500; ++i) g_xpt[i] = i * 4096;
    /* Input arrays and output spans cross unrelated physical pages. */
    g_xpt[3] = 0x6000;
    uint32_t entries[] = {0x4086F8, 4, 0x4088B0, 4, 0x4088BC, 4};
    x_guest_write(0x2FF4, entries, sizeof entries);
    args(2, 0x2FF4, 0, 0); reject(h2_input_init); assert(!platform_calls);
    g_xpt[3] = 0x500000;
    args(3, 0x2FF4, 0, 0); reject(h2_input_init); assert(!platform_calls);
    g_xpt[3] = 0x6000;
    platform_result = -7;
    args(3, 0x2FF4, 0, 0); reject(h2_input_init);
    assert(platform_calls == 1 && !X_M32(0x4086F8));
    platform_result = 0;
    args(3, 0x2FF4, 0, 0); call(h2_input_init, 2, 0, 0);
    assert(platform_calls == 2 && initializing && X_M32(0x4086F8) == 1 &&
           X_M32(0x4086FC) == 1 && !X_M32(0x408700));
    assert(!X_M32(0x4088B0) && !X_M32(0x4088BC) && !X_M32(0x4087A8));
    args(3, 0x2FF4, 0, 0); reject(h2_input_init); assert(platform_calls == 2);
    args(0x4086F8, 1, 0, 0); call(h2_input_open, 4, 1, 0);
    args(0x4088B0, 0, 0, 0); call(h2_input_open, 4, 1, 0);
    args(0x4086F8, 0, 0, 0x2000); reject(h2_input_open);
    args(0x4086F8, 0, 0, 0); call(h2_input_open, 4, 1, 0xE2000001);
    args(0x4086F8, 0, 0, 0); reject(h2_input_open);
    uint8_t bytes[80], expected[80]; memset(expected, 0xCC, sizeof expected);
    x_guest_write(0x2FF4, expected, sizeof expected);
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_state, 2, 1, 0);
    memset(expected, 0, 22); expected[0] = 1;
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    assert(platform_calls == 3 && !initializing);
    sample.buttons = 0x25; sample.analog[0] = 255; sample.analog[7] = 99;
    sample.axes[0] = -32768; sample.axes[1] = 32767; sample.axes[2] = -1; sample.axes[3] = 1234;
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_state, 2, 1, 0);
    const uint8_t changed[] = {2,0,0,0,0x25,0,255,0,0,0,0,0,0,99,0,128,255,127,255,255,0xD2,4};
    memcpy(expected, changed, sizeof changed);
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_state, 2, 1, 0);
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    unsigned calls = platform_calls; g_xpt[3] = 0x500000;
    args(0xE2000001, 0x2FF4, 0, 0); reject(h2_input_state); assert(platform_calls == calls);
    reject(h2_input_capabilities); reject(h2_input_feedback);
    g_xpt[3] = 0x6000;
    platform_result = -8; reject(h2_input_state);
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    platform_result = 0;
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_capabilities, 2, 1, 0);
    const uint8_t caps[] = {1,0,0,0x3F,0,255,255,255,255,0,0,255,255,255,255,255,255,255,255,255,255,0,0,0,0};
    memcpy(expected, caps, sizeof caps);
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_feedback, 2, 1, 50);
    expected[0] = 50; expected[1] = expected[2] = expected[3] = 0;
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    args(0xE2000001, 0, 0, 0); call(h2_input_close, 1, 0, 0); reject(h2_input_close);
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_state, 2, 1, 1167);
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_capabilities, 2, 1, 1167);
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_feedback, 2, 1, 1167);
    x_guest_read(bytes, 0x2FF4, sizeof bytes); assert(!memcmp(bytes, expected, sizeof bytes));
    args(0x4086F8, 0, 0, 0); call(h2_input_open, 4, 1, 0xE2000002);
    args(0xE2000001, 0x2FF4, 0, 0); call(h2_input_state, 2, 1, 1167);
    free(g_xpt); free(g_xram); puts("Halo 2 input ABI tests passed"); return 0;
}
