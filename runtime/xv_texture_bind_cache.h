#ifndef XV_TEXTURE_BIND_CACHE_H
#define XV_TEXTURE_BIND_CACHE_H
#include <stdint.h>
#include <string.h>
/* Include after GXM declarations. Own descriptor copies, never borrowed pointers.
 * Local to one context's uninterrupted replay range; reset at clears/UI/scenes.
 * GPU units, not guest stages, are the identity because linked shaders remap them. */
typedef struct {
    SceGxmTexture texture[SCE_GXM_MAX_TEXTURE_UNITS];
    uint32_t valid;
    int enabled;
    unsigned submitted, reused;
} xv_texture_bind_cache;

static inline int xv_texture_bind_cached(xv_texture_bind_cache *cache,
    SceGxmContext *ctx, unsigned unit, const SceGxmTexture *texture)
{
    if (unit >= SCE_GXM_MAX_TEXTURE_UNITS) return 0;
    uint32_t bit = 1u << unit;
    if (cache && cache->enabled && (cache->valid & bit) &&
        !memcmp(&cache->texture[unit], texture, sizeof *texture)) {
        cache->reused++;
        return 1;
    }
    int result = sceGxmSetFragmentTexture(ctx, unit, texture);
    if (cache) {
        cache->submitted++;
        /* A failed setter cannot establish what is bound in the driver. */
        cache->valid &= ~bit;
        if (cache->enabled && result == SCE_OK) {
            cache->texture[unit] = *texture;
            cache->valid |= bit;
        }
    }
    return result == SCE_OK;
}
#endif
