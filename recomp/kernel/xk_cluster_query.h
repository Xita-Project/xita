#pragma once
#include <stdint.h>

/* Prototype only: caller owns a validated immutable geometry snapshot. There
 * is deliberately no production hook, guest pointer cache or live unlock. */
typedef struct { uint32_t first, count; } XvCluster;
typedef struct {
    int16_t sides[2];
    uint32_t plane;
    float center[3], radius;
    uint32_t first_vertex, vertices;
} XvPortal;
typedef struct { float v[3]; } XvPoint;
typedef struct { float v[4]; } XvPlane;
typedef struct {
    const XvCluster *clusters;
    const uint16_t *adjacency;
    const XvPortal *portals;
    const XvPoint *vertices;
    const XvPlane *distance_planes, *projection_planes;
    uint32_t cluster_count, adjacency_count, portal_count, vertex_count;
    uint32_t distance_plane_count, projection_plane_count;
    int16_t axes[6][2];
} XvClusterGeometry;
typedef struct {
    float center[3], radius;
    int16_t start;
    uint32_t epoch, budget;
    const uint32_t *visited;
} XvClusterInput;
typedef struct {
    uint16_t clusters[64];
    uint32_t count, epoch, changed[8], backedges, portal_tests;
    uint32_t maximum_depth;
} XvClusterResult;

/* Array pointers must refer to actual allocations covering their stated counts.
 * Validation is amortized at snapshot construction; callers must validate once,
 * keep the descriptor/arrays owned and unchanged during queries, and supply the
 * original axes lookup with the original zero comparison constant equal to 0.
 * Native FP traps must be disabled. No live guest-pointer constructor exists. */
int xv_cluster_geometry_valid(const XvClusterGeometry *);
/* Success describes numerical traversal output only. It does NOT reconstruct
 * guest scratch, context or native FPSCR for 566DE publication. A zero return
 * requires discarding the partial result and using the untouched original path.
 * Input, geometry, visited[256] and result allocations must not overlap. */
int xv_cluster_query_direct(const XvClusterGeometry *,const XvClusterInput *,XvClusterResult *);

enum { XV_CLUSTER_REBASE_CONFLICT=0, XV_CLUSTER_REBASE_NO_EPOCH=1,
       XV_CLUSTER_REBASE_EPOCH=2 };
/* Rebase a successful private numerical result against the current guarded
 * visited table. The captured input/visited copy and geometry must still be
 * valid. Matching already-visited sets preserve the traversal even if another
 * completed query advanced the epoch. Only result->epoch changes on success;
 * conflict leaves the entire result untouched. This writes NO shared memory
 * and does not validate other inputs, geometry lifetime or guest-state replay.
 * EPOCH requires publishing the new epoch and all changed[] words atomically
 * with the original list tail under its guard. NO_EPOCH writes neither.
 * The result must be private and disjoint from both visited arrays and input. */
int xv_cluster_query_rebase(const XvClusterInput *,XvClusterResult *,unsigned clusters,
    uint32_t current_epoch,const uint32_t current_visited[256]);

/* Stateful prototype: slots use indices relative to entry_fsp. Initialize all
 * eight slots and fsw from the entry context; successful return preserves the
 * original numerical prefix's x87 slots/status and balanced stack position.
 * Failure discards BOTH result and FP state and requires restoring the entry
 * native FP environment before fallback. Guest scratch/GP replay is separate.
 * Geometry validation/ownership and disabled traps have the same preconditions
 * as the numerical query; result and FP storage must be disjoint from each
 * other and from all input/geometry allocations. */
typedef struct {double slots[8];uint32_t entry_fsp;uint16_t fsw;} XvClusterFpu;
int xv_cluster_query_fpu(const XvClusterGeometry *,const XvClusterInput *,
                         XvClusterResult *,XvClusterFpu *);
