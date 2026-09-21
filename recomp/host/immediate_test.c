/* Exercise the real HLE with both Xbox immediate vertex submission forms. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"
#include "../kernel/xd3d.h"

uint8_t *g_xram;
xk_thread *xk_cur;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
static xd3d_im_vtx captured[1024];
static unsigned count;
static int passthrough;
void xd3d_r_im_end(uint32_t prim, const xd3d_im_vtx *v, unsigned n)
{
    assert(prim == 7 && n <= 1024);
    count = n;
    passthrough = xd3d_im_passthrough();
    memcpy(captured, v, n * sizeof *v);
}
extern void xv_hle_D3DDevice_Begin(xctx *);
extern void xv_hle_D3DDevice_End(xctx *);
extern void xv_hle_D3DDevice_SetVertexData4f(xctx *);
extern void xv_hle_D3DDevice_SetTextureState_Deferred(xctx *);
extern void xv_hle_D3DDevice_SetTextureStageStateNotInline(xctx *);
extern void xv_hle_D3DDevice_SetTextureState_BorderColor(xctx *);

static uint32_t stack;
static void begin(void)
{
    xctx c = {0}; c.r[4] = stack;
    X_M32(stack + 4) = 7;
    xv_hle_D3DDevice_Begin(&c);
    assert(c.r[4] == stack + 8);
}
static void vertex(unsigned reg, float x, float y, float z, float w)
{
    float value[4] = {x, y, z, w};
    xctx c = {0}; c.r[4] = stack;
    X_M32(stack + 4) = reg;
    memcpy(X_G(stack + 8), value, sizeof value);
    xv_hle_D3DDevice_SetVertexData4f(&c);
    assert(c.r[4] == stack + 24);
}
static void end(void)
{
    xctx c = {0}; c.r[4] = stack;
    xv_hle_D3DDevice_End(&c);
    assert(c.r[4] == stack + 4);
}
int main(void)
{
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena(); stack = xk_kalloc(64); assert(stack);
    begin();
    vertex(3, .2f, .4f, .6f, .8f);
    vertex(9, 320, 240, 0, 1);
    for (unsigned i = 0; i < 4; ++i) vertex(UINT32_MAX, (float)i, -1, .5f, 1);
    end();
    assert(count == 4 && passthrough);
    for (unsigned i = 0; i < count; ++i) {
        assert(captured[i].a[0][0] == (float)i && captured[i].a[0][3] == 1);
        assert(captured[i].a[3][3] == .8f && captured[i].a[9][0] == 320);
    }
    /* Diagnostics must preserve the guest payload, including non-finite color,
     * rather than silently clamp it and hide its origin. */
    uint32_t nan_bits = 0x7fc12345u, captured_bits;
    float nan_color; memcpy(&nan_color, &nan_bits, sizeof nan_color);
    X_M32(stack) = 0xD4473u;
    X_M32(0x2E40C4u) = 0xBA000000u; X_M32(0x2E40C8u) = 0x404C28F6u;
    begin(); vertex(3, nan_color, 1, 0, 1);
    vertex(UINT32_MAX, 0, 0, .5f, 1); end();
    memcpy(&captured_bits, &captured[0].a[3][0], sizeof captured_bits);
    assert(count == 1 && passthrough && captured_bits == nan_bits);
    assert(X_M32(0x2E40C4u) == 0xBA000000u && X_M32(0x2E40C8u) == 0x404C28F6u);
    /* Attribute 15 is an ordinary attribute, not the -1 sentinel. */
    begin(); vertex(15, 7, 8, 9, 10); end();
    assert(count == 0 && !passthrough);
    begin(); vertex(0, 10, 20, 0, 1); end();
    assert(count == 1 && !passthrough && captured[0].a[15][0] == 7);
    /* A mesh draw must see current values even outside Begin/End. Fade is v9,
     * independent of diffuse v3 and of other attributes written afterwards. */
    vertex(9, .1f, .2f, .3f, .25f);
    vertex(3, 1, 0, 0, 1);
    const float (*attrs)[4] = xd3d_current_attributes();
    assert(attrs[9][3] == .25f && attrs[3][0] == 1 && attrs[15][0] == 7);
    begin(); end();
    assert(xd3d_current_attributes()[9][3] == .25f);
    /* Bounds are enforced without wrapping an invalid register to position. */
    begin(); vertex(16, 1, 2, 3, 4); end(); assert(count == 0);
    begin();
    for (unsigned i = 0; i < 1030; ++i) vertex(UINT32_MAX, (float)i, 0, 0, 1);
    end(); assert(count == 1024 && captured[1023].a[0][0] == 1023);
    /* Specialized border writes must reach the shared sampler mirror, including
     * alpha-only changes, and must not alias an invalid stage onto stage zero. */
    xctx border = {0}; border.r[4] = stack;
    X_M32(stack + 4) = 1; X_M32(stack + 8) = 0x05050505u;
    xv_hle_D3DDevice_SetTextureState_BorderColor(&border);
    assert(border.r[4] == stack + 12);
    assert(xd3d_texture_state(1, 29) == 0x05050505u);
    border.r[4] = stack; X_M32(stack + 8) = 0x80050505u;
    xv_hle_D3DDevice_SetTextureState_BorderColor(&border);
    assert(xd3d_texture_state(1, 29) == 0x80050505u);
    border.r[4] = stack; X_M32(stack + 4) = 5; X_M32(stack + 8) = 0;
    xv_hle_D3DDevice_SetTextureState_BorderColor(&border);
    assert(xd3d_texture_state(1, 29) == 0x80050505u);
    /* Deferred fastcall and stack setters must expose the same per-stage state
     * to the renderer, including Xbox 3925's filter indices 13/14. */
    xctx c = {0}; c.r[4] = stack; c.r[1] = 1; c.r[2] = 14;
    X_M32(stack + 4) = 2;
    xv_hle_D3DDevice_SetTextureState_Deferred(&c);
    assert(c.r[4] == stack + 8 && xd3d_texture_state(1, 14) == 2);
    c.r[4] = stack;
    X_M32(stack + 4) = 2; X_M32(stack + 8) = 13; X_M32(stack + 12) = 1;
    xv_hle_D3DDevice_SetTextureStageStateNotInline(&c);
    assert(c.r[4] == stack + 16 && xd3d_texture_state(2, 13) == 1);
    assert(xd3d_texture_state(1, 14) == 2 && xd3d_texture_state(4, 14) == 0);
    c.r[4] = stack; c.r[1] = 4; c.r[2] = 14; X_M32(stack + 4) = 1;
    xv_hle_D3DDevice_SetTextureState_Deferred(&c);
    assert(xd3d_texture_state(1, 14) == 2);
    free(g_xram); /* page table is statically owned by xk_mem */
    puts("PASS: immediate vertices, persistent attributes, bounds and sampler state");
}
