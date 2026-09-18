#pragma once
#include "../xv_x86rt.h"
/* Only the pinned 52E10 call/loop sites may use these narrowed interfaces.
 * A zero return preserves guest state and lets the original path execute. */
int xv_subcluster_bounds(xctx *);
int xv_subcluster_publish(xctx *);
void xv_subcluster_report(unsigned frames);
