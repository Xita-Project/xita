#include "kernel/xk_scene_index_hook.h"
#include <assert.h>
uint8_t *g_xram;
uint32_t *g_xpt;
static int owned;
uint32_t xv_scene_index_stack(const void *c) { (void)c; return owned ? 0x1000 : 0; }
unsigned xk_mem_arena_size(void) { return 0x60000; }
int main(void)
{
    xctx c = {0}, old;
    xv_scene_index_scope s;
    setenv("XV_SCENE_INDEX_RUN", "2", 1);
    g_xram = calloc(1, 0x60000); g_xpt = calloc(1u << 20, 4);
    assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 0x60; ++i) g_xpt[i] = i * 4096u;
    c.r[3] = 0x2000; c.r[4] = 0x7000; c.r[1] = 1; c.preempt = 100;
    uint32_t bound = 0x2100; memcpy(g_xram + 0x7014, &bound, 4);
    old = c;
    xv_scene_index_begin(&s, &c); xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(s.mode == 0 && !memcmp(&old, &c, sizeof c));
    owned = 1;
    xv_scene_index_begin(&s, &c);
#ifdef XV_CHECK_GUEST_ADDRESS
    assert(s.mode == 0);
#else
    assert(s.mode == 2);
    /* The index page and complete bound word must both be private. */
    uint32_t cursors[] = {0x400, 0x41000, 0xfffffffc};
    for (unsigned i = 0; i < 3; ++i) {
        c.r[3] = cursors[i]; old = c;
        xv_scene_index_step(&s, &c, g_xram, g_xpt);
        assert(!memcmp(&old, &c, sizeof c));
    }
    c.r[3] = 0x2000; c.r[4] = 0x41000 - 0x16; old = c;
    xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(!memcmp(&old, &c, sizeof c));
    c.r[4] = 0x7000;
    xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(c.r[3] == 0x20fc && c.preempt == 37);
    /* The only admitted non-stack region is the bounded CE scene list. */
    g_xpt[0x38b] = 0x10000;
    c.r[3] = 0x38be14; c.preempt = 100;
    bound = 0x39be18; memcpy(g_xram + 0x7014, &bound, 4); old = c;
    xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(!memcmp(&old, &c, sizeof c));
    bound = 0x38bf14; memcpy(g_xram + 0x7014, &bound, 4);
    xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(c.r[3] == 0x38bf10 && c.preempt == 37);
    c.r[3] = 0x39be14; old = c;
    xv_scene_index_step(&s, &c, g_xram, g_xpt);
    assert(!memcmp(&old, &c, sizeof c));
#endif
    free(g_xram); free(g_xpt);
    puts("PASS helper ownership, stack boundaries and diagnostic exclusion");
}
