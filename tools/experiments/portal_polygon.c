/* A typed polygon/half-plane algorithm for the audited Halo 3925 visibility
 * boundary. Handwritten research implementation, deliberately not a runtime
 * replacement yet. Float stores between planes and double intermediates are
 * part of the geometry contract; build with -ffp-contract=off,
 * -frounding-math and no fast-math.
 */
#include "portal_polygon.h"
#include <math.h>

static int plane_from_edge(xp_point a, xp_point b, xp_plane *p)
{
    /* The two edge components have different rounding points in the engine. */
    p->x = (float)((double)a.y - b.y);
    double dy = (double)b.x - a.x;
    p->y = (float)dy;
    double length = sqrt((double)p->x * p->x + dy * dy);
    if (fabs(length) < 9.999999747378752e-5 || length == 0.0) return 0;
    double scale = 1.0 / length;
    p->x = (float)(scale * p->x);
    p->y = (float)(scale * p->y);
    p->d = (float)((double)p->y * b.y + (double)p->x * b.x);
    return 1;
}

static double distance(xp_point v, xp_plane p)
{
    return ((double)v.y * p.y + (double)p.x * v.x) - p.d;
}

static int near(xp_point a, xp_point b, double tolerance)
{
    return tolerance > fabs((double)a.x - b.x)
        && tolerance > fabs((double)a.y - b.y);
}

static int deduplicate(xp_point *out, int n, double tolerance)
{
    if (n > 1 && (near(out[n-1], out[0], tolerance)
                  || near(out[n-1], out[n-2], tolerance))) --n;
    return n;
}

static int half_plane(const xp_point *in, int n, xp_plane plane, int capacity,
                      double tolerance, xp_point *out, xp_work *work)
{
    ++work->clips;
    work->vertices += n;
    int used = 0, positive = 0, negative = 0;
    xp_point previous = in[n-1];
    int previous_inside = distance(previous, plane) >= 0.0;
    for (int i = 0; i < n; ++i) {
        xp_point current = in[i];
        double d = distance(current, plane);
        int inside = d >= 0.0;
        if (d > tolerance) positive = 1;
        else if (d < -tolerance) negative = 1;
        if (inside != previous_inside) {
            if (used == capacity) return -1;
            double dx = (double)previous.x - current.x;
            double dy = (double)previous.y - current.y;
            double t = -(d / (dx * plane.x + dy * plane.y));
            if (t < 0.0) t = 0.0;
            else if (t > 1.0) t = 1.0;
            out[used].x = (float)(t * dx + current.x);
            out[used].y = (float)(t * dy + current.y);
            used = deduplicate(out, used + 1, tolerance);
        }
        if (inside) {
            if (used == capacity) return -1;
            out[used] = current;
            used = deduplicate(out, used + 1, tolerance);
        }
        previous = current;
        previous_inside = inside;
        if (i + 1 < n) ++work->backedges;
    }
    if (!positive) return 0;
    ++work->backedges; /* Both positive-result exit paths branch backward. */
    /* Near-plane polygons are kept intact, including their original ordering. */
    if (!negative) {
        for (int i = 0; i < n; ++i) out[i] = in[i];
        return n;
    }
    return used < 3 ? 0 : used;
}

int xp_portal_polygon(const xp_point *input, int count,
                      const xp_point *boundary, int edges,
                      int capacity, float tolerance,
                      xp_point *output, xp_point *scratch, xp_work *work)
{
    *work = (xp_work){0};
    const xp_point *current = input;
    for (int i = 0; i < edges && count > 0; ++i) {
        ++work->planes;
        xp_point *next = i == edges - 1 ? output : scratch + (i & 1) * 256;
        xp_plane plane;
        if (plane_from_edge(boundary[i ? i - 1 : edges - 1], boundary[i], &plane)) {
            count = half_plane(current, count, plane, capacity, tolerance, next, work);
        } else {
            for (int j = 0; j < count; ++j) next[j] = current[j];
        }
        current = next;
        if (count < 0) break;
        if (i + 1 < edges) {
            ++work->backedges;
            if (count == 0) ++work->backedges;
        }
    }
    return count;
}
