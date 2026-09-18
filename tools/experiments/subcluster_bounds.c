#include "subcluster_bounds.h"

/* Preserve the original, different addition order for each side plane.
 * Build without contraction/reassociation, with -frounding-math. */
static double distance(const float *p, double x, double y, double z, unsigned i)
{
    if (i == 0) return ((x * p[0] + y * p[1]) + (double)p[2] * z) - p[3];
    if (i == 1) return (((double)p[0] * x + (double)p[2] * z) + y * p[1]) - p[3];
    return (((double)p[2] * z + y * p[1]) + (double)p[0] * x) - p[3];
}

xs_bounds_result xs_bounds(const xs_frustum *f, const xs_box *box)
{
    /* Keep the original broad-phase comparison order. */
    for (unsigned a=0; a<3; ++a)
        if (f->enclosing.axis[a][1] < box->axis[a][0]) return (xs_bounds_result){0,0};
    for (unsigned a=0; a<3; ++a)
        if (f->enclosing.axis[a][0] > box->axis[a][1]) return (xs_bounds_result){0,0};
    unsigned any=0, all=0;
    for (unsigned i=0; i<4; ++i) {
        const float *p=f->plane[i];
        /* Under the input contract each fixed-order expression is monotonic
         * in each coordinate. Its extrema occur at these two box corners.
         * Thus two evaluations replace eight without a center/radius rewrite
         * or a changed rounding order. */
        double lo[3],hi[3];
        for (unsigned a=0; a<3; ++a) {
            unsigned negative=p[a]<0;
            lo[a]=box->axis[a][negative];
            hi[a]=box->axis[a][!negative];
        }
        if (distance(p,hi[0],hi[1],hi[2],i)>0) any=1;
        if (distance(p,lo[0],lo[1],lo[2],i)>0) all=1;
    }
    /* Original executes eight corners and seven cooperative budget debits. */
    return (xs_bounds_result){all ? 0u : any ? 1u : 2u,7};
}

void xs_bounds_batch(const xs_frustum *f,const xs_box *boxes,unsigned n,
                     xs_bounds_result *out)
{
    for(unsigned i=0;i<n;++i)out[i]=xs_bounds(f,boxes+i);
}
