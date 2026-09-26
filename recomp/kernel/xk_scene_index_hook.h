#pragma once
#include "xk_scene_index_run.h"
#include <stdlib.h>
#include <stdio.h>
#ifdef __vita__
/* Ordinary helper logging is suppressed on hardware. Verification uses the
 * explicit critical sink; fast mode reports activation once only. */
extern void xv_log_criticalf(const char *, ...);
#define XV_INDEX_LOG(...) xv_log_criticalf(__VA_ARGS__)
#else
#define XV_INDEX_LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

extern uint32_t xv_scene_index_stack(const void *context);
extern unsigned xk_mem_arena_size(void);
/* All statics below belong exclusively to the scene helper. Other callers
 * return before touching them. Mode 1 predicts into a copy; mode 2 applies.
 * Restrict both modes to the same owned inputs so verification covers admission.
 */
typedef struct {
    xctx expected;
    uint32_t stack;
    unsigned bytes, pending, tried;
    int mode;
} xv_scene_index_scope;

static inline void xv_scene_index_begin(xv_scene_index_scope *s, xctx *c)
{
    s->stack = 0; s->bytes = 0; s->pending = 0; s->tried = 0; s->mode = 0;
#ifndef XV_CHECK_GUEST_ADDRESS
    s->stack = xv_scene_index_stack(c);
    if (s->stack) {
        static int mode = -1;
        if (mode < 0) {
            const char *e = getenv("XV_SCENE_INDEX_RUN");
            mode = e ? atoi(e) : 0;
            if (mode != 1 && mode != 2) mode = 0;
            XV_INDEX_LOG("[scene-index] mode %d; private-stack admission\n", mode);
        }
        s->mode = mode;
        if (mode) s->bytes = xk_mem_arena_size();
    }
#endif
}

static inline unsigned xv_scene_index_owned(xv_scene_index_scope *s, xctx *c,
                                            uint8_t *arena, const uint32_t *pages)
{
    /* Private stack is a page-aligned 256 KiB allocation. The primitive never
     * reads an index outside this one page. Validate the complete bound word. */
    uint32_t next = c->r[3] + 4u, slot = c->r[4] + 0x14u;
    if (!s->stack || next < c->r[3] || slot < c->r[4] ||
        next - s->stack >= 256u * 1024u ||
        slot - s->stack > 256u * 1024u - 4u) return 0;
    return xv_scene_index_run(c, arena, pages, s->bytes);
}

static inline void xv_scene_index_step(xv_scene_index_scope *s, xctx *c,
                                       uint8_t *arena, const uint32_t *pages)
{
    if (!s->mode) return;
    if (s->mode == 2) {
        static unsigned batches, entries;
        unsigned n = xv_scene_index_owned(s, c, arena, pages);
        if (n) {
            entries += n;
            if (++batches == 1u)
                XV_INDEX_LOG("[scene-index] applied %u batches %u entries\n", batches, entries);
        }
        return;
    }
    if (s->pending && !--s->pending) {
        static unsigned checked;
        if (memcmp(c, &s->expected, sizeof *c)) {
            XV_INDEX_LOG("[scene-index] MISMATCH after %u prefixes; original loop retained\n", checked);
            s->mode = 0;
            return;
        }
        if (!(++checked % 65536u))
            XV_INDEX_LOG("[scene-index] verified %u prefixes mismatches 0\n", checked);
    }
    if (!s->tried) {
        s->expected = *c;
        s->pending = xv_scene_index_owned(s, &s->expected, arena, pages);
        s->tried = 1;
    }
}
static inline void xv_scene_index_end_run(xv_scene_index_scope *s)
{
    if (s->pending) {
        XV_INDEX_LOG("[scene-index] MISMATCH premature exit; original loop retained\n");
        s->mode = 0;
        s->pending = 0;
    }
    s->tried = 0;
}
