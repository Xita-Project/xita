#pragma once
#include "xk_cluster_query.h"
#include "../xv_x86rt.h"

/* Numeric guest addresses are used only to reconstruct original scratch and
 * registers. They are never dereferenced by this prototype. Capture these from
 * the SAME stable generation as geometry; arrays have one entry per cluster
 * and portal respectively. No runtime integration or publication is implied. */
typedef struct {
    uint32_t bsp, clusters, portals, projection_planes;
    const uint32_t *adjacency_addresses, *original_plane_indices;
    uint32_t center_address, head_address;
} XvClusterReplayLayout;
enum { XV_CLUSTER_SCRATCH=16384, XV_CLUSTER_DIRTY_WORDS=256 };
typedef struct {
    xctx context;
    unsigned char scratch[XV_CLUSTER_SCRATCH];
    /* Bit i marks two bytes at entry_esp - SCRATCH + 2*i. Unmarked bytes
     * are unspecified and MUST NOT be published or compared. */
    uint32_t dirty[XV_CLUSTER_DIRTY_WORDS];
} XvClusterReplay;
/* All inputs and output allocations are disjoint. Geometry must be validated,
 * owned and immutable. Layout arrays cover geometry counts. Entry stack is
 * four-byte aligned and >= SCRATCH; native FP traps must be disabled.
 * Only private result/replay outputs change. A decline requires discarding
 * both and restoring native FP state before falling back. The caller must
 * separately validate input/layout consistency, mappings, aliases, generation
 * and synchronization before any real guest publication or original list tail. */
int xv_cluster_query_replay(const XvClusterGeometry *,const XvClusterInput *,
    const XvClusterReplayLayout *,const xctx *entry,XvClusterResult *,XvClusterReplay *);
