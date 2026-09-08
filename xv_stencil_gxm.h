#pragma once
#include <string.h>
#include <psp2/gxm.h>
#include "xv_stencil.h"

typedef struct { int valid; xv_stencil value; } xv_stencil_cache;

static inline void xv_stencil_bind(SceGxmContext *ctx, const xv_stencil *state)
{
    static const SceGxmStencilFunc funcs[] = {
        SCE_GXM_STENCIL_FUNC_NEVER, SCE_GXM_STENCIL_FUNC_LESS,
        SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_FUNC_LESS_EQUAL,
        SCE_GXM_STENCIL_FUNC_GREATER, SCE_GXM_STENCIL_FUNC_NOT_EQUAL,
        SCE_GXM_STENCIL_FUNC_GREATER_EQUAL, SCE_GXM_STENCIL_FUNC_ALWAYS
    };
    static const SceGxmStencilOp ops[] = {
        SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_ZERO,
        SCE_GXM_STENCIL_OP_REPLACE, SCE_GXM_STENCIL_OP_INCR,
        SCE_GXM_STENCIL_OP_DECR, SCE_GXM_STENCIL_OP_INVERT,
        SCE_GXM_STENCIL_OP_INCR_WRAP, SCE_GXM_STENCIL_OP_DECR_WRAP
    };
    xv_stencil disabled = {0, 7, 0, 0, 0, 0, 255, 0};
    const xv_stencil *s = state && state->enabled ? state : &disabled;
    sceGxmSetFrontStencilFunc(ctx, funcs[s->func], ops[s->fail],
        ops[s->depth_fail], ops[s->pass], s->read_mask, s->write_mask);
    sceGxmSetBackStencilFunc(ctx, funcs[s->func], ops[s->fail],
        ops[s->depth_fail], ops[s->pass], s->read_mask, s->write_mask);
    sceGxmSetFrontStencilRef(ctx, s->ref);
    sceGxmSetBackStencilRef(ctx, s->ref);
}

static inline void xv_stencil_bind_cached(xv_stencil_cache *cache,
    SceGxmContext *ctx, const xv_stencil *s)
{
    if (cache->valid && !memcmp(&cache->value, s, sizeof *s)) return;
    xv_stencil_bind(ctx, s);
    cache->value = *s;
    cache->valid = 1;
}

static inline void xv_stencil_clear(SceGxmContext *ctx, int enabled, uint8_t value)
{
    xv_stencil s = {1, 7, 2, 2, 2, value, 255, 255};
    xv_stencil_bind(ctx, enabled ? &s : NULL);
}
