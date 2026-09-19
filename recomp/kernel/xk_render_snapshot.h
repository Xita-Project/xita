/* Owned model-pose snapshots. No guest pointers survive publication.
 * This payload is only the model-pose part of a render snapshot, not a world
 * clone. Callers must establish a quiescent capture boundary and route every
 * pose consumer through the acquired view before overlapping simulation. */
#pragma once
#include "xk_frame_snapshot.h"

enum { XV_RENDER_SNAPSHOT_OBJECTS=256, XV_RENDER_SNAPSHOT_NODES=64 };
typedef struct {
    uint32_t datum; /* Full salted handle, never just the low object index. */
    uint32_t model;
    uint32_t nodes;
    float matrices[XV_RENDER_SNAPSHOT_NODES][13];
} xv_render_pose;
typedef struct {
    uint64_t world_generation, tick;
    unsigned count;
    xv_render_pose poses[XV_RENDER_SNAPSHOT_OBJECTS];
} xv_render_state;
typedef struct {
    xv_frame_snapshot exchange;
    xv_render_state *building;
    unsigned failed;
} xv_render_snapshots;

int xv_render_snapshots_init(xv_render_snapshots *, xv_render_state *, xv_render_state *);
/* Ticks are supplied by the simulation owner, not inferred from present count. */
int xv_render_snapshots_begin(xv_render_snapshots *, uint64_t world, uint64_t tick);
/* Copy a complete pose. A duplicate, invalid pose or capacity overflow poisons
 * this publication; never publish an apparently complete partial object list. */
int xv_render_snapshots_add(xv_render_snapshots *, uint32_t datum, uint32_t model,
                           unsigned nodes, const float *matrices);
int xv_render_snapshots_publish(xv_render_snapshots *);
void xv_render_snapshots_cancel(xv_render_snapshots *);
const xv_render_state *xv_render_snapshots_acquire(xv_render_snapshots *, uint64_t world);
void xv_render_snapshots_release(xv_render_snapshots *);
const xv_render_pose *xv_render_snapshot_find(const xv_render_state *, uint32_t datum,
                                             uint32_t model);
