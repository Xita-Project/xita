#ifndef XK_QUAT_CACHE_H
#define XK_QUAT_CACHE_H
#include "../xv_x86rt.h"
/* Guest-thread-only experiment. The caller retains all native math guards. */
typedef struct {
    uint32_t key[8], fp_before;
    unsigned slot, active;
} xv_quat_cache_request;
int xv_quat_cache_restore(xctx *c, const float *input, float *output,
                          float *scratch, const void *constants,
                          xv_quat_cache_request *request);
void xv_quat_cache_store(const xctx *c, const float *output, const float *scratch,
                         const xv_quat_cache_request *request);
void xv_quat_cache_report(unsigned frames);
#endif
