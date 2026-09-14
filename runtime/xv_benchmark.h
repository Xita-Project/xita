#pragma once
#include <stdint.h>

/* Guest-thread state machine. Resolution changes run on the drained pump. */
void xv_benchmark_toggle(void);
/* Compare this build's optimization candidate at the current resolution. */
void xv_benchmark_compare_toggle(void);
/* Developer selection; default comparison remains deferred visibility. */
int xv_benchmark_compare_object_basis(void);
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
