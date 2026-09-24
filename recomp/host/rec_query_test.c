/* XV_REC_QUERY: the visibility-query hash index against the original 512-entry scans, in verify mode (both run,
 * every slot/return code/serial compared) and then in fast mode against a scan-only reference run of the same
 * sequence: random IDs below and above the table size, reissues, reads of issued and unknown IDs, generation reads,
 * and the out-of-memory path once more than 512 IDs exist. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../runtime/xv_d3d.c"

void xv_logf(const char *fmt, ...) { (void)fmt; }

static uint32_t rng;
static uint32_t next(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static uint32_t pick_id(void)
{
    uint32_t r = next() % 100;
    if (r < 50) return next() % 400;                 /* Halo-like small IDs */
    if (r < 80) return 512 + next() % 300;           /* IDs past the table size */
    return next();                                   /* anything */
}
/* One sequence; returns a digest of every observable result. */
static uint64_t run(unsigned seed, unsigned steps)
{
    rng = seed; uint64_t digest = 1469598103934665603ull;
    #define MIX(x) (digest = (digest ^ (uint64_t)(x)) * 1099511628211ull)
    for (unsigned i = 0; i < steps; i++) {
        uint32_t id = pick_id(), op = next() % 4, pixels = 0xABCD;
        if (op == 0) {
            xd3d_r_visibility_begin(640, 480);
            MIX(xd3d_r_visibility_end(id));
            cmdlist_t *l = cur_list();
            if (l->nvisibility) { MIX(l->visibility[l->nvisibility - 1].result_slot); MIX(l->visibility[l->nvisibility - 1].serial); }
            if (l->nvisibility == XV_VISIBILITY_PER_FRAME) l->nvisibility = 0;
        } else if (op == 1) { MIX(xd3d_r_visibility_result(id, &pixels)); MIX(pixels); }
        else if (op == 2) MIX(xd3d_r_visibility_generation(id));
        else { uint32_t behind = 0; MIX(xd3d_r_visibility_result_stale(id, &pixels, &behind)); MIX(pixels); MIX(behind); }
    }
    return digest;
}
static void reset(void)
{
    memset(g_visibility_results, 0, sizeof g_visibility_results);
    memset(g_vis_map, 0, sizeof g_vis_map); g_vis_count = 0;
    for (unsigned i = 0; i < XV_NUM_LISTS; i++) memset(g_lists[i], 0, sizeof *g_lists[i]);
}
int main(void)
{
    for (unsigned i = 0; i < XV_NUM_LISTS; i++) { g_lists[i] = calloc(1, sizeof(cmdlist_t)); assert(g_lists[i]); }
    unsigned sequences = 0;
    for (unsigned seed = 1; seed <= 24; seed++) {
        unsigned steps = seed % 3 == 0 ? 20000 : 3000;   /* long ones fill the table and hit out-of-memory */
        reset(); g_opt_query.mode = 0; uint64_t scan = run(seed, steps);
        reset(); g_opt_query.mode = 1; uint64_t verify = run(seed, steps);
        reset(); g_opt_query.mode = 2; uint64_t fast = run(seed, steps);
        assert(scan == verify && scan == fast);
        sequences++;
    }
    assert(g_opt_query.session_checks > 100000 && g_opt_query.session_mismatches == 0);
    printf("PASS: %u query sequences: index results equal the scans (%llu verified lookups, 0 mismatches)\n",
        sequences, (unsigned long long)g_opt_query.session_checks);
    return 0;
}
