/* Opt-in helper-exclusive admission for exact material arithmetic caches.
 * No shared-owner fallback when enabled: owner report/boundary calls must not
 * mutate a cache while a scene is in flight. Restart required to change mode. */
#pragma once
#include "../xv_x86rt.h"
#include "xk_owner_phase.h"
#include <stdlib.h>
extern uint32_t xv_scene_thread_context_generation(const void *) __attribute__((weak));
static inline int xv_render_cache_helper_enabled(void)
{
    static int configured = -1;
    int enabled = __atomic_load_n(&configured, __ATOMIC_RELAXED);
    if (enabled < 0) {
        const char *e = getenv("XV_MODEL_CACHE_HELPER");
        enabled = e && atoi(e) != 0;
        __atomic_store_n(&configured, enabled, __ATOMIC_RELAXED);
    }
    return enabled;
}
static inline int xv_render_cache_admit(xctx *c, uint32_t *generation)
{
    if (!xv_render_cache_helper_enabled())
        return xv_owner_phase_active(c, XV_OWNER_SCENE, generation);
    if (!generation || !xv_scene_thread_context_generation) return -1;
    uint32_t current = xv_scene_thread_context_generation(c);
    if (!current || (*generation && *generation != current)) return -1;
    *generation = current;
    return 1;
}
static inline const uint32_t *xv_render_cache_pt(void)
{
    return xv_render_cache_helper_enabled() ? X_PT : g_xpt;
}
static inline const uint8_t *xv_render_cache_image(void)
{
    return xv_render_cache_helper_enabled() ? X_IMG_BASE : g_img_base;
}
