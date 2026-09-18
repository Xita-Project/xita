/* Pure typed visibility kernel; guest admission lives in xk_portal_polygon.c. */
#ifndef XITA_PORTAL_POLYGON_H
#define XITA_PORTAL_POLYGON_H

typedef struct { float x, y; } xp_point;
typedef struct { float x, y, d; } xp_plane;
typedef struct { unsigned backedges, planes, clips, vertices; } xp_work;

/* All arrays are separate native allocations. Counts/capacity are 1..256.
 * Inputs must be finite, with finite intermediate arithmetic. No guest state,
 * scheduler, allocation, or rendering operations occur inside this interface.
 * scratch holds two arrays of 256 points. The returned count is authoritative:
 * output is unspecified when count <= 0. -1 means clipping exceeded capacity;
 * -2 requires fallback because intersection arithmetic became nonfinite.
 * The caller is responsible for validating the admitted geometry and lifetime.
 */
int xp_portal_polygon(const xp_point *input, int count,
                      const xp_point *boundary, int edges,
                      int capacity, float tolerance,
                      xp_point *output, xp_point *scratch, xp_work *work);
#endif
