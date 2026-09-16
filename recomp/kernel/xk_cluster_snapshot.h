#pragma once
#include "xk_cluster_query.h"
#include "xk_cluster_query_replay.h"
#include <stddef.h>

typedef struct XvClusterSnapshot XvClusterSnapshot;
/* read must either copy the entire span or return zero. The caller must own a
 * stable source generation for the WHOLE build: bounds checks and copied bytes
 * cannot synchronize an uncooperative live writer. No source storage survives;
 * numeric address metadata is retained only for original-state reconstruction. */
typedef int (*XvClusterRead)(void *,uint32_t,void *,size_t);
typedef struct {
    uint32_t bsp, projection, axes, zero;
} XvClusterSource;
/* max_bytes bounds the two owned allocations, excluding allocator metadata and
 * libc sorting scratch. Invalid input, a failed read or allocation returns NULL. */
XvClusterSnapshot *xv_cluster_snapshot_build(XvClusterRead,void *,
    const XvClusterSource *,size_t max_bytes);
const XvClusterGeometry *xv_cluster_snapshot_geometry(const XvClusterSnapshot *);
size_t xv_cluster_snapshot_bytes(const XvClusterSnapshot *);
/* The returned layout borrows immutable arrays from this snapshot, just like
 * geometry(). Hold the same lease throughout computation. Numeric addresses
 * are not permission to read retired source data or publish into a live guest. */
int xv_cluster_snapshot_replay_layout(const XvClusterSnapshot *,uint32_t center,
    uint32_t head,XvClusterReplayLayout *);

/* Zero-initialize the store. All store operations AND shared-snapshot release
 * share one caller-owned mutex. An unpublished snapshot is caller-exclusive.
 * A lease pins the owned immutable arrays after that
 * mutex is released. Replace/retire do not wait for readers or retain guest
 * storage. Drain all leases before destroying the mutex/store. */
typedef struct {
    XvClusterSnapshot *current;
    uint64_t generation;
} XvClusterStore;
typedef struct {
    XvClusterSnapshot *snapshot;
    uint64_t generation;
} XvClusterLease;
/* On success transfers the builder's sole reference to the store. A snapshot
 * can be published once only. Failure leaves caller ownership unchanged. */
int xv_cluster_store_publish(XvClusterStore *,XvClusterSnapshot *);
void xv_cluster_store_retire(XvClusterStore *);
XvClusterLease xv_cluster_store_acquire(XvClusterStore *);
int xv_cluster_store_current(const XvClusterStore *,const XvClusterLease *);
void xv_cluster_snapshot_release(XvClusterSnapshot *);
