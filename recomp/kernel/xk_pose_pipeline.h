/* Experimental render-only palette pipeline. One-frame-old complete poses;
 * never AI/physics state. All worker inputs and outputs are owned copies. */
#pragma once
#include <stdint.h>
typedef void (*xv_pose_product)(const float *,const float *,float *);
void xv_pose_pipeline_begin(uint64_t frame);
void xv_pose_pipeline_end(void);
void xv_pose_pipeline_invalidate(void);
void xv_pose_pipeline_shutdown(void);
/* Caller must be the presenting owner with joined guest object jobs. A miss
 * leaves output untouched. Numerical/layout admission belongs to the caller. */
int xv_pose_pipeline_try(uint32_t datum,uint32_t model,uint32_t pose,uint32_t nodes,
    unsigned count,const float *left,const void *right,unsigned stride,
    xv_pose_product product,float *output);
