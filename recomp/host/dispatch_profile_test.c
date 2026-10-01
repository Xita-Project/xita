/* Sample attribution across real indirect dispatch, nested callbacks, cached
 * guest targets, HLE vtables, and kernel magic imports. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../xv_x86rt.h"
uint8_t *g_xram;
uint32_t *g_xpt;
const unsigned xv_guest_trace_enabled = 1;
volatile uint32_t xv_cur_fn;
static unsigned callbacks, hles, magic;
static void guest_b(xctx *c) { (void)c; xv_cur_fn = 0x22000; callbacks++; }
static void guest_a(xctx *c)
{
    xv_cur_fn = 0x11000;
    xv_call(c, 0x22000);
    assert(xv_cur_fn == 0x11000);
    xv_call(c, 0x33000);
    assert(xv_cur_fn == 0x11000);
}
static void hle(xctx *c)
{
    assert(xv_cur_fn == 0x80033000);
    xv_call(c, 0x22000); /* native HLE calling back into guest code */
    assert(xv_cur_fn == 0x80033000);
    hles++;
}
const xv_fn_entry_t xv_fn_table[] = {{0x11000,guest_a},{0x22000,guest_b}};
const unsigned xv_fn_table_count = 2;
const xv_fn_entry_t xv_hle_table[] = {{0x33000,hle}};
const unsigned xv_hle_table_count = 1;
int xk_dispatch_magic(xctx *c, uint32_t target)
{
    assert(target == 0xfe000001 && xv_cur_fn == target);
    xv_call(c, 0x22000);
    assert(xv_cur_fn == target);
    magic++; return 1;
}
int main(void)
{
    xctx c = {0}; g_xram = calloc(1,4096); g_xpt = calloc(1u<<20,4);
    xv_cur_fn = 0x1234;
    for (unsigned i=0;i<3;i++) { /* first lookup followed by cache hits */
        xv_call(&c,0x11000); assert(xv_cur_fn == 0x1234);
        xv_call(&c,0xfe000001); assert(xv_cur_fn == 0x1234);
    }
    assert(callbacks==9 && hles==3 && magic==3);
    free(g_xram); free(g_xpt);
    puts("PASS: indirect/cached/nested guest calls, HLE callbacks and kernel imports restore profiler attribution");
}
