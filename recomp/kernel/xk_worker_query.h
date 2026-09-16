#pragma once
#include "../xv_x86rt.h"
#ifdef XV_WORKER_QUERY
/* Called only inside the original 56670 guard. Success leaves c at 566DE;
 * failure leaves all live query state untouched. Neither path unlocks. */
int xv_worker_query(xctx *c, int guard);
int xv_object_query_lane(xctx *c, int guard, uint32_t base, unsigned bytes);
void xv_worker_query_report(void);
#endif
