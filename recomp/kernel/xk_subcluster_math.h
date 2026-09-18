/* Data-only visibility kernel; production callers must validate the contract.
 * Inputs must be finite, ordered float bounds and finite plane coefficients.
 * Four side planes and enclosing AABB correspond to Halo 3925's 5C300 with
 * its extra reverse-corner check disabled (the 52EC1 subcluster caller).
 */
#ifndef XITA_EXPERIMENT_SUBCLUSTER_BOUNDS_H
#define XITA_EXPERIMENT_SUBCLUSTER_BOUNDS_H
typedef struct { float axis[3][2]; } xs_box;
typedef struct { float plane[4][4]; xs_box enclosing; } xs_frustum;
typedef struct { unsigned classification, backedges; } xs_bounds_result;
xs_bounds_result xs_bounds(const xs_frustum *, const xs_box *);
/* Each worker owns its output slice; the caller retains immutable inputs. */
void xs_bounds_batch(const xs_frustum *, const xs_box *, unsigned,
                     xs_bounds_result *);
#endif
