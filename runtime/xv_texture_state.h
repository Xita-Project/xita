#ifndef XV_TEXTURE_STATE_H
#define XV_TEXTURE_STATE_H
#include <psp2/gxm.h>
#include <stdint.h>
#include <string.h>

/* Pump-owned and local to one uninterrupted mesh range. Never retain this
 * cache across a clear, UI replay, scene transition, or resource mutation.
 * A descriptor is copied, including sampler state; a resource pointer alone
 * cannot identify the active GXM binding. */
typedef struct {
    uint32_t valid;
    SceGxmTexture textures[SCE_GXM_MAX_TEXTURE_UNITS];
} xv_texture_state;

enum { XV_TEXTURE_BOUND, XV_TEXTURE_UNCHANGED, XV_TEXTURE_BIND_ERROR };

/* NULL selects the uncached path. The result describes an API invocation or
 * an exact redundant binding, rather than changing the caller's draw policy. */
static inline unsigned xv_texture_state_bind(xv_texture_state *state,
    SceGxmContext *ctx, unsigned unit, const SceGxmTexture *texture)
{
    uint32_t bit = state && unit < SCE_GXM_MAX_TEXTURE_UNITS ? 1u << unit : 0;
    if (bit && (state->valid & bit) &&
        !memcmp(&state->textures[unit], texture, sizeof *texture))
        return XV_TEXTURE_UNCHANGED;
    int error = sceGxmSetFragmentTexture(ctx, unit, texture);
    if (bit) {
        if (error < 0) state->valid &= ~bit;
        else {
            state->textures[unit] = *texture;
            state->valid |= bit;
        }
    }
    return error < 0 ? XV_TEXTURE_BIND_ERROR : XV_TEXTURE_BOUND;
}
#endif
