/* Halo 2 target: the shared recompiler's call macros (XV_HLE_TIMED in the generated xv_recomp_protos.h) and the
 * shared kernel's dispatch (recomp/kernel/xk_xapi.c) reference Halo CE's [hle-time] instrumentation, which CE
 * defines in recomp/kernel/xd3d.c - a unit this target does not link. Timing stays off here: xv_hle_timing is
 * never set, so the timed branch is never taken and xv_hle_time_add is never called. */
#include <stdint.h>

int xv_hle_timing;              /* 0: the generated XV_HLE_TIMED calls the HLE directly */
unsigned xv_hle_timed_calls;
const char *xv_hle_cur_name;
void xv_hle_time_add(const char *name, uint64_t us) { (void)name; (void)us; }
