/* Both a generated trap and an unresolved indirect call reach a diagnostic
 * handler. This matters because linker --wrap does not intercept a call to a
 * function defined in the same runtime object. */
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include "../xv_x86rt.h"
uint8_t *g_xram;
uint32_t *g_xpt;
const xv_fn_entry_t xv_fn_table[] = {{0, NULL}};
const unsigned xv_fn_table_count = 0;
const xv_fn_entry_t xv_hle_table[] = {{0, NULL}};
const unsigned xv_hle_table_count = 0;
static jmp_buf escape;
static xctx *expected;
static uint32_t observed;
void xv_runtime_trap(xctx *c, uint32_t address)
{ assert(c == expected && c->r[0] == 0x1000 && c->r[4] == 0x1000); observed = address; longjmp(escape, 1); }
int main(void)
{
    g_xram = calloc(1, 8192); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    xctx c = {0}; c.r[0] = c.r[4] = 0x1000; expected = &c;
    if (!setjmp(escape)) { xv_trap(&c, 0x22000); abort(); }
    assert(observed == 0x22000);
    if (!setjmp(escape)) { xv_call(&c, 0x33000); abort(); }
    assert(observed == 0x33000);
    free(g_xpt); free(g_xram);
    return 0;
}
