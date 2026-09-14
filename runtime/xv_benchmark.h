#pragma once
#include <stdint.h>

enum {
    XV_BENCH_OBJECT_BASIS=1, XV_BENCH_MODEL_PALETTE, XV_BENCH_VERTEX_WORKER,
    XV_BENCH_VERTEX_REFERENCES, XV_BENCH_NATIVE_BOUNDS, XV_BENCH_VERTEX_COPY,
    XV_BENCH_DRAW_SCAN, XV_BENCH_FLARE, XV_BENCH_RESOLUTION, XV_BENCH_EARLY_VISIBILITY,
    XV_BENCH_POINT_MATH, XV_BENCH_TEXTURE_STATE, XV_BENCH_MATRIX_NEON,
    XV_BENCH_OBJECT_SCAN, XV_BENCH_HLE_DISPATCH, XV_BENCH_FLARE_QUERY_OVERLAP, XV_BENCH_GUEST_AFFINITY
};
/* Network admission only publishes a request. The ordinary guest input owner
 * validates first-person control and consumes it before the present boundary. */
int xv_benchmark_remote_request(unsigned kind);
void xv_benchmark_remote_poll(int control);
unsigned xv_benchmark_remote_busy(void);

/* Guest-thread state machine. Resolution changes run on the drained pump. */
void xv_benchmark_toggle(void);
/* Compare this build's optimization candidate at the current resolution. */
void xv_benchmark_compare_toggle(void);
/* Developer selection; default comparison remains deferred visibility. */
int xv_benchmark_compare_object_basis(void);
int xv_benchmark_compare_early_visibility(void);
int xv_benchmark_compare_point_math(void);
int xv_benchmark_compare_matrix_neon(void);
int xv_benchmark_compare_object_scan(void);
int xv_benchmark_compare_hle_dispatch(void);
int xv_benchmark_compare_flare_query_overlap(void);
int xv_benchmark_compare_guest_affinity(void);
int xv_benchmark_compare_texture_state(void);
int xv_benchmark_compare_model_palette(void);
int xv_benchmark_compare_vertex_worker(void);
int xv_benchmark_compare_vertex_references(void);
int xv_benchmark_compare_native_bounds(void);
int xv_benchmark_compare_draw_scan(void);
int xv_benchmark_compare_vertex_copy(void);
int xv_benchmark_active(void);
unsigned xv_benchmark_step(uint64_t now,unsigned height,int valid,const float view[6]);
void xv_benchmark_applied(uint64_t now,unsigned height);
/* Atomic overlay snapshot: height 0..9, progress 10..16, phase 17..18, candidate comparison 19. */
uint32_t xv_benchmark_status(void);
