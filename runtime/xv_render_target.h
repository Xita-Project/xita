#ifndef XV_RENDER_TARGET_H
#define XV_RENDER_TARGET_H

#include <stdint.h>
#include <stdlib.h>
#include "xv_log.h"

/* Include after <psp2/gxm.h>. Allocation-time policy only; never resize a live
 * target. Repeated scenes retain their existing submission and GPU lifetime. */
#define XV_RT_EXTRA_DRIVER_BYTES (512u * 1024u)
static inline unsigned xv_render_target_scenes(void)
{
    const char *e = getenv("XV_RT_SCENES");
    if (!e || !*e) return 4;
    char *end;
    long n = strtol(e, &end, 10);
    if (end == e || *end) return 4;
    return n < 1 ? 1 : n > 8 ? 8 : (unsigned)n;
}

/* Driver memory is queried for each capacity, counted against the caller's
 * remaining budget, and limited to 512 KiB extra per target. Creation failure
 * or insufficient space retries smaller capacities down to the original one.
 * Input parameters and output ownership are unchanged on failure. */
static inline int xv_render_target_create(const SceGxmRenderTargetParams *params,
    uint32_t budget, SceGxmRenderTarget **target, uint32_t *driver_bytes,
    const char *label)
{
    SceGxmRenderTargetParams p = *params;
    unsigned requested = xv_render_target_scenes(), scenes = requested;
    uint32_t baseline = 0;
    p.scenesPerFrame = 1;
    int rc = sceGxmGetRenderTargetMemSize(&p, &baseline);
    if (rc < 0) return rc;
    if (baseline > budget) return -1;
    for (;;) {
        p.scenesPerFrame = scenes;
        uint32_t bytes = baseline;
        rc = scenes == 1 ? 0 : sceGxmGetRenderTargetMemSize(&p, &bytes);
        if (rc >= 0 && bytes <= budget &&
            (bytes <= baseline || bytes - baseline <= XV_RT_EXTRA_DRIVER_BYTES)) {
            SceGxmRenderTarget *created = NULL;
            rc = sceGxmCreateRenderTarget(&p, &created);
            if (rc >= 0) {
                *target = created;
                if (driver_bytes) *driver_bytes = bytes;
                xv_logf("[render-capacity] %s %ux%u: scenes %u requested %u driver %u baseline %u bytes\n",
                    label, (unsigned)p.width, (unsigned)p.height, scenes, requested, bytes, baseline);
                return rc;
            }
        } else if (rc >= 0) rc = -1;
        if (scenes == 1) return rc;
        xv_logf("[render-capacity] %s: retry below %u scenes (rc %08X, bytes %u, budget %u)\n",
            label, scenes, (unsigned)rc, bytes, budget);
        scenes = (scenes + 1) / 2;
    }
}

#endif
