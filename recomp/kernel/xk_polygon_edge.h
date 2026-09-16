#ifndef XV_POLYGON_EDGE_H
#define XV_POLYGON_EDGE_H

#include "xv_x86rt.h"

/* Present only in an XV_NATIVE_POLYGON_EDGE=1 build. No environment switch.
 * At a joined/idle worker boundary, init binds the control owner, mode OFF.
 * Existing workers are allowed; choose the eventual recording/report thread.
 * init/override/counter drain require that owner and a drained worker pool;
 * override enables only positive values; 0 disables, -1 restores default OFF.
 * Misuse or changing controls across a suspended guest call aborts.
 * The integrating runtime must drain all users before changing mode. */
void xv_native_polygon_edge_init(void);
int xv_native_polygon_edge_available(void);
void xv_native_polygon_edge_override(int enabled);
unsigned xv_math_polygon_edge_calls(void);

/* Zero leaves the entire context/memory untouched for the original fallback.
 * One completes the exact guest function, including preemption handoffs. */
int xv_math_polygon_edge(xctx *c);

/* Internal generated-helper admission. Never obtains an OS thread identity.
 * One begin must pair with one end, including across scheduler suspension. */
int xv_polygon_edge_begin(void);
void xv_polygon_edge_end(void);

#endif
