/* Single-producer/single-consumer ownership for two render-state snapshots.
 * This transports owned bytes, NOT guest pointers or an implicit world clone.
 * Producer and consumer handles must remain on their respective threads.
 * No allocation, semaphore, lock wait or overwrite of a pinned generation.
 * Integration must capture all render-visible mutable state before removing
 * existing object-job joins. This module alone does not make guest jobs safe.
 */
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void *data;
    size_t size;
    uint64_t generation;
    unsigned state;
} xv_snapshot_slot;
typedef struct {
    xv_snapshot_slot slots[2];
    size_t capacity;
    unsigned published;
    unsigned writing; /* producer-owned; 2 means none */
    unsigned reading; /* consumer-owned; 2 means none */
    uint64_t next_generation; /* producer-owned; never wraps */
} xv_frame_snapshot;
typedef struct {
    const void *data;
    size_t size;
    uint64_t generation;
} xv_snapshot_view;

/* Both buffers must be distinct, nonoverlapping, and remain alive until all
 * handles are released and both threads have stopped using the exchange.
 * Initialize only before threads start; no concurrent reset is supported. */
int xv_frame_snapshot_init(xv_frame_snapshot *, void *, void *, size_t);
/* NULL means no writable slot; keep the current snapshot, do not block. */
void *xv_frame_snapshot_begin(xv_frame_snapshot *);
int xv_frame_snapshot_publish(xv_frame_snapshot *, size_t);
void xv_frame_snapshot_cancel(xv_frame_snapshot *);
/* Returns 1 with a pinned immutable view, or 0. Pin at frame start before
 * dispatching updates; release only after every CPU user has finished.
 * GPU references, if any, must also retire before release. */
int xv_frame_snapshot_acquire(xv_frame_snapshot *, xv_snapshot_view *);
void xv_frame_snapshot_release(xv_frame_snapshot *);
