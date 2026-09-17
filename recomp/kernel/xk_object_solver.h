/* Research-only boundary for Halo 3925's captured collision solver.
 * Memory admission does not prove the surrounding object update is atomic.
 * This remains disabled until caller-state ordering is qualified separately. */
#pragma once
#include "../xv_x86rt.h"
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_SOLVER_EXPERIMENT)
void xv_object_solver_override(int enabled);
int xv_object_solver_begin(xctx *c);
void xv_object_solver_end(int *token);
#define XV_OBJECT_SOLVER_SCOPE(c) \
    int xv_object_solver_token_ __attribute__((cleanup(xv_object_solver_end))) = xv_object_solver_begin(c)
#endif
