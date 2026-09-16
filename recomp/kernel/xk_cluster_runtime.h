#pragma once
#include "../xv_x86rt.h"
#ifdef XV_TYPED_CLUSTER_QUERY
/* Only the guest owner, with all prior workers joined, may begin/end a batch.
 * Invalidation never frees storage; it can cancel in-flight private results. */
void xv_cluster_runtime_begin(void);
void xv_cluster_runtime_end(void);
void xv_cluster_runtime_invalidate(unsigned service);
/* Actual pool ownership checks, not context-marker-only admission. */
int xv_object_query_source_allowed(uintptr_t pointer,unsigned bytes);
int xv_object_query_private(xctx *,int guard,uint32_t address,unsigned bytes);
#endif
