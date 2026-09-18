/* Bounded copied-data jobs on the existing object worker threads.
 * No guest callbacks, guest pointers, or GPU submission are allowed here. */
#pragma once
#include "../xv_x86rt.h"
#include "xk_subcluster_math.h"
enum { XV_VISIBILITY_JOB_CAPACITY=512 };
typedef struct { xs_frustum frustum; xs_box box; } xv_visibility_input;
#if XV_NATIVE_VISIBILITY_JOBS
/* Owner-only, synchronous lifetime: copy all inputs before waking workers;
 * publish output only after all workers finish. The caller owns valid native
 * arrays of n elements. Decline leaves output, guest context and FP unchanged.
 * This does not capture guest geometry or debit the original loop budget. */
int xv_visibility_classify_jobs(xctx *, const xv_visibility_input *, unsigned,
                                xs_bounds_result *);
#endif
