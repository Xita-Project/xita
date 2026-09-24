/* XV_REC_HLE render-state table against the original rs_method: every NV2A method offset (all 8192 method words,
 * masked like the HLE), a spread of values, both ps_synced states and random starting states; the complete
 * xd3d_state must match. Also the texture-state table read through one page against the 20 X_M32 reads. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/xd3d.c"

uint8_t *g_xram;
xk_thread *xk_cur;
void xk_os_log(const char *fmt, ...) { (void)fmt; }

static uint32_t rng = 99;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng; }

int main(void)
{
    xk_mem_setup(0x10000, 0x400000);
    g_xram = calloc(1, xk_mem_arena_size()); assert(g_xram);
    xk_mem_bind_arena();
    rs_table_init();
    static xd3d_state_t start, a, b;
    const uint32_t values[] = {0, 1, 2, 0x200, 0x203, 0x207, 0x208, 0x1E00, 0x1E01, 0x8007, 0x0101, 0x01000000u, 0xFFFFFFFFu, 0x12345678u};
    unsigned cases = 0;
    for (unsigned synced = 0; synced < 2; synced++) {
        ps_synced = (int)synced;
        for (uint32_t method = 0; method < 0x2000u; method++) {
            for (unsigned vi = 0; vi < sizeof values / sizeof *values + 2; vi++) {
                uint32_t v = vi < sizeof values / sizeof *values ? values[vi] : next();
                uint8_t *p = (uint8_t *)&start;
                for (unsigned i = 0; i < sizeof start; i++) p[i] = (uint8_t)next();
                if (vi & 1) {   /* half the time the combiner word already holds v: the no-change branch */
                    int off = ps_method_to_def(method & 0x1FFC);
                    if (off >= 0) start.ps_shadow[off / 4] = v;
                }
                start.ps_dirty = (int)(next() & 1);
                xd3d_state = start; rs_method_old(method | (next() & 0xE000u), v); a = xd3d_state;
                b = start; rs_state_fast(&b, method, v);
                assert(!memcmp(&a, &b, sizeof a));
                cases++;
            }
        }
    }
    /* texture-state table: the page read equals the 20 separate reads */
    for (unsigned round = 0; round < 64; round++) {
        for (unsigned i = 0; i < 128; i++) X_M32(D3D_G_TEXTURESTATE + 4 * i) = next();
        uint32_t old_ts[4][5], fast[4][5];
        xd3d_texture_states_old(old_ts);
        g_opt_hle.mode = 2; g_hle_now = -1; xd3d_texture_states(fast);
        assert(!memcmp(old_ts, fast, sizeof fast));
    }
    printf("PASS: %u render-state methods x values x states match the original; texture-state page reads match\n", cases);
    return 0;
}
