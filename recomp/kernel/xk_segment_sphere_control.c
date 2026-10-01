#ifdef XV_NATIVE_SEGMENT_SPHERE
#include "xk_segment_sphere.h"
/* Mode bit zero selects the experiment; it starts OFF. Either route is exact
 * for every call, and the helper keeps no state across its only yield, so a
 * mode change needs no drained boundary. Counts are admitted calls. */
unsigned xv_segment_sphere_mode, xv_segment_sphere_count;
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
