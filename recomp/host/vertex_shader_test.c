/* Exercise resident shader selection through the real Xbox D3D HLE entry points. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xk.h"
#include "../kernel/xd3d.h"

uint8_t *g_xram;
xk_thread *xk_cur;
void xk_os_log(const char *fmt, ...) { (void)fmt; }
extern void xv_hle_D3DDevice_CreateVertexShader(xctx *);
extern void xv_hle_D3DDevice_GetVertexShaderSize(xctx *);
extern void xv_hle_D3DDevice_LoadVertexShader(xctx *);
extern void xv_hle_D3DDevice_SelectVertexShader(xctx *);
extern void xv_hle_D3DDevice_SetVertexShader(xctx *);
static uint32_t stack;
static void call(void (*fn)(xctx *), unsigned n, uint32_t a, uint32_t b,
                 uint32_t d, uint32_t e)
{
    xctx c = {0}; c.r[4] = stack;
    X_M32(stack) = 0x123456;
    X_M32(stack + 4) = a; X_M32(stack + 8) = b;
    X_M32(stack + 12) = d; X_M32(stack + 16) = e;
    fn(&c);
    assert(c.r[4] == stack + 4 * (n + 1));
    assert(X_M32(stack) == 0x123456);
}
static uint32_t shader(uint32_t decl, unsigned count, unsigned value)
{
    uint32_t func = xk_kalloc(4 + count * 16), out = xk_kalloc(4);
    X_M16(func) = 0x2078; X_M16(func + 2) = count;
    memset(X_G(func + 4), value, count * 16);
    call(xv_hle_D3DDevice_CreateVertexShader, 4, decl, func, out, 0);
    uint32_t handle = X_M32(out); assert(handle & 1);
    call(xv_hle_D3DDevice_GetVertexShaderSize, 2, handle, out, 0, 0);
    assert(X_M32(out) == count); /* instructions, not bytes including header */
    return handle;
}
static void load(uint32_t h, uint32_t address)
{ call(xv_hle_D3DDevice_LoadVertexShader, 2, h, address, 0, 0); }
static void select_shader(uint32_t h, uint32_t address)
{ call(xv_hle_D3DDevice_SelectVertexShader, 2, h, address, 0, 0); }
static void set(uint32_t h)
{ call(xv_hle_D3DDevice_SetVertexShader, 1, h, 0, 0, 0); }
int main(void)
{
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena(); stack = xk_kalloc(64);
    /* Halo's three model programs: VS 10 (lit), 9, 27. All use one declaration. */
    uint32_t lit = shader(0x1E1400, 66, 10);
    uint32_t plain = shader(0x1E1400, 45, 9);
    uint32_t third = shader(0x1E1400, 24, 27);
    load(lit, 0); select_shader(lit, 0);
    load(plain, 66); select_shader(plain, 66);
    load(third, 111); select_shader(third, 111);
    for (unsigned i = 0; i < 10000; i++) {
        select_shader(0, 0); assert(xd3d_state.vs_program == lit);
        assert(xd3d_state.vs_handle == third); /* null preserves declaration */
        select_shader(0, 66); assert(xd3d_state.vs_program == plain);
        select_shader(0, 111); assert(xd3d_state.vs_program == third);
    }
    /* A nonzero handle supplies input state, independently of the chosen code. */
    uint32_t other = shader(0x1E13A8, 8, 40);
    select_shader(other, 66);
    assert(xd3d_state.vs_handle == other && xd3d_state.vs_program == plain);
    load(other, 0); /* Load alone must not change selection/input state. */
    assert(xd3d_state.vs_handle == other && xd3d_state.vs_program == plain);
    select_shader(0, 0); assert(xd3d_state.vs_program == other);
    select_shader(0, 66); assert(xd3d_state.vs_program == plain);
    /* Set uploads at zero; it must replace the resident code at that address. */
    set(lit); select_shader(0, 0); assert(xd3d_state.vs_program == lit);
    select_shader(0, 66); assert(xd3d_state.vs_program == plain);
    /* Overwriting any instruction invalidates that entire compiled program. */
    load(other, 65); select_shader(0, 0); assert(!xd3d_state.vs_program);
    select_shader(0, 66); assert(!xd3d_state.vs_program);
    select_shader(0, 111); assert(xd3d_state.vs_program == third);
    /* Old ownership tags after a shorter replacement cannot invalidate it. */
    load(lit, 0); load(other, 0); load(third, 30);
    select_shader(0, 0); assert(xd3d_state.vs_program == other);
    load(other, 135); load(other, UINT32_MAX); /* no range wrap / out-of-bounds */
    select_shader(0, 111); assert(xd3d_state.vs_program == third);
    select_shader(0, UINT32_MAX); assert(!xd3d_state.vs_program);
    set(0x112); assert(xd3d_state.vs_handle == 0x112 && xd3d_state.vs_program == 0x112);
    call(xv_hle_D3DDevice_GetVertexShaderSize, 2, 0, 0, 0, 0);
    free(g_xram); free(g_xpt);
    puts("PASS: resident shader addresses, Halo lighting switches, declaration retention, size units, overlap and bounds");
}
