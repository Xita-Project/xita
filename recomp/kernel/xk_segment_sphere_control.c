#ifdef XV_NATIVE_SEGMENT_SPHERE
#include "xk_segment_sphere.h"
#ifndef XV_NATIVE_SEGMENT_SPHERE_DEFAULT
#define XV_NATIVE_SEGMENT_SPHERE_DEFAULT 0
#endif
#if XV_NATIVE_SEGMENT_SPHERE_DEFAULT != 0 && XV_NATIVE_SEGMENT_SPHERE_DEFAULT != 1
#error XV_NATIVE_SEGMENT_SPHERE_DEFAULT must be 0 or 1
#endif
/* Mode bit zero selects the experiment; default OFF unless the build selects
 * startup ON before any threads exist. Either route is exact
 * for every call, and the helper keeps no state across its only yield, so a
 * mode change needs no drained boundary. Counts are admitted calls. */
unsigned xv_segment_sphere_mode=XV_NATIVE_SEGMENT_SPHERE_DEFAULT;
unsigned xv_segment_sphere_count;
int xv_segment_sphere_enabled(void)
{
    return !!(__atomic_load_n(&xv_segment_sphere_mode, __ATOMIC_ACQUIRE) & 1u);
}
void xv_segment_sphere_override(int enabled)
{
    __atomic_store_n(&xv_segment_sphere_mode, enabled > 0 ? 1u : 0u, __ATOMIC_RELEASE);
}
unsigned xv_segment_sphere_calls(void)
{
    return __atomic_exchange_n(&xv_segment_sphere_count, 0u, __ATOMIC_RELAXED);
}
#endif
